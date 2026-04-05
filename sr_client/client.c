#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define PACKET_SIZE 1024
#define WINDOW_SIZE 5
#define TIMEOUT_SEC 2
#define MAX_PACKETS 10000

typedef struct {
    char filename[256];
    int seq_no;
    int ack_no;
    int is_last;
    int data_size;
    char data[PACKET_SIZE];
} Packet;

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <server_ip> <server_port> <file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char *server_ip = argv[1];
    int port = atoi(argv[2]);
    char *filename = argv[3];
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &server.sin_addr);
    socklen_t len = sizeof(server);
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Unable to open the file");
        exit(EXIT_FAILURE);
    }
    printf("Transmission started...\n");
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);
    int total_packets = (filesize + PACKET_SIZE - 1) / PACKET_SIZE;
    Packet window[WINDOW_SIZE];
    int acked[WINDOW_SIZE] = {0};
    int base = 0;
    int next_seq = 0;
    while (base < total_packets) {
        while (next_seq < base + WINDOW_SIZE && next_seq < total_packets) {
            Packet *p = &window[next_seq % WINDOW_SIZE];
            p->seq_no = next_seq;
            strcpy(p->filename, filename);
            p->data_size = fread(p->data, 1, PACKET_SIZE, file);
            p->is_last = (next_seq == total_packets - 1);
            sendto(sock, p, sizeof(Packet), 0, (struct sockaddr *)&server, len);
            char msg[128];
            sprintf(msg, "Sent packet %d", p->seq_no);
            printf("%s\n", msg);
            next_seq++;
        }
        struct timeval tv = {TIMEOUT_SEC, 0};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        int activity = select(sock + 1, &fds, NULL, NULL, &tv);
        if (activity > 0) {
            int ack_no;
            recvfrom(sock, &ack_no, sizeof(ack_no), 0, (struct sockaddr *)&server, &len);
            if (ack_no >= base && ack_no < base + WINDOW_SIZE) {
                acked[ack_no % WINDOW_SIZE] = 1;
                while (acked[base % WINDOW_SIZE] && base < total_packets) {
                    acked[base % WINDOW_SIZE] = 0;
                    base++;
                }
                char msg[128];
                sprintf(msg, "ACK received for packet %d\n", ack_no);
                printf("%s\n", msg);
            }
        } else {
            printf("Timeout occurred. Retransmitting missing packets...\n");
            for (int i = base; i < next_seq; i++) {
                if (!acked[i % WINDOW_SIZE]) {
                    sendto(sock, &window[i % WINDOW_SIZE], sizeof(Packet), 0, (struct sockaddr *)&server, len);
                    char msg[128];
                    sprintf(msg, "Resent packet %d\n", window[i % WINDOW_SIZE].seq_no);
                    printf("%s\n", msg);
                }
            }
        }
    }
    printf("Transmission completed successfully.\n");
    fclose(file);
    close(sock);
    return 0;
}
