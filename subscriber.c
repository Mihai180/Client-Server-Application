#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include "server.h"

/*
Trimite un mesaj pe socket-ul TCP cu framing de tip:
- 2 octeți (uint16_t) în network byte order care indică lungimea mesajului;
- conținutul mesajului ca șir de caractere, fără '\0';
*/
int send_msg(int sockfd, const char *m) {
    uint16_t length = htons(strlen(m));  // lungimea mesajului
    if (send (sockfd, &length, sizeof(length), 0) != sizeof (length)) {
        return -1;  // eroare la trimiterea mesajului
    }
    if (send (sockfd, m, strlen(m), 0) != (int)strlen(m)) {
        return -1;  // eroare la trimiterea mesajului
    }
    return 0;
}

/*
Primește un mesaj de tip framing TCP:
- citește 2 octeți (uint16_t) pentru lungime în network byte order;
- alocă buffer și primește payload-ul de lungimea indicată;
- termină șirul cu '\0' și returnează pointerul la buffer;
*/
char *recv_msg(int sockfd) {
    uint16_t length;
    if (recv (sockfd, &length, sizeof(length), 0) <= 0) {
        return NULL;  // conexiune închisă sau eroare
    }

    uint16_t l = ntohs(length);
    if (l == 0|| l > BUFFER_SIZE) {
        return NULL;  // lungime invalidă
    }

    // Aloc buffer pentru payload + terminator de șir
    char *buf = malloc (l + 1);
    if (!buf) {  // eroare la alocare
        return NULL;
    }

    // Citire payload
    if (recv (sockfd, buf, l, 0) <= 0) {
        free(buf);
        return NULL;  // conexiune închisă sau eroare
    }
    buf[l]='\0';
    return buf;
}

int main(int argc, char *argv[]) {
    if (argc!=4) {  // verific argumentele
        return EXIT_FAILURE;
    }

    // Dezactivez buffering-ul pentru stdout
    setvbuf(stdout,NULL,_IONBF,0);

    // Creare socket TCP și dezactivare Nagle
    int sockfd = socket(AF_INET,SOCK_STREAM,0);
    disable_nagle(sockfd);

    // Configurare adresa server și conectare
    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;  // IPv4
    server.sin_port   = htons(atoi(argv[3]));  // portul server-ului
    inet_pton(AF_INET,argv[2],&server.sin_addr);  // IP-ul server-ului
    connect(sockfd,(struct sockaddr*)&server,sizeof(server));

    // Trimit ID-ul clientului
    send_msg(sockfd, argv[1]);

    // Pregătesc mulțimile pentru select
    fd_set master;
    fd_set read_fds;
    FD_ZERO(&master);
    FD_SET(STDIN_FILENO,&master);  // stdin
    FD_SET(sockfd,&master);  // socket server
    int fdmax = sockfd;

    char line[BUFFER_SIZE];
    while(1) {
        read_fds = master;
        select(fdmax + 1, &read_fds, NULL, NULL, NULL);

        // Verific dacă am primit date de la stdin
        if (FD_ISSET (STDIN_FILENO, &read_fds)) {
            if (!fgets(line, sizeof(line), stdin)) {  // EOF sau eroare
                break;
            }
            // Scot newline
            line[strcspn(line,"\n")] = '\0';
            if (!strcmp (line, "exit")) {  // primire exit
                send_msg(sockfd,"exit");
                break;
            }
            if (!strncmp (line, "subscribe ", 10) || !strncmp (line, "unsubscribe ", 12)) {  // comenzi subscribe/unsubscribe
                send_msg(sockfd, line);
            }
        }

        // Mesaje de la server
        if (FD_ISSET (sockfd, &read_fds)) {
            char *m = recv_msg(sockfd);
            if (!m) {  // conexiune închisă sau eroare
                break;
            }
            printf("%s\n",m);  // afișez mesajul
            free(m);
        }
    }

    // Închid socket
    close(sockfd);
    return 0;
}
