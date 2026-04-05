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
    int connfd = *(int *)args;
    char buff[MAX];
    while (1) {
        bzero(buff, MAX);
        read(connfd, buff, sizeof(buff));
        printf("Client: %s", buff);
        if (strncmp("exit", buff, 4) == 0) {
            printf("Client disconnected...\n");
            break;
        }
    }
    return NULL;
}

void *send_msg(void *args) {
    int connfd = *(int *)args;
    char buff[MAX];
    int n;
    for (;;) {
        bzero(buff, MAX);
        n = 0;
        while ((buff[n++] = getchar()) != '\n');
        write(connfd, buff, sizeof(buff));
        if (strncmp("exit", buff, 4) == 0) {
            printf("Server Exit...\n");
            break;
        }
    }
    return NULL;
}

int main() {
    int sockfd, connfd;
    struct sockaddr_in servaddr, cli;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("Socket creation failed...\n");
        exit(0);
    } else
        printf("Socket successfully created...\n");
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);
    if ((bind(sockfd, (SA *)&servaddr, sizeof(servaddr))) != 0) {
        printf("Socket bind failed...\n");
        exit(0);
    } else
        printf("Socket successfully binded...\n");
    if ((listen(sockfd, 5)) != 0) {
        printf("Listen failed...\n");
        exit(0);
    } else
        printf("Server listening...\n");
    socklen_t len = sizeof(cli);
    connfd = accept(sockfd, (SA *)&cli, &len);
    if (connfd < 0) {
        printf("Server accept failed...\n");
        exit(0);
    } else
        printf("Server accepted the client...\n");
    pthread_t send_thread, recv_thread;
    pthread_create(&send_thread, NULL, send_msg, &connfd);
    pthread_create(&recv_thread, NULL, receive_msg, &connfd);
    pthread_join(send_thread, NULL);
    pthread_join(recv_thread, NULL);
    close(sockfd);
    return 0;
}
