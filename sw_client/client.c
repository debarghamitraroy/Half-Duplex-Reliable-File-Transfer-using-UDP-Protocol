#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT 8080
#define MAXLINE 1024

void log_event(const char *format, ...) {
    FILE *log = fopen("client_log.log", "a");
    if (!log)
        return;
    time_t now = time(NULL);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(log, "[%s] ", timestr);
    va_list args;
    va_start(args, format);
    vfprintf(log, format, args);
    va_end(args);
    fprintf(log, "\n");
    fclose(log);
}

int main() {
    printf("Client has been started\n");
    int sockfd;
    char buffer[MAXLINE];
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len;
    FILE *fp;
    int totalPackets = 0, packetsLost = 0, connections = 0;
    time_t start_time = time(NULL);
    log_event("Receiver has been started");
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        printf("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Bind failed");
        printf("Bind failed\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    connections++;
    char filename[256];
    printf("Enter the file name (eg: received_test.txt): ");
    scanf("%255s", filename);
    fp = fopen(filename, "wb");
    if (fp == NULL) {
        perror("File creation failed");
        printf("File creation failed\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    len = sizeof(cliaddr);
    while (1) {
        int n = recvfrom(sockfd, buffer, MAXLINE, 0, (struct sockaddr *)&cliaddr, &len);
        if (n < 0)
            continue;
        buffer[n] = '\0';
        if (strcmp(buffer, "END") == 0)
            break;
        fwrite(buffer, 1, n, fp);
        printf("Sending ACK: %d\n", totalPackets);
        totalPackets++;
        sendto(sockfd, "ACK", 3, MSG_CONFIRM, (const struct sockaddr *)&cliaddr, len);
    }
    time_t end_time = time(NULL);
    double elapsed = difftime(end_time, start_time);
    log_event("Packets received: %d", totalPackets);
    log_event("Packets lost (approx): %d", packetsLost);
    log_event("Connections made: %d", connections);
    log_event("Start time: %s", ctime(&start_time));
    log_event("End time: %s", ctime(&end_time));
    log_event("Total time taken: %.2f seconds", elapsed);
    log_event("Receiving complete.");
    printf("client has been closed\n");
    fclose(fp);
    close(sockfd);
    return 0;
}
