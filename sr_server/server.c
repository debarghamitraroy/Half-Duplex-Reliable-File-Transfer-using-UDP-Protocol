#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PACKET_SIZE 1024
#define WINDOW_SIZE 5

typedef struct {
    char filename[256];
    int seq_no;
    int ack_no;
    int is_last;
    int data_size;
    char data[PACKET_SIZE];
} Packet;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server, client;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Unable to bind");
        exit(EXIT_FAILURE);
    }
    printf("Server started. Waiting for file...\n");
    Packet pkt;
    socklen_t len = sizeof(client);
    FILE *file = NULL;
    Packet recv_window[WINDOW_SIZE];
    int received[WINDOW_SIZE] = {0};
    int base = 0;
    while (1) {
        ssize_t recv_len = recvfrom(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&client, &len);
        if (recv_len <= 0)
            continue;
        if (!file) {
            file = fopen(pkt.filename, "wb");
            if (!file) {
                perror("Unable to open the file");
                exit(EXIT_FAILURE);
            }
            char msg[256];
            snprintf(msg, sizeof(msg), "Receiving file: %s\n", pkt.filename);
            printf("%s\n", msg);
        }
        int seq = pkt.seq_no;
        sendto(sock, &seq, sizeof(seq), 0, (struct sockaddr *)&client, len);
        if (seq >= base && seq < base + WINDOW_SIZE) {
            recv_window[seq % WINDOW_SIZE] = pkt;
            received[seq % WINDOW_SIZE] = 1;
            while (received[base % WINDOW_SIZE]) {
                Packet *p = &recv_window[base % WINDOW_SIZE];
                fwrite(p->data, 1, p->data_size, file);
                received[base % WINDOW_SIZE] = 0;
                if (p->is_last) {
                    printf("File received completely.\n");
                    fclose(file);
                    close(sock);
                    return 0;
                }
                base++;
            }
        } else
            printf("Received packet outside current window, ignored.\n");
    }
    close(sock);
    return 0;
}
