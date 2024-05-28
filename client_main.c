#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 3334
#define MAX_MESSAGE_SIZE 1024

void *receive_thread(void *arg) {
    int sockfd = *((int *)arg);
    char buffer[MAX_MESSAGE_SIZE];

    while (1) {
        int bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            printf("Server disconnected.\n");
            break;
        }

        buffer[bytes_received] = '\0';
        printf("Received from server: %s\n", buffer);
    }

    close(sockfd);
    pthread_exit(NULL);
}

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    pthread_t tid;
    if (pthread_create(&tid, NULL, receive_thread, (void *)&sockfd) != 0) {
        perror("pthread_create");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    char message[MAX_MESSAGE_SIZE];
    while (1) {
        printf("Enter message to send (or type 'exit' to quit): ");
        fgets(message, MAX_MESSAGE_SIZE, stdin);
        message[strcspn(message, "\n")] = '\0'; // Remove newline character

        if (strcmp(message, "exit") == 0) {
            printf("Exiting...\n");
            break;
        }

        int bytes_sent = send(sockfd, message, strlen(message), 0);
        if (bytes_sent == -1) {
            perror("send");
            break;
        }
    }

    pthread_join(tid, NULL);
    close(sockfd);
    return 0;
}
