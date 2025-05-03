#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

// Crează TCP socket, bind+listen, disable Nagle
int setup_tcp_socket(uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket TCP"); exit(EXIT_FAILURE); }
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("bind TCP");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    if (listen(sockfd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    disable_nagle(sockfd);
    return sockfd;
}

// Stub UDP (nefolosit)
int setup_udp_socket(uint16_t port) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("socket UDP"); exit(EXIT_FAILURE); }
    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(port);
    if (bind(s, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("bind UDP");
        close(s);
        exit(EXIT_FAILURE);
    }
    return s;
}

// Disable Nagle
void disable_nagle(int sockfd) {
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

// TCP accept + framing pentru ID
void handle_new_tcp_connection(int tcp_sock,
                               client_t clients[],
                               int *nclients,
                               fd_set *master_fds,
                               int *fdmax)
{
    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    int newsock = accept(tcp_sock, (struct sockaddr*)&cli, &len);
    if (newsock < 0) { perror("accept"); return; }
    disable_nagle(newsock);

    // 1) Read framed ID
    uint16_t l;
    if (recv(newsock, &l, sizeof(l), 0) <= 0) { close(newsock); return; }
    l = ntohs(l);
    if (l == 0 || l > 10)  { close(newsock); return; }
    char id[11] = {0};
    if (recv(newsock, id, l, 0) <= 0) { close(newsock); return; }
    id[l] = '\0';

    // 2) Dacă există deja un client cu acest ID...
    for (int i = 0; i < *nclients; i++) {
        if (!strcmp(clients[i].id, id)) {
            if (clients[i].sockfd != -1) {
                // client activ deja → refuz
                printf("Client %s already connected.\n", id);
                close(newsock);
                return;
            } else {
                // reconectare → refolosim slotul și păstrăm subscrierile
                clients[i].sockfd = newsock;
                FD_SET(newsock, master_fds);
                if (newsock > *fdmax) *fdmax = newsock;
                printf("New client %s connected from %s:%d\n",
                       id, inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));
                return;
            }
        }
    }

    // 3) Altfel e client nou – înregistrăm un slot nou
    client_t *c = &clients[*nclients];
    c->sockfd   = newsock;
    strncpy(c->id, id, 10);
    c->subcount = 0;
    (*nclients)++;
    FD_SET(newsock, master_fds);
    if (newsock > *fdmax) *fdmax = newsock;

    printf("New client %s connected from %s:%d\n",
           id, inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));
}

// TCP subscribe/unsubscribe/exit
void handle_tcp_message(int idx,
                        client_t clients[],
                        int *nclients,
                        fd_set *master_fds)
{
    client_t *c = &clients[idx];
    int s = c->sockfd;

    // read length
    uint16_t l;
    int r = recv(s, &l, sizeof(l), 0);
    if (r <= 0) {
        // client s-a deconectat
        printf("Client %s disconnected.\n", c->id);
        FD_CLR(s, master_fds);
        close(s);
        c->sockfd = -1;
        return;
    }
    l = ntohs(l);
    if (l == 0 || l > BUFFER_SIZE) return;

    char buf[BUFFER_SIZE+1] = {0};
    r = recv(s, buf, l, 0);
    if (r <= 0) {
        printf("Client %s disconnected.\n", c->id);
        FD_CLR(s, master_fds);
        close(s);
        c->sockfd = -1;
        return;
    }
    buf[l] = '\0';

    if (strncmp(buf, "subscribe ", 10) == 0) {
        char *topic = buf + 10;
        // validare minimală
        if (*topic && !strchr(topic,' ') && strlen(topic)<=MAX_TOPIC_LEN) {
            // adaugă dacă nu e deja
            int found = 0;
            for (int i=0;i<c->subcount;i++)
                if (!strcmp(c->subscriptions[i],topic)) { found=1; break; }
            if (!found && c->subcount < MAX_CLIENTS)
                strcpy(c->subscriptions[c->subcount++], topic);

            // feedback
            char resp[BUFFER_SIZE];
            snprintf(resp,BUFFER_SIZE,"Subscribed to topic %s",topic);
            uint16_t rn = htons(strlen(resp));
            send(s, &rn, sizeof(rn),0);
            send(s, resp, strlen(resp),0);
        }
    }
    else if (strncmp(buf, "unsubscribe ", 12) == 0) {
        char *topic = buf + 12;
        // caută şi şterge
        for (int i=0;i<c->subcount;i++){
            if (!strcmp(c->subscriptions[i],topic)){
                for (int j=i;j<c->subcount-1;j++)
                    strcpy(c->subscriptions[j],c->subscriptions[j+1]);
                c->subcount--;
                break;
            }
        }
        char resp[BUFFER_SIZE];
        snprintf(resp,BUFFER_SIZE,"Unsubscribed from topic %s",topic);
        uint16_t rn = htons(strlen(resp));
        send(s, &rn, sizeof(rn),0);
        send(s, resp, strlen(resp),0);
    }
    else if (strcmp(buf,"exit")==0) {
        printf("Client %s disconnected.\n", c->id);
        FD_CLR(s, master_fds);
        close(s);
        c->sockfd = -1;
    }
}

void handle_udp_message(int udp_sock, client_t clients[], int nclients) {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    // 1) Primește UDP
    int bytes = recvfrom(udp_sock, buffer, sizeof(buffer), 0,
                         (struct sockaddr*)&cli_addr, &cli_len);
    if (bytes < MAX_TOPIC_LEN + 1) return;  // cel puţin topic+type

    // 2) Extrage topic ca şir fix-length de MAX_TOPIC_LEN
    char topic[MAX_TOPIC_LEN+1];
    memcpy(topic, buffer, MAX_TOPIC_LEN);
    topic[MAX_TOPIC_LEN] = '\0';
    // elimină padding-ul de '\0'
    int tlen = strnlen(topic, MAX_TOPIC_LEN);
    topic[tlen] = '\0';

    // 3) Citeşte type şi setează offset
    uint8_t type = (uint8_t)buffer[MAX_TOPIC_LEN];
    int off = MAX_TOPIC_LEN + 1;

    char typestr[16], valstr[64];
    if (type == 0) {  // INT
        if (off + 5 > bytes) return;
        uint8_t sign = buffer[off++];
        uint32_t neti;
        memcpy(&neti, buffer + off, 4);
        off += 4;
        int32_t v = ntohl(neti);
        if (sign) v = -v;
        strcpy(typestr, "INT");
        snprintf(valstr, sizeof(valstr), "%d", v);

    } else if (type == 1) {  // SHORT_REAL
        if (off + 2 > bytes) return;
        uint16_t netsh;
        memcpy(&netsh, buffer + off, 2);
        off += 2;
        double d = ntohs(netsh) / 100.0;
        strcpy(typestr, "SHORT_REAL");
        snprintf(valstr, sizeof(valstr), "%.2f", d);

    } else if (type == 2) {  // FLOAT
        if (off + 6 > bytes) return;
        uint8_t sign = buffer[off++];
        uint32_t netm;
        memcpy(&netm, buffer + off, 4);
        off += 4;
        uint8_t exp = buffer[off++];
        double div = 1.0;
        for (int i = 0; i < exp; i++) div *= 10.0;
        double m = ntohl(netm) / div;
        if (sign) m = -m;
        strcpy(typestr, "FLOAT");
        char fmt[16];
        snprintf(fmt, sizeof(fmt), "%%.%df", exp);
        snprintf(valstr, sizeof(valstr), fmt, m);

    } else if (type == 3) {  // STRING
        strcpy(typestr, "STRING");
        int sl = bytes - off;
        if (sl < 0) sl = 0;
        // limitează la spațiul din valstr-1 pentru terminator
        int clen = sl < (int)sizeof(valstr)-1 ? sl : (int)sizeof(valstr)-1;
        memcpy(valstr, buffer + off, clen);
        valstr[clen] = '\0';

    } else {
        return;
    }

    // 4) Construiește mesajul TCP
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
             "%s:%d - %s - %s - %s",
             inet_ntoa(cli_addr.sin_addr),
             ntohs(cli_addr.sin_port),
             topic, typestr, valstr);

    // 5) Forward către toți abonații
    for (int i = 0; i < nclients; i++) {
        client_t *c = &clients[i];
        if (c->sockfd == -1) continue;
        for (int j = 0; j < c->subcount; j++) {
            if (topic_matches(c->subscriptions[j], topic)) {
                uint16_t ln = htons(strlen(msg));
                send(c->sockfd, &ln, sizeof(ln), 0);
                send(c->sockfd, msg, strlen(msg), 0);
                break;
            }
        }
    }
}


int topic_matches(const char *pattern, const char *msg) {
    // 1) Copiem pattern și msg în buffer-uri modifiabile
    char pat_copy[MAX_TOPIC_LEN+1];
    char msg_copy[BUFFER_SIZE+1];
    strncpy(pat_copy, pattern, MAX_TOPIC_LEN);
    pat_copy[MAX_TOPIC_LEN] = '\0';
    strncpy(msg_copy, msg, BUFFER_SIZE);
    msg_copy[BUFFER_SIZE] = '\0';

    // 2) Spargem fiecare pe segmente separate prin '/'
    char *pat_parts[ MAX_TOPIC_LEN + 1 ];
    char *msg_parts[ MAX_TOPIC_LEN + 1 ];
    int pcount = 0, mcount = 0;
    for (char *tok = strtok(pat_copy, "/"); tok; tok = strtok(NULL, "/"))
        pat_parts[pcount++] = tok;
    for (char *tok = strtok(msg_copy, "/"); tok; tok = strtok(NULL, "/"))
        msg_parts[mcount++] = tok;

    // 3) Construim tabela DP: dp[i][j] == true dacă pat_parts[i..] poate potrivi msg_parts[j..]
    //    Dimensiuni max (pcount+1)x(mcount+1)
    bool dp[ MAX_TOPIC_LEN+2 ][ MAX_TOPIC_LEN+2 ];
    for (int i = 0; i <= pcount; i++)
        for (int j = 0; j <= mcount; j++)
            dp[i][j] = false;
    dp[pcount][mcount] = true;  // succes când am consumat tot pattern și tot mesajul

    // 4) Umplem tabla în ordine inversă
    for (int i = pcount; i >= 0; i--) {
        for (int j = mcount; j >= 0; j--) {
            if (i == pcount && j == mcount) continue;
            if (i == pcount) {
                dp[i][j] = false;
                continue;
            }
            char *seg = pat_parts[i];
            if (strcmp(seg, "*") == 0) {
                // '*' poate acoperi 0..(mcount-j) segmente
                bool ok = false;
                for (int k = j; k <= mcount; k++) {
                    if (dp[i+1][k]) { ok = true; break; }
                }
                dp[i][j] = ok;
            }
            else if (strcmp(seg, "+") == 0) {
                // '+' trebuie să consume exact un segment
                dp[i][j] = (j < mcount && dp[i+1][j+1]);
            }
            else {
                // literal: seg trebuie să fie identic cu msg_parts[j]
                dp[i][j] = (j < mcount
                            && strcmp(seg, msg_parts[j]) == 0
                            && dp[i+1][j+1]);
            }
        }
    }

    return dp[0][0];
}

