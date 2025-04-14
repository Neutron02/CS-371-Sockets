/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/* 
Please specify the group members here

# Student #1: Sam Scott
# Student #2: N/A
# Student #3: N/A

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct {
    int epoll_fd;        /* File descriptor for the epoll instance, used for monitoring events on the socket. */
    int socket_fd;       /* File descriptor for the client socket connected to the server. */
    long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    long total_messages; /* Total number of messages sent and received. */
    float request_rate;  /* Computed request rate (requests per second) based on RTT and total messages. */
    unsigned long transmitted_cnt; // packets transmitted
    unsigned long received_cnt; // packets recieved
} client_thread_data_t;

/*
 * This function runs in a separate client thread to handle communication with the server
 */
void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;
    
    // Hint 1: register the "connected" client_thread's socket in the its epoll instance
    // Hint 2: use gettimeofday() and "struct timeval start, end" to record timestamp, which can be used to calculated RTT.

    /* TODO:
     * It sends messages to the server, waits for a response using epoll,
     * and measures the round-trip time (RTT) of this request-response.
     */

    int nfds, bytes_received;

    // register connected socket with epoll instance
    event.data.fd = data->socket_fd;
    event.events = EPOLLIN;

    // add connected socket fd to epoll
    if(epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event) == -1){
        perror("Epoll_ctl: add client socket");
        pthread_exit(NULL);
    }
    
    data->total_rtt = 0;
    data->total_messages = 0;
    data->transmitted_cnt = 0;
    data->received_cnt = 0;

    for(int i = 0; i < num_requests; i++){
        // get timestamp
        gettimeofday(&start, NULL);

        // send to server
        if(send(data->socket_fd, send_buf, MESSAGE_SIZE, 0) != MESSAGE_SIZE){
            perror("send");
            break;
        }
        data->transmitted_cnt++;

        // wait for event, keep blocking
        nfds = epoll_wait(data->epoll_fd, events, MAX_EVENTS, -1);
        if(nfds == -1){
            perror("epoll_wait");
            break;
        }

        // find socket event by looking through the fds, if event is ready, then break
        for(int j = 0; j < nfds; j++){
            if(events[j].data.fd == data->socket_fd && (events[j].events & EPOLLIN)){
                bytes_received = recv(data->socket_fd, recv_buf, MESSAGE_SIZE, 0);
                if(bytes_received <= 0){
                    perror("recv: either no bytes are received, or an error");
                    char buf[32];
                    sprintf(buf, "bytes_received is: %d", bytes_received);
                } else {
                    data->received_cnt++;
                    printf("Received %d bytes from server\n", bytes_received);
                }
            }
        }

        // get end timestamp
        gettimeofday(&end, NULL);

        // calculate RTT in microseconds
        long long rtt = (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);

        data->total_rtt += rtt;
        data->total_messages++;
    }

    // calculate request rate: #requests/s
    if(data->total_rtt > 0)
        data->request_rate = (float)data->total_messages / (data->total_rtt / 1000000.0);
    else
        data->request_rate = 0;
    
    /* TODO:
     * The function exits after sending and receiving a predefined number of messages (num_requests). 
     * It calculates the request rate based on total messages and RTT
     */

    printf("Thread %ld: transmitted_cnt=%lu, received_cnt=%lu, lost=%lu\n",
           pthread_self(), data->transmitted_cnt, data->received_cnt,
           data->transmitted_cnt - data->received_cnt);

    return NULL;
}

/*
 * This function orchestrates multiple client threads to send requests to a server,
 * collect performance data of each threads, and compute aggregated metrics of all threads.
 */
void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    struct sockaddr_in server_addr;
    int i;

    /* TODO:
     * Create sockets and epoll instances for client threads
     * and connect these sockets of client threads to the server
     */
    
    // setup server address structure
    //  init mem to 0
    // set addr fam
    // set port
    // conv to binary
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if(inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0){
        perror("inet_pton failure");
        exit(EXIT_FAILURE);
    }

    // looop through threads and create sockets, connect, and epoll
    for(i = 0; i < num_client_threads; i++){
        // now making udp instead of tcp
        thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if(thread_data[i].socket_fd < 0){
            perror("socket error");
            exit(EXIT_FAILURE);
        }
        
        // connect to server
        if(connect(thread_data[i].socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("connect");
            exit(EXIT_FAILURE);
        }

        // create an epoll instance for the thread
        thread_data[i].epoll_fd = epoll_create1(0);
        if(thread_data[i].epoll_fd < 0){
            perror("epoll_create1");
            exit(EXIT_FAILURE);
        }
    }

    // create client threads
    for(i = 0; i < num_client_threads; i++){
        if(pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]) != 0){
            perror("pthread create");
        }
    }

    // wait for threads to finish, then exit
    for(i = 0; i < num_client_threads; i++){
        pthread_join(threads[i], NULL);
    }

    long long total_rtt = 0;
    long total_messages = 0;
    unsigned long total_transmitted = 0;
    unsigned long total_received = 0;
    unsigned long total_lost = 0;
    for(i = 0; i < num_client_threads; i++){
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_transmitted += thread_data[i].transmitted_cnt;
        total_received += thread_data[i].received_cnt;
        total_lost += thread_data[i].transmitted_cnt - thread_data[i].received_cnt;
    }

    // should be 1
    long long average_rtt = total_messages ? total_rtt / total_messages : 0;
    float total_request_rate = total_rtt ? ((float)total_messages) / (total_rtt / 1000000.0) : 0;

    /* TODO:
     * Wait for client threads to complete and aggregate metrics of all client threads
     */

    printf("Average RTT: %lld us\n", total_rtt / total_messages);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);

    printf("RTT * request rate: %f", (total_rtt / total_messages) * total_request_rate / 1000000);
    printf("Total transmitted: %lu\n", total_transmitted);
    printf("Total received: %lu\n", total_received);
    printf("Total lost: %lu\n", total_lost);
}

void run_server() {

    /* TODO:
     * Server creates listening socket and epoll instance.
     * Server registers the listening socket to epoll
     */

    // inits
    // int listen_fd, conn_fd, epoll_fd;
    // now only a socket file decriptor
    int sock_fd, epoll_fd;
    struct sockaddr_in server_addr, client_addr;
    struct epoll_event event, events[MAX_EVENTS];
    socklen_t client_len = sizeof(client_addr);
    char buf[MESSAGE_SIZE];
    int nfds, n;
    
    // listening socket
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock_fd < 0){
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // init mem
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    // bind to listen socket
    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    // // allow up to 10 queue size for connects
    // if (listen(listen_fd, 10) < 0) {
    //     perror("listen");
    //     close(listen_fd);
    //     exit(EXIT_FAILURE);
    // }

    // make epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    event.data.fd = listen_fd;
    event.events = EPOLLIN;

    // add fd of the listening socket to epoll
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) < 0) {
        perror("epoll_ctl: listen_fd");
        close(listen_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    /* Server's run-to-completion event loop */
    while (1) {
        /* TODO:
         * Server uses epoll to handle connection establishment with clients
         * or receive the message from clients and echo the message back
         */

         nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
         if (nfds < 0) {
             perror("epoll_wait");
             close(listen_fd);
             close(epoll_fd);
             exit(EXIT_FAILURE);
         }

        for (n = 0; n < nfds; n++) {
            // new connection on listen socket, so aaccept it
            if (events[n].data.fd == listen_fd) {
                conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
                if (conn_fd < 0) {
                    perror("accept");
                    continue;
                }

                // register
                event.data.fd = conn_fd;
                event.events = EPOLLIN;

                // add fd of new client socket to epoll
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &event) < 0) {
                    perror("epoll_ctl: conn_fd");
                    close(conn_fd);
                    continue;
                }
            } else {
                // for if an event is already on a connection
                int client_fd = events[n].data.fd;
                int bytes_received = recv(client_fd, buf, MESSAGE_SIZE, 0);
                if (bytes_received <= 0) {
                    // nothging received, close the connect
                    close(client_fd);
                } else {
                    // echo it back to client
                    send(client_fd, buf, bytes_received, 0);
                }
            }
        }
    }

    // cleanup
    close(listen_fd);
    close(epoll_fd);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);

        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);

        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }

    return 0;
}