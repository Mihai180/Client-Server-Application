# TCP and UDP Client-Server Application for Message Management

**Păunescu Mihai-Ionuț**

## Project Structure

- Makefile
- README.md
- server_utils.c
- server.c
- server.h
- subscriber.c

## Files Descriptions

### Makefile  
Defines the compilation rules:  
- **CC**: compiler (`gcc`)  
- **CFLAGS**: warning options, C11 standard, and debug mode  
- **all**: default target, builds `server` and `subscriber`  
- **server**: compiles `server.c` + `server_utils.c` → `server`  
- **subscriber**: compiles `subscriber.c` + `server_utils.c` → `subscriber`  
- **clean**: removes executables  
- **.PHONY**: declares `all` and `clean` as phony targets

### server.h  
Shared header containing:  
- **Macros**: `BUFFER_SIZE`, `MAX_CLIENTS`, `MAX_TOPIC_LEN`  
- **`client_t` structure**: TCP descriptor, ID (10+1 characters), number of subscriptions, and list of topics  
- **Function headers** for:  
  - `setup_tcp_socket(uint16_t port)`  
  - `setup_udp_socket(uint16_t port)`  
  - `disable_nagle(int sockfd)`  
  - `handle_new_tcp_connection(int tcp_sock, client_t clients[], int *nclients, fd_set *master_fds, int *fdmax)`  
  - `handle_tcp_message(int idx, client_t clients[], fd_set *master_fds)`  
  - `handle_udp_message(int udp_sock, client_t clients[], int nclients)`  
  - `topic_matches(const char *sub, const char *msg)`

#### setup_tcp_socket(uint16_t port)  
- **Purpose:** Initializes and configures the TCP socket the server listens on.  
- **Functionality:**  
  1. Creates a TCP socket.  
  2. Enables `SO_REUSEADDR` for fast rebinding after close.  
  3. Binds the socket to all local interfaces and the specified port.  
  4. Sets the socket to passive mode (`listen`) with a `MAX_CLIENTS` backlog.  
  5. Disables the Nagle algorithm (`TCP_NODELAY`) to minimize latency.

#### setup_udp_socket(uint16_t port)  
- **Purpose:** Prepares the UDP socket the server receives messages on.  
- **Functionality:**  
  1. Creates a UDP socket.  
  2. Binds it to all local interfaces and the specified port.

#### disable_nagle(int sockfd)  
- **Purpose:** Reduces TCP communication latency for short messages.  
- **Functionality:**  
  - Sets the `TCP_NODELAY` option on the `sockfd` descriptor to send packets immediately without buffering.

#### handle_new_tcp_connection(int tcp_sock, client_t clients[], int *nclients, fd_set *master_fds, int *fdmax)  
- **Purpose:** Handles a new TCP connection from a subscriber.  
- **Functionality:**  
  1. Accepts the connection.  
  2. Disables Nagle on the new socket.  
  3. Reads login framing: two-byte length + client ID.  
  4. Checks if the client is reconnecting or new:  
     - If reconnecting, reactivates the socket.  
     - If new, registers the client in the `clients[]` array.  
  5. Adds the socket to `master_fds` and updates `fdmax`.

#### handle_tcp_message(int idx, client_t clients[], fd_set *master_fds)  
- **Purpose:** Processes TCP commands sent by an already connected subscriber.  
- **Functionality:**  
  1. Reads framing: two-byte length + payload.  
  2. Interprets the payload as text:  
     - `subscribe <TOPIC>` → adds topic to `clients[idx].subscriptions` and sends confirmation  
     - `unsubscribe <TOPIC>` → removes topic and sends confirmation  
     - `exit` → disconnects client (closes socket, removes from `master_fds`)

#### handle_udp_message(int udp_sock, client_t clients[], int nclients)  
- **Purpose:** Receives UDP messages from publishers and forwards them to relevant subscribers.  
- **Functionality:**  
  1. Receives a raw UDP datagram (`recvfrom`) into a buffer.  
  2. Extracts the topic (50 characters).  
  3. Reads the `data_type` and decodes the payload into one of the formats:  
     - **INT**, **SHORT_REAL**, **FLOAT**, **STRING**  
  4. Formats a message string:  
     ```
     "<UDP_IP>:<UDP_PORT> - <TOPIC> - <DATA_TYPE> - <VALUE>"
     ```  
  5. Iterates over all active clients and, for each, checks their subscriptions using `topic_matches(...)`.  
  6. Sends the framed message (length + text) to relevant clients.

#### topic_matches(const char *pattern, const char *msg)  
- **Purpose:** Supports wildcard filtering in subscriptions.  
- **Functionality:**  
  1. Splits `pattern` and `msg` by `/` into segments.  
  2. Builds a `dp[i][j]` matrix using dynamic programming to match:  
     - `*` → zero or more segments  
     - `+` → exactly one segment  
     - literal → exact match  
  3. Returns `1` if the pattern covers the entire message (`dp[0][0]`), otherwise `0`.

### server_utils.c  
Implements helper functions used by the server:  
- `setup_tcp_socket`  
- `setup_udp_socket`  
- `disable_nagle`  
- `handle_new_tcp_connection`  
- `handle_tcp_message`  
- `handle_udp_message`  
- `topic_matches`

### server.c  
The server performs:  
- Reads port from arguments and disables stdout buffering  
- Initializes TCP and UDP sockets  
- Sets up the master file descriptor set (STDIN, TCP, UDP)  
- `select` loop for multiplexing:  
  - `exit` from STDIN → terminates server  
  - new TCP connections → `handle_new_tcp_connection`  
  - UDP messages → `handle_udp_message`  
  - TCP commands from subscribers → `handle_tcp_message`  
- Closes all sockets on termination

### subscriber.c  
TCP client "subscriber":  
- Reads `CLIENT_ID`, `SERVER_IP`, and `SERVER_PORT` from command line  
- Opens a TCP socket, disables Nagle, and connects to the server  
- Sends framing with ID (2-byte length + ID string)  
- `select` loop between STDIN and socket:  
  - from STDIN: `subscribe <TOPIC>`, `unsubscribe <TOPIC>`, `exit` → sends TCP framing  
  - from server: receives confirmation or message framing (`MESSAGE_FROM_SERVER`) and displays them
