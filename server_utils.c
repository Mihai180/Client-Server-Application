#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/tcp.h>


/*
Creează și configurează un socket TCP pentru ascultare pe portul dat.
Creaza unnsocket TCP, configureaza reutilizarea portului pentru reveniri rapide,
leagă socket-ul de toate interfețele locale și portul specificat,
pune socket-ul în modul de ascultare, cu un backlog pentru MAX_CLIENTS
si dezactivează algoritmul Nagle pentru latență minimă la trimiterea mesajelor.
*/
int setup_tcp_socket(uint16_t port) {
    // Creez socket-ul TCP
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { 
        perror("socket TCP");
        exit(EXIT_FAILURE); 
    }

    // Configurez reutilizarea portului
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Formez adresa de bind: orice interfață + portul dat
    struct sockaddr_in server = {0};  // structura de configurare a adresei
    server.sin_family = AF_INET;  // IPv4
    server.sin_addr.s_addr = INADDR_ANY;  // ascult pe toate interfețele
    server.sin_port = htons(port);  // portul dat

    // Fac bind la port
    if (bind(sockfd, (struct sockaddr*)&server, sizeof(server)) < 0) {
        perror("bind TCP");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Pun socket-ul în mod pasiv, listen cu backlog MAX_CLIENTS
    if (listen(sockfd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Dezactivez algoritmul Nagle pentru latență minimă
    disable_nagle(sockfd);
    return sockfd;
}

/*
Deschide un socket UDP și îl leaga de portul specificat.
Primește portul UDP pe care să asculte serverul.
Returnează descriptorul socket-ului creat sau -1 în caz de eroare.
*/
int setup_udp_socket(uint16_t port) {
    // Creez socket-ul UDP
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { 
        perror("socket UDP");
        exit(EXIT_FAILURE);
    }

    // Configurez adresa locală pentru bind
    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;  // IPv4
    server.sin_addr.s_addr = INADDR_ANY;  // ascult pe toate interfețele
    server.sin_port = htons(port);  // portul dat

    // Fac bind la port
    if (bind(sockfd, (struct sockaddr*)&server, sizeof(server)) < 0) {
        perror("bind UDP");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

/*
Dezactivez algoritmul Nagle pe socket-ul TCP indicat.
Primeste descriptorul socket-ului TCP.
*/
void disable_nagle(int sockfd) {
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

/*
Primește o nouă conexiune TCP și procesează framing-ul pentru ID-ul clientului.
Acceptă conexiunea și obține descriptorul clientului,
primește doi octeți cu lungimea ID-ului, apoi șirul ID,
verifică dacă clientul este deja înregistrat și reface conexiunea dacă este cazul
inregistrează clientul nou dacă nu există precedent
adaugă socket-ul client nou în mulțimea monitorizată și actualizează fdmax.
*/
void handle_new_tcp_connection(int tcp_sock, client_t clients[], int *nclients, fd_set *master_fds, int *fdmax)
{
    struct sockaddr_in client;  // structura pentru informatiile adresei clientului
    socklen_t len = sizeof(client);

    // Accept conexiunea TCP
    int newsock = accept(tcp_sock, (struct sockaddr*)&client, &len);
    if (newsock < 0) { 
        perror("accept");
        return;
    }

    // Dezactivez Nagle pentru latență minimă
    disable_nagle(newsock);

    // Citesc framing-ul pentru ID-ul clientului, 2 octeți pentru lungime
    uint16_t length;
    if (recv(newsock, &length, sizeof(length), 0) <= 0) { 
        close(newsock);
        return;
    }

    // Verific lungimea ID-ului
    length = ntohs(length);
    if (length == 0 || length > 10)  {
        close(newsock);
        return;
    }

    // Citesc ID-ul clientului
    char id[11] = {0};
    if (recv(newsock, id, length, 0) <= 0) {
        close(newsock);
        return;
    }
    id[length] = '\0';

    // Verific daca este reconectare sau clientul este deja conectat
    for (int i = 0; i < *nclients; i++) {
        if (!strcmp(clients[i].id, id)) {
            if (clients[i].sockfd != -1) {
                // Clientul este deja conectat
                printf("Client %s already connected.\n", id);
                close(newsock);
                return;
            } else {
                // Clientul a mai fost conectat, reconectez
                clients[i].sockfd = newsock;
                FD_SET(newsock, master_fds);
                if (newsock > *fdmax) *fdmax = newsock;
                printf("New client %s connected from %s:%d\n", id, inet_ntoa(client.sin_addr), ntohs(client.sin_port));
                return;
            }
        }
    }

    // Clientul este nou, il initializez si il adaug in lista
    client_t *c = &clients[*nclients];
    c->sockfd   = newsock;
    strncpy(c->id, id, 10);
    c->subcount = 0;
    (*nclients)++;
    FD_SET(newsock, master_fds);
    if (newsock > *fdmax) {
        *fdmax = newsock;
    }

    printf("New client %s connected from %s:%d\n", id, inet_ntoa(client.sin_addr), ntohs(client.sin_port));
}

/*
Procesează o comanda TCP de la un client deja conectat.
Citește framing-ul: lungime + conținut, interprețează comanda: subscribe, 
unsubscribe sau exit, actualizează lista de abonari și mulțimea descriptorilor
daca este cazul.
*/
void handle_tcp_message(int idx, client_t clients[], fd_set *master_fds)
{
    // Clientul de la care vine comanda
    client_t *c = &clients[idx];
    int s = c->sockfd;

    // Citesc cei doi octeti care reprezinta lungimea mesajului
    uint16_t length;
    int r = recv(s, &length, sizeof(length), 0);
    if (r <= 0) {
        // Clientul nu este conectat
        FD_CLR(s, master_fds);
        close(s);
        c->sockfd = -1;
        return;
    }
    length = ntohs(length);
    if (length == 0 || length > BUFFER_SIZE) {  // Lungimea este invalidă
        return;
    }

    // Citesc payload-ul si pun terminatorul de sir
    char buf[BUFFER_SIZE + 1] = {0};
    r = recv(s, buf, length, 0);
    if (r <= 0) {
        // Clientul nu este conectat
        FD_CLR(s, master_fds);
        close(s);
        c->sockfd = -1;
        return;
    }
    buf[length] = '\0';

    // Interpretez comanda
    if (strncmp(buf, "subscribe ", 10) == 0) {  // comanda subscribe
        char *topic = buf + 10;
        // validare minimală
        if (*topic && strlen(topic) <= MAX_TOPIC_LEN) {
            // Verific daca topic-ul este deja in lista de abonamente
            // Daca nu este, il adaug
            int found = 0;
            for (int i = 0; i < c->subcount; i++) {
                if (!strcmp(c->subscriptions[i],topic)) {
                    found=1;
                    break;
                }
            }
            if (!found && c->subcount < MAX_CLIENTS)
                strcpy(c->subscriptions[c->subcount++], topic);

            // Raspund clientului cu mesajul de confirmare
            char response[BUFFER_SIZE];
            snprintf(response,BUFFER_SIZE,"Subscribed to topic %s",topic);
            uint16_t payload_len = htons(strlen(response));
            send(s, &payload_len, sizeof(payload_len),0);
            send(s, response, strlen(response),0);
        }
    }
    else if (strncmp(buf, "unsubscribe ", 12) == 0) {  // comanda unsubscribe
        char *topic = buf + 12;
        // Caut topic-ul in lista de abonamente si il sterg daca exista
        for (int i = 0; i < c->subcount; i++) {
            if (!strcmp(c->subscriptions[i], topic)){
                for (int j = i; j < c->subcount - 1; j++)
                    strcpy(c->subscriptions[j], c->subscriptions[j+1]);
                c->subcount--;
                break;
            }
        }

        // Raspund clientului cu mesajul de confirmare
        char response[BUFFER_SIZE];
        snprintf(response, BUFFER_SIZE, "Unsubscribed from topic %s", topic);
        uint16_t payload_len = htons(strlen(response));
        send(s, &payload_len, sizeof(payload_len),0);
        send(s, response, strlen(response),0);
    }
    else if (strcmp(buf,"exit")==0) {  // comanda exit
        printf("Client %s disconnected.\n", c->id);
        FD_CLR(s, master_fds);
        close(s);
        c->sockfd = -1;
    }
}

/*
Primește o datagramă UDP, decodează topic-ul și payload-ul,
formatează un mesaj text și îl trimite către fiecare client abonat.
Suportă tipuri: INT, SHORT_REAL, FLOAT, STRING.
*/
void handle_udp_message(int udp_sock, client_t clients[], int nclients) {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;  // structura pentru informatiile adresei clientului
    socklen_t cli_len = sizeof(client_addr);

    // Citesc datagrama UDP si adresa clientului
    int bytes = recvfrom(udp_sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &cli_len);
    if (bytes < MAX_TOPIC_LEN + 1) {
        return;  // Trebuiue sa fie cel putin topic + tip
    }

    // Extrag topicul
    char topic[MAX_TOPIC_LEN + 1];
    memcpy(topic, buffer, MAX_TOPIC_LEN);
    topic[MAX_TOPIC_LEN] = '\0';
    int topic_len = strlen(topic);
    topic[topic_len] = '\0';

    // Citesc tipul mesajului
    uint8_t type = (uint8_t)buffer[MAX_TOPIC_LEN];
    int off = MAX_TOPIC_LEN + 1;

    // Decodez payloadul in functie de tip
    char typestr[16];
    char valstr[BUFFER_SIZE + 1];
    if (type == 0) {  // INT
        // Payload: 1 octet semn + 4 octeți valoare
        if (off + 5 > bytes) {
            return;  // Nu este suficient pentru un int
        }
        uint8_t sign = buffer[off++];  // 0 = pozitiv, 1 = negativ
        uint32_t number;
        memcpy(&number, buffer + off, 4);
        off += 4;
        int32_t v = ntohl(number);
        if (sign) {  // Aplic semnul
            v = -v;
        } 
        strcpy(typestr, "INT");
        snprintf(valstr, sizeof(valstr), "%d", v);

    } else if (type == 1) {  // SHORT_REAL
        // Payload: 2 octeți valoare inmultita cu 100
        if (off + 2 > bytes) {
            return;  // Nu este suficient pentru un short
        }
        uint16_t number;
        memcpy(&number, buffer + off, 2);
        off += 2;
        double d = ntohs(number) / 100.0;
        strcpy(typestr, "SHORT_REAL");
        snprintf(valstr, sizeof(valstr), "%.2f", d);

    } else if (type == 2) {  // FLOAT
        // Payload: 1 octet semn + 4 octeți valoare + 1 octet exponent
        if (off + 6 > bytes) {
            return;  // Nu este suficient pentru un float
        }
        uint8_t sign = buffer[off++];  // 0 = pozitiv, 1 = negativ
        uint32_t number;
        memcpy(&number, buffer + off, 4);
        off += 4;
        uint8_t exp = buffer[off++];
        double div = 1.0;
        for (int i = 0; i < exp; i++) {
            div *= 10.0;
        }
        double m = ntohl(number) / div;
        if (sign) {
            m = -m;
        }
        strcpy(typestr, "FLOAT");
        char format[16];
        snprintf(format, sizeof(format), "%%.%df", exp);
        snprintf(valstr, sizeof(valstr), format, m);

    } else if (type == 3) {  // STRING
        // Payload: restul octeților reprezintă șirul text
        strcpy(typestr, "STRING");
        int char_num = bytes - off;
        if (char_num < 0) char_num= 0;
        // limitează la spațiul din valstr-1 pentru terminator
        int clen;
        if (char_num < (int)sizeof(valstr) - 1) {
            // dacă numărul de caractere din payload e mai mic decât spațiul disponibil
            clen = char_num;
        } else {
            // altfel, iau câte încap în buffer (fără locul pentru terminatorul de șir)
            clen = sizeof(valstr) - 1;
        }
        memcpy(valstr, buffer + off, clen);
        valstr[clen] = '\0';

    } else {  // Comanda necunoscută
        return;
    }

    // Construiesc mesajul final "IP:PORT - topic - tip - valoare"
    size_t needed = strlen(topic) + strlen(typestr) + strlen(valstr) + 64;
    char *msg = malloc(needed);
    snprintf(msg, needed, "%s:%d - %s - %s - %s", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), 
                                                                                        topic, typestr, valstr);

    // Fac forward către toți clienții abonați
    for (int i = 0; i < nclients; i++) {
        client_t *c = &clients[i];
        if (c->sockfd == -1) {
            continue;
        }
        for (int j = 0; j < c->subcount; j++) {
            if (topic_matches(c->subscriptions[j], topic)) {
                uint16_t len = htons(strlen(msg));
                send(c->sockfd, &len, sizeof(len), 0);
                send(c->sockfd, msg, strlen(msg), 0);
                break;
            }
        }
    }
    free(msg);
}

/*
Verifică dacă topic-ul unui mesaj se potrivește cu pattern-ul.
Împarte pattern-ul și mesajul pe segmente delimitate de ‘/’ și folosește
programare dinamică pentru a testa toate cazurile.
*/
int topic_matches(const char *pattern, const char *msg) {
    // Copiez pattern și msg în buffer-uri modificabile
    char pat_copy[MAX_TOPIC_LEN+1];
    char msg_copy[BUFFER_SIZE+1];
    strncpy(pat_copy, pattern, MAX_TOPIC_LEN);
    pat_copy[MAX_TOPIC_LEN] = '\0';
    strncpy(msg_copy, msg, BUFFER_SIZE);
    msg_copy[BUFFER_SIZE] = '\0';

    // Sparg pe fiecare pe segmente separate prin '/'
    char *pat_parts[ MAX_TOPIC_LEN + 1 ];
    char *msg_parts[ MAX_TOPIC_LEN + 1 ];
    int pcount = 0;
    int mcount = 0;
    for (char *tok = strtok(pat_copy, "/"); tok; tok = strtok(NULL, "/")) {
        pat_parts[pcount++] = tok;
    }
    for (char *tok = strtok(msg_copy, "/"); tok; tok = strtok(NULL, "/")) {
        msg_parts[mcount++] = tok;
    }

    // Construim matricea dp: dp[i][j] == true dacă pat_parts[i] poate potrivi msg_parts[j]
    // Dimensiuni max (pcount + 1)x(mcount + 1)
    bool dp[MAX_TOPIC_LEN + 2][MAX_TOPIC_LEN + 2];
    for (int i = 0; i <= pcount; i++)
        for (int j = 0; j <= mcount; j++)
            dp[i][j] = false;
    dp[pcount][mcount] = true;  // succes când am consumat tot pattern și tot mesajul

    // Completez matricea în ordine inversă
    for (int i = pcount; i >= 0; i--) {
        for (int j = mcount; j >= 0; j--) {
            if (i == pcount && j == mcount) {
                continue;
            }
            if (i == pcount) {
                dp[i][j] = false;
                continue;
            }
            char *seg = pat_parts[i];
            if (strcmp(seg, "*") == 0) {
                // '*' poate acoperi de la 0 la (mcount-j) segmente
                bool ok = false;
                for (int k = j; k <= mcount; k++) {
                    if (dp[i+1][k]) {
                        ok = true;
                        break;
                    }
                }
                dp[i][j] = ok;
            }
            else if (strcmp(seg, "+") == 0) {
                // '+' trebuie să consume exact un segment
                if (j < mcount && dp[i+1][j+1]) {
                    dp[i][j] = true;
                } else {
                    dp[i][j] = false;
                }
            }
            else {
                // segment literal: trebuie să fie identic cu msg_parts[j]
                if (j < mcount) {
                    // există un segment în msg_parts de comparat
                    if (strcmp(seg, msg_parts[j]) == 0) {
                        // segmentul pattern se potrivește cu segmentul mesajului
                        if (dp[i+1][j+1]) {
                            dp[i][j] = true;
                        } else {
                            dp[i][j] = false;
                        }
                    } else {
                        // segmentele nu se potrivesc literal
                        dp[i][j] = false;
                    }
                } else {
                    // nu mai sunt segmente în msg_parts
                    dp[i][j] = false;
                }
            }
        }
    }

    // Returnez rezultatul final
    // dp[0][0] == true dacă pattern-ul se potrivește cu mesajul
    return dp[0][0];
}
