#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <stdint.h>

#define BUFFER_SIZE 1600

void disable_nagle(int sockfd) {
    int flag = 1;
    setsockopt(sockfd,IPPROTO_TCP,TCP_NODELAY,&flag,sizeof(flag));
}

int send_msg(int sockfd, const char *m) {
    uint16_t l = htons(strlen(m));
    if (send(sockfd,&l,sizeof(l),0)!=sizeof(l)) return -1;
    if (send(sockfd,m,strlen(m),0)!= (int)strlen(m)) return -1;
    return 0;
}

char *recv_msg(int sockfd) {
    uint16_t l_n;
    if (recv(sockfd,&l_n,sizeof(l_n),0)<=0) return NULL;
    uint16_t l = ntohs(l_n);
    if (l==0||l>BUFFER_SIZE) return NULL;
    char *b = malloc(l+1);
    if (!b) return NULL;
    if (recv(sockfd,b,l,0)<=0) { free(b); return NULL; }
    b[l]='\0';
    return b;
}

int main(int argc, char *argv[]) {
    if (argc!=4) {
        fprintf(stderr,"Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n",argv[0]);
        return EXIT_FAILURE;
    }
    setvbuf(stdout,NULL,_IONBF,0);

    int sockfd = socket(AF_INET,SOCK_STREAM,0);
    disable_nagle(sockfd);

    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_port   = htons(atoi(argv[3]));
    inet_pton(AF_INET,argv[2],&serv.sin_addr);
    connect(sockfd,(struct sockaddr*)&serv,sizeof(serv));

    // trimite ID la conectare
    send_msg(sockfd, argv[1]);

    fd_set master, read_fds;
    FD_ZERO(&master);
    FD_SET(STDIN_FILENO,&master);
    FD_SET(sockfd,&master);
    int fdmax = sockfd;

    char line[BUFFER_SIZE];
    while(1) {
        read_fds = master;
        select(fdmax+1,&read_fds,NULL,NULL,NULL);
        if (FD_ISSET(STDIN_FILENO,&read_fds)) {
            if (!fgets(line,sizeof(line),stdin)) break;
            line[strcspn(line,"\n")] = '\0';
            if (!strcmp(line,"exit")) { send_msg(sockfd,"exit"); break;}
            if (!strncmp(line,"subscribe ",10) || !strncmp(line,"unsubscribe ",12))
                send_msg(sockfd,line);
        }
        if (FD_ISSET(sockfd,&read_fds)) {
            char *m = recv_msg(sockfd);
            if (!m) break;
            printf("%s\n",m);
            free(m);
        }
    }
    close(sockfd);
    return 0;
}
