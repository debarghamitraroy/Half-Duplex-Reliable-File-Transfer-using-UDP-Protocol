#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define PORT 8080
#define MAXLINE 1024
#define TIMEOUT_SEC 2

void log_event(const char *format, ...) {
    FILE *log = fopen("server_log.log", "a");
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
    printf("Server has been started\n");
    int sockfd;
    struct sockaddr_in servaddr;
    char buffer[MAXLINE];
    FILE *fp;
    struct timeval timeout = {TIMEOUT_SEC, 0};
    int totalPackets = 0, packetsLost = 0, timeouts = 0, connections = 0;
    ssize_t n;
    socklen_t len;
    long totalBytes = 0, fileSize = 0;
    time_t start_time = time(NULL);
    log_event("Transmission started");
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = INADDR_ANY;
    connections++;
    char filename[256];
    printf("Enter the file name (eg: test.txt): ");
    scanf("%255s", filename);
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("File open error");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    while (!feof(fp)) {
        int bytesRead = fread(buffer, 1, MAXLINE, fp);
        printf("Sending packet of size: %d\n", bytesRead);
        if (bytesRead <= 0)
            break;
        sendto(sockfd, buffer, bytesRead, MSG_CONFIRM, (const struct sockaddr *)&servaddr, sizeof(servaddr));
        printf("Sending SEQ: %d\n", totalPackets);
        totalPackets++;
        totalBytes += bytesRead;
        len = sizeof(servaddr);
        n = recvfrom(sockfd, buffer, MAXLINE, 0, (struct sockaddr *)&servaddr, &len);
        if (n < 0) {
            timeouts++;
            packetsLost++;
            log_event("Timeout occurred (packet %d). Retrying...", totalPackets);
            printf("Timeout occurred (packet %d). Retrying...", totalPackets);
            fseek(fp, -bytesRead, SEEK_CUR);
            continue;
        }
    }
    strcpy(buffer, "END");
    sendto(sockfd, buffer, strlen(buffer), MSG_CONFIRM, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    time_t end_time = time(NULL);
    double elapsed = difftime(end_time, start_time);
    double percentSent = ((double)totalBytes / (double)fileSize) * 100.0;
    log_event("Transmission completed");
    log_event("File size: %ld bytes", fileSize);
    log_event("Bytes sent: %ld (%.2f%%)", totalBytes, percentSent);
    log_event("Packets sent: %d", totalPackets);
    log_event("Packets lost: %d", packetsLost);
    log_event("Timeouts: %d", timeouts);
    log_event("Connections made: %d", connections);
    log_event("Start time: %s", ctime(&start_time));
    log_event("End time: %s", ctime(&end_time));
    log_event("Total time taken: %.2f seconds", elapsed);
    log_event("Sending complete\n");
    printf("Server has been closed\n");
    fclose(fp);
    close(sockfd);
    return 0;
}
