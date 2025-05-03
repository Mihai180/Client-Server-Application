#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include "server.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr,"Usage: %s <PORT>\n", argv[0]);
        return EXIT_FAILURE;
    }
    uint16_t port = atoi(argv[1]);

    // disable stdout buffering
    setvbuf(stdout,NULL,_IONBF,BUFSIZ);

    int tcp_sock = setup_tcp_socket(port);
    int udp_sock = setup_udp_socket(port);
    //printf("Server listening on port %d...\n", port);

    client_t clients[MAX_CLIENTS];
    int nclients = 0;
    // init clienţi
    for(int i=0;i<MAX_CLIENTS;i++) clients[i].sockfd = -1;

    fd_set master, read_fds;
    FD_ZERO(&master);
    FD_SET(STDIN_FILENO,&master);
    FD_SET(tcp_sock,&master);
    FD_SET(udp_sock,&master);
    int fdmax = tcp_sock>udp_sock?tcp_sock:udp_sock;

    while(1) {
        read_fds = master;
        if (select(fdmax+1,&read_fds,NULL,NULL,NULL)<0) {
            perror("select"); break;
        }
        // exit din consolă
        if (FD_ISSET(STDIN_FILENO,&read_fds)) {
            char cmd[16];
            if (fgets(cmd,sizeof(cmd),stdin) && !strncmp(cmd,"exit",4))
                break;
        }
        // connection TCP nouă
        if (FD_ISSET(tcp_sock,&read_fds))
            handle_new_tcp_connection(tcp_sock,clients,&nclients,&master,&fdmax);
        // mesaje UDP (stub)
        if (FD_ISSET(udp_sock,&read_fds))
            handle_udp_message(udp_sock,clients,nclients);
        // mesaje TCP deja conectați
        for(int i=0;i<nclients;i++) {
            int s = clients[i].sockfd;
            if (s!=-1 && FD_ISSET(s,&read_fds))
                handle_tcp_message(i,clients,&nclients,&master);
        }
    }

    // cleanup
    for(int i=0;i<nclients;i++)
        if (clients[i].sockfd!=-1)
            close(clients[i].sockfd);
    close(tcp_sock);
    close(udp_sock);
    return 0;
}
