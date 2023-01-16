#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>

#define BUFFER_SIZE 1024
#define MAX_CLI 3
#define PORT 8081

// Splitting header by space to get filename
void splitHeader(char *header, int header_size, char *fileName);

// get number of active clients
int getClientsNumber(int *use);

// putting active clients on begging of arrays
void sort(int *use, struct pollfd *clients[], int *state, FILE **file, int *n, char **ip, char **fileName);

int main() {

    signal(SIGPIPE, SIG_IGN); //Disabling SIGPIPE signal to prevent crash on send() when connection is broken

    int sock;
    struct sockaddr_in address;
    socklen_t address_size = sizeof(address);
    int opt = 1;
    char buffer[BUFFER_SIZE];
    int received;
    struct pollfd clients[MAX_CLI];
    int use[MAX_CLI];
    int state[MAX_CLI];
    FILE *file[MAX_CLI];
    int n[MAX_CLI];
    char *ip[MAX_CLI];
    char *fileName[MAX_CLI];
    char cli_ip[16];
    char fileNameForClient[256];
    int newConnection;

    // Set array to 0
    memset(use, 0, sizeof(use));


    // Creating socket file descriptor
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        return 1;
    } else {
        printf("Socket created.\n");
    }

    // Adding server to clients
    use[0] = 1;
    clients[0].fd = sock;
    clients[0].events = POLLIN;


    // Forcefully attaching socket to the port 8080
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        return 1;
    } else {
        printf("Socket attached.\n");
    }

    // Sockaddr configuration
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Forcefully attaching socket to the PORT
    if (bind(sock, (struct sockaddr *) &address, address_size) < 0) {
        perror("bind failed");
        return 1;
    } else {
        printf("Socket bound.\n");
    }

    // Start listening
    if (listen(sock, 3) < 0) {
        perror("listen");
        return 1;
    } else {
        printf("Listening started.\n");
    }


    while (1) {
        // put active clients on top of arrays
        sort(use, (struct pollfd **) clients, state, file, n, ip, fileName);

        if (poll(&clients[0], getClientsNumber(use), -1)) {
            for (int i = 0; i < MAX_CLI; i++) {
                if (clients[i].revents & POLLIN || clients[i].revents & POLLOUT) {
                    if (i == 0) {
                        // New connection handling
                        newConnection = accept(sock, (struct sockaddr *) &address, &address_size);
                        if (newConnection < 0) {
                            perror("accept");
                        } else {
                            // Looking for place for a new client
                            for (int j = 1; j < MAX_CLI; j++) {
                                if (!use[j]) {
                                    use[j] = 1;
                                    state[j] = 1;
                                    clients[j].fd = newConnection;
                                    clients[j].events = POLLIN;
                                    inet_ntop(AF_INET, &address.sin_addr.s_addr, cli_ip, sizeof(cli_ip));
                                    printf("New connection from %s.\n", cli_ip);
                                    ip[j] = cli_ip;
                                    break;
                                }
                                // If server can't handle more clients
                                if (j == MAX_CLI - 1) {
                                    send(newConnection, "Server is overloaded", 21, 0);
                                    close(newConnection);

                                }
                            }
                        }
                    } else {
                        switch (state[i]) {
                            // Reading header
                            case 1:
                                if (clients[i].revents & POLLIN) {
                                    memset(buffer, '\0', BUFFER_SIZE);
                                    received = recv(clients[i].fd, buffer, BUFFER_SIZE, 0);

                                    //recv() error or connection is closed
                                    if (received < 0) {
                                        state[i] = 4;
                                    } else {
                                        // client request = upload
                                        if (buffer[0] == 'u') {
                                            clients[i].events = POLLIN;
                                            splitHeader(buffer, MAX_CLI, (char *) &fileNameForClient);
                                            // save filename
                                            fileName[i] = fileNameForClient;
                                            file[i] = fopen(fileName[i], "wb");

                                            // check if file opened correctly
                                            // if open file error
                                            if (file[i] == NULL) {
                                                send(clients[i].fd, "File error.", 12, 0);
                                                state[i] = 4;
                                            } else {
                                                // file opened correctly
                                                state[i] = 2;
                                            }
                                        } else if (buffer[0] == 'd') {
                                            // client request = download
                                            state[i] = 3;
                                            splitHeader(buffer, MAX_CLI, (char *) &fileNameForClient);
                                            fileName[i] = fileNameForClient;
                                            file[i] = fopen(fileName[i], "rb");
                                            // check if file opened correctly
                                            // if open file error
                                            if (file[i] == NULL) {
                                                send(clients[i].fd, "File error.", 12, 0);
                                                state[i] = 4;
                                            } else {
                                                // file opened correctly
                                                clients[i].events = POLLOUT;
                                            }
                                        } else {
                                            send(clients[i].fd, "Wrong header", 12, 0);
                                        }
                                    }
                                }

                                break;

                            case 2: //Upload request
                                if (clients[i].revents & POLLOUT) {
                                    memset(buffer, '\0', BUFFER_SIZE);
                                    received = recv(clients[i].fd, buffer, BUFFER_SIZE, 0);
                                    if (received > 0) {
                                        fwrite(buffer, 1, received, file[i]);
                                        clients[i].events = POLLIN;
                                    } else if (received == 0) {
                                        state[i] = 1;
                                    } else { //recv() error or connection is closed
                                        state[i] = 4;
                                    }
                                }
                                break;

                            case 3: //Download request
                                if (clients[i].revents & POLLIN) {
                                    n[i] = fread(buffer, 1, BUFFER_SIZE, file[i]);
                                    if (n[i]) {
                                        if (send(clients[i].fd, buffer, n[i], 0) < 0) {
                                            state[i] = 4;
                                        }
                                        memset(buffer, '\0', BUFFER_SIZE);
                                        clients[i].events = POLLOUT;
                                    } else {
                                        state[i] = 1;
                                    }
                                }
                                break;

                            case 4: //Close connection
                                clients[i].events = 0;
                                inet_ntop(AF_INET, &address.sin_addr.s_addr, cli_ip, sizeof(cli_ip));
                                printf("Close connetion with: %s.\n", cli_ip);
                                fclose(file[i]);
                                close(clients[i].fd);
                                use[i] = 0;
                                state[i] = 0;
                                break;
                        }
                    }
                //}
            }
        } else {
            perror("poll");
            exit(EXIT_FAILURE);
        }
    }
}


void splitHeader(char *header, int header_size, char *fileName) {
    int afterSpace = 0;
    for (int i = 0; i < header_size && header[i] != '\0'; i++) {
        if (header[i] == ' ') {
            afterSpace = i + 1;
            break;
        }
    }
    if (afterSpace == 0) { //Wrong header
        sprintf(fileName, "%s", "");
    } else {
        if (sizeof(&header[afterSpace]) < 256) { //Check if file name is smaller than 256
            sprintf(fileName, "%s", &header[afterSpace]);
        }

    }
}

int getClientsNumber(int *use) {
    int output = 0;
    int i = 0;
    while (use[i] == 1) {
        output++;
        i++;
    }
    return output;
}

void sort(int *use, struct pollfd *clients[], int *state, FILE **file, int *n, char **ip, char **fileName) {
    int i = 1;
    int j = MAX_CLI - 1;

    int a;
    struct pollfd *b;
    char *c;
    FILE **d;

    for (int j = MAX_CLI; j > 1; j--) {
        if (use[j] == 1) {
            for (int i = 1; i < MAX_CLI; i++) {
                if (use[i] == 0 && i < j) {
                    a = use[i];
                    use[i] = use[j];
                    use[j] = a;

                    b = clients[i];
                    clients[i] = clients[j];
                    clients[j] = b;

                    a = state[i];
                    state[i] = state[j];
                    state[j] = a;

                    a = n[i];
                    n[i] = n[j];
                    n[j] = a;

                    c = ip[i];
                    ip[i] = ip[j];
                    ip[j] = c;

                    c = fileName[i];
                    fileName[i] = fileName[j];
                    fileName[j] = c;

                    d = (FILE **) file[i];
                    file[i] = file[j];
                    file[j] = (FILE *) d;

                    i++;
                    j--;
                }
            }
        }
    }
}
