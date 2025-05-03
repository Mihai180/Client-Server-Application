#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <sys/select.h>

#define BUFFER_SIZE 1600
#define MAX_CLIENTS 100
#define MAX_TOPIC_LEN 50

typedef struct {
    int sockfd;                 // -1 dacă nu e conectat
    char id[11];                // ID client
    int subcount;               // câte topic-uri are
    char subscriptions[MAX_CLIENTS][MAX_TOPIC_LEN+1];
} client_t;

// socket TCP
int setup_tcp_socket(uint16_t port);
// socket UDP (stub, nu e folosit deocamdată)
int setup_udp_socket(uint16_t port);
// dezactivează Nagle
void disable_nagle(int sockfd);

// TCP: accept, subscribe/unsubscribe, exit
void handle_new_tcp_connection(int tcp_sock,
                               client_t clients[],
                               int *nclients,
                               fd_set *master_fds,
                               int *fdmax);
void handle_tcp_message(int idx,
                        client_t clients[],
                        int *nclients,
                        fd_set *master_fds);

// (stub) UDP
void handle_udp_message(int udp_sock,
                        client_t clients[],
                        int nclients);

// (stub) wildcard matching simplu
int topic_matches(const char *sub, const char *msg);

#endif // SERVER_H
