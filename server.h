#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <sys/select.h>

#define BUFFER_SIZE 1600  // dimensiunea maxima a bufer-ului pentru mesaje
#define MAX_CLIENTS 100  // numarul maxim de clienti TCP conectati simultan
#define MAX_TOPIC_LEN 50  // lungimea maxima a unui topic in octeteti

// Structura pentru reprezentarea unui client TCP
typedef struct {
	int sockfd;  // socket-ul TCP al clientului (-1 daca nu e conectat)              
	char id[11];  // id-ul clientului (10 caractere + terminator de sir)
	int subcount;  // numarul de topicuri la care este abonat clientul
	char subscriptions[MAX_CLIENTS][MAX_TOPIC_LEN + 1];  // lista de topic-uri la care este 
														// abonat clientul
} client_t;

// Creaza si configureaza un socket TCP
// Primeste portul TCP pe care sa asculte serverul
// Returneaza descriptorul socket-ului creat sau -1 in caz de eroare
int setup_tcp_socket(uint16_t port);

// Creaza si configureaza un socket UDP
// Primeste portul UDP pe care sa asculte serverul
// Returneaza descriptorul socket-ului creat sau -1 in caz de eroare
int setup_udp_socket(uint16_t port);

// Dezactiveaza Nagle pentru un socket TCP
// Primeste descriptorul socket-ului TCP
// Returneaza 0 in caz de succes sau -1 in caz de eroare
void disable_nagle(int sockfd);

// Gestioneaza o noua conexiune TCP
// Primeste descriptorul socket-ului TCP, lista de clienti, numarul de clienti,
// setul de file descriptori master si descriptorul maxim
void handle_new_tcp_connection(int tcp_sock, client_t clients[], int *nclients, fd_set *master_fds, int *fdmax);

// Gestioneaza un mesaj TCP de la un client deja conectat
// Primeste indexul clientului, lista de clienti, numarul de clienti,
// setul de file descriptori master
void handle_tcp_message(int idx, client_t clients[], fd_set *master_fds);

// Gestioneaza un mesaj UDP
// Primeste descriptorul socket-ului UDP, lista de clienti si numarul de clienti
void handle_udp_message(int udp_sock, client_t clients[], int nclients);

// Verifica daca un topic (cu wildcard-uri) se potriveste unui topic de mesaj
// Primeste un topic de abonare si un topic de mesaj
// Returneaza 1 daca se potrivesc, 0 altfel
int topic_matches(const char *sub, const char *msg);

#endif
