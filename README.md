# Tema 2 – Aplicație client-server TCP și UDP pentru gestionarea mesajelor

Păunescu Mihai-Ionuț 322CD

## Structura proiectului

- Makefile
- README.md
- server_utils.c
- server.c
- server.h
- subscriber.c

## Descriere fișiere

### Makefile  
Definește regulile de compilare:  
- **CC**: compilator (`gcc`)  
- **CFLAGS**: opțiuni de warning, standard C11 și debug  
- **all**: target implicit, construiește `server` și `subscriber`  
- **server**: compilează `server.c` + `server_utils.c` → `server`  
- **subscriber**: compilează `subscriber.c` + `server_utils.c` → `subscriber`  
- **clean**: șterge executabilele  
- **.PHONY**: declară `all` și `clean` ca ținte

### server.h  
Header comun ce conține:  
- **Macro-uri**: `BUFFER_SIZE`, `MAX_CLIENTS`, `MAX_TOPIC_LEN`  
- **Structura** `client_t`: descriptor TCP, ID (10+1 caractere), număr abonamente și lista de topic-uri  
- **Header** pentru:  
  - `setup_tcp_socket(uint16_t port)`
  - `setup_udp_socket(uint16_t port)`
  - `disable_nagle(int sockfd)`
  - `handle_new_tcp_connection(int tcp_sock, client_t clients[], int *nclients, fd_set *master_fds, int *fdmax)`
  - `handle_tcp_message(int idx, client_t clients[], fd_set *master_fds)`
  - `handle_udp_message(int udp_sock, client_t clients[], int nclients)`
  - `topic_matches(const char *sub, const char *msg)`

#### setup_tcp_socket(uint16_t port)  
- **Rol:** Iniţializarea şi configurarea socket-ului TCP pe care serverul ascultă.
- **Funcţionalitate:**
  1. Creează un socket TCP.
  2. Activează `SO_REUSEADDR` pentru a permite re‐bind rapid după închidere.
  3. Leagă socket-ul de toate interfeţele locale şi portul specificat (`bind`).
  4. Pune socket-ul în modul pasiv (`listen`) cu backlog `MAX_CLIENTS`.
  5. Dezactivează algoritmul Nagle (`TCP_NODELAY`) pentru latenţă minimă.

#### setup_udp_socket(uint16_t port)  
- **Rol:** Pregăteşte socket-ul UDP pe care serverul primeşte mesaje.  
- **Funcţionalitate:**  
  1. Creează un socket UDP.  
  2. Leagă socket-ul de toate interfeţele locale şi portul specificat (`bind`).  

#### disable_nagle(int sockfd)  
- **Rol:** Scade latenţa comunicaţiei TCP pentru mesaje scurte.  
- **Funcţionalitate:**  
  - Setează opţiunea `TCP_NODELAY` pe descriptorul `sockfd` pentru a trimite imediat pachetele fără buffering.

#### handle_new_tcp_connection(int tcp_sock, client_t clients[], int *nclients, fd_set *master_fds, int *fdmax)  
- **Rol:** Gestionează o conexiune TCP nouă de tip subscriber.  
- **Funcţionalitate:**  
  1. Acceptă conexiunea (`accept`).  
  2. Dezactivează Nagle pe noul socket.  
  3. Citeşte framing-ul de login: doi octeţi lungime + ID client.  
  4. Verifică dacă clientul se reconectează sau este nou:  
     - Dacă se reconectează, reactivatează socket-ul.  
     - Dacă este nou, il înregistrează în vectorul `clients[]`.  
  5. Adaugă socket-ul în `master_fds` şi actualizează `fdmax`.

#### handle_tcp_message(int idx, client_t clients[], fd_set *master_fds)  
- **Rol:** Procesează comenzi TCP trimise de un subscriber deja conectat.  
- **Funcţionalitate:**  
  1. Citeşte framing-ul: doi octeţi lungime + payload.  
  2. Interpretează payload-ul ca text:  
     - `subscribe <TOPIC>` → adaugă topic în `clients[idx].subscriptions` şi trimite confirmare.  
     - `unsubscribe <TOPIC>` → elimină topic din abonamente şi trimite confirmare.  
     - `exit` → deconectează clientul (închide socket, şterge din `master_fds`).  

#### handle_udp_message(int udp_sock, client_t clients[], int nclients)  
- **Rol:** Primeşte mesaje UDP de la publisheri şi le forwardează abonaţilor relevanţi.  
- **Funcţionalitate:**  
  1. Primeşte o datagramă UDP (`recvfrom`) într-un buffer brut.  
  2. Extrage topic-ul de 50 octeţi (`char topic[50]`).  
  3. Citeşte `data_type` şi decodează payload-ul în unul din formatele:  
     - **INT**, **SHORT_REAL**, **FLOAT**, **STRING**.  
  4. Formatează un mesaj text:  
     ```
     "<IP_UDP>:<PORT_UDP> - <TOPIC> - <TIP_DATE> - <VALOARE>"
     ```  
  5. Parcurge toţi clienţii activi și, pentru fiecare, verifică abonamentele cu `topic_matches(...)`.  
  6. Trimite framing-ul mesajului (lungime + text) către cei potriviţi.

#### topic_matches(const char *pattern, const char *msg)  
- **Rol:** Suportă filtrarea cu wildcard-uri în abonamente.  
- **Funcţionalitate:**  
  1. Sparge `pattern` şi `msg` pe segmente delimitate de `/`.  
  2. Construieşte o matrice `dp[i][j]`, folosind programare dinamica pentru potrivire cu:  
     - `*` → zero sau mai multe segmente  
     - `+` → exact un segment  
     - literal → egalitate exactă  
  3. Returnează `1` dacă pattern-ul acoperă întregul mesaj (`dp[0][0]`), altfel `0`.

### server_utils.c  
Implementarile funcţiilor auxiliare folosite de server:  
- `setup_tcp_socket`
- `setup_udp_socket`  
- `disable_nagle`
- `handle_new_tcp_connection`
- `handle_tcp_message`
- `handle_udp_message`
- `topic_matches`

### server.c  
Serverul realizează:  
- Citirea portului din argumente și dezactivarea buffering-ului stdout  
- Inițializarea socket-urilor TCP și UDP
- Configurarea setului master de file-descriptori (STDIN, TCP, UDP)  
- Bucla `select` pentru multiplexare:  
  - `exit` de la STDIN → terminare  
  - conexiuni TCP noi → `handle_new_tcp_connection`  
  - mesaje UDP → `handle_udp_message`  
  - comenzi TCP de la abonați → `handle_tcp_message`  
- Închiderea a tuturor socket-urilor  

### subscriber.c  
Client TCP “subscriber”:  
- Citește `ID_CLIENT`, `IP_SERVER`, `PORT_SERVER` din linia de comandă  
- Deschide socket TCP, disable Nagle și se conectează la server  
- Trimite framing-ul cu ID-ul (2 octeți lungime + șir ID)  
- Buclă `select` între STDIN și socket:  
  - de la STDIN: `subscribe <TOPIC>`, `unsubscribe <TOPIC>`, `exit` → trimite framing TCP  
  - de la server: primește framing de confirmare sau mesaje (`MESSAGE_FROM_SERVER`) și le afișează
