#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include "server.h"

int main(int argc, char *argv[]) {
    // Verific argumentele primite de server
    if (argc != 2) {
        return EXIT_FAILURE;
    }
    // Portul server-ului
    uint16_t port = atoi(argv[1]);

    // Dezactivez buffering-ul pentru afișare
    setvbuf(stdout,NULL,_IONBF,BUFSIZ);

    // Creez socket-urile TCP și UDP pe portul dat
    int tcp_sock = setup_tcp_socket(port);
    int udp_sock = setup_udp_socket(port);

    // Inițializez structura pentru clienți, inițializându-i pe toți cu -1
    // adică deconectați și numărul de clienți conectați la 0
    client_t clients[MAX_CLIENTS];
    int nclients = 0;
    for(int i = 0; i < MAX_CLIENTS; i++) {
       clients[i].sockfd = -1;
    }

    // Configurare file descriptori pentru select
    fd_set master;
    fd_set read_fds;
    FD_ZERO(&master);
    // STDIN pentru a detecta comanda exit
    FD_SET(STDIN_FILENO,&master);
    // TCP socket pentru clienți noi
    FD_SET(tcp_sock,&master);
    // UDP socket pentru mesaje
    FD_SET(udp_sock,&master);

    // Calzulez descriptorul maxim folosit de select
    int fdmax;
    if (tcp_sock > udp_sock) {
        fdmax = tcp_sock;
    } else {
        fdmax = udp_sock;
    }

    // Bucla principală pentru multiplexare input/output
    while(1) {
        read_fds = master; // copiez setul de file descriptori pentru select
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) < 0) {
            break;
        }
        
        // Comanda exit de la stdin
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char cmd[16];
            if (fgets(cmd, sizeof(cmd), stdin) && !strncmp(cmd, "exit", 4))
                break;
        }
        
        // Conexiune TCP nouă
        // Acceptă conexiunea și adaugă clientul în lista de clienți
        if (FD_ISSET(tcp_sock, &read_fds)) {
            handle_new_tcp_connection(tcp_sock, clients, &nclients, &master, &fdmax);
        }
        
        // UDP primire si forward mesaje
        if (FD_ISSET(udp_sock, &read_fds)) {
            handle_udp_message(udp_sock, clients, nclients);
        }
        
        // TCP mesaje de la clienți deja conectați
        for(int i = 0; i < nclients; i++) {
            int s = clients[i].sockfd;
            if (s != -1 && FD_ISSET(s, &read_fds))
                handle_tcp_message(i, clients, &master);
        }
    }

    // Inchid toate socket-urile deschise
    for(int i = 0; i < nclients; i++)
        if (clients[i].sockfd != -1)
            close(clients[i].sockfd);
    close(tcp_sock);
    close(udp_sock);
    return 0;
}
