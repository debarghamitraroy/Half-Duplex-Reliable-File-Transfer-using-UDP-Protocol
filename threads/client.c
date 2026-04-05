#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX 80
#define PORT 8090
#define SA struct sockaddr

void *receive_msg(void *args) {
    int sockfd = *(int *)args;
    char buff[MAX];
    while (1) {
        bzero(buff, MAX);
        read(sockfd, buff, sizeof(buff));
        printf("Server: %s", buff);
        if (strncmp("exit", buff, 4) == 0) {
            printf("Server disconnected...\n");
            break;
        }
    }
    return NULL;
}

void *send_msg(void *args) {
    int sockfd = *(int *)args;
    char buff[MAX];
    int n;
    for (;;) {
        bzero(buff, MAX);
        n = 0;
        while ((buff[n++] = getchar()) != '\n');
        write(sockfd, buff, sizeof(buff));
        if (strncmp("exit", buff, 4) == 0) {
            printf("Client Exit...\n");
            break;
        }
    }
    return NULL;
}

int main() {
    int sockfd;
    struct sockaddr_in servaddr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("Socket creation failed...\n");
        exit(0);
    } else
        printf("Socket successfully created..\n");
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servaddr.sin_port = htons(PORT);
    if (connect(sockfd, (SA *)&servaddr, sizeof(servaddr)) != 0) {
        printf("Connection with the server failed...\n");
        exit(0);
    } else
        printf("Connected to the server...\n");
    pthread_t send_thread, recv_thread;
    pthread_create(&send_thread, NULL, send_msg, &sockfd);
    pthread_create(&recv_thread, NULL, receive_msg, &sockfd);
    pthread_join(send_thread, NULL);
    pthread_join(recv_thread, NULL);
    close(sockfd);
    return 0;
}
