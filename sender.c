#include "common.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <receiver_ip> <port> <file_path> [loss_prob%%]\n", prog);
    fprintf(stderr, "Example: %s 127.0.0.1 8080 test.txt 0\n", prog);
}

static double parse_loss_prob(const char *s) {
    char *end = NULL;
    double p = strtod(s, &end);
    if (!end || *end != '\0' || p < 0.0 || p > 100.0)
        return 0.0;
    return p / 100.0;
}

static int maybe_drop(double p) {
    if (p <= 0.0)
        return 0;
    return ((double)rand() / (double)RAND_MAX) < p;
}

int main(int argc, char **argv) {
    if (argc < 4 || argc > 5) {
        usage(argv[0]);
        return 1;
    }

    const char *dst_ip = argv[1];
    int dst_port = atoi(argv[2]);
    const char *file_path = argv[3];
    double loss_prob = (argc == 5) ? parse_loss_prob(argv[4]) : 0.0;
    srand((unsigned)time(NULL));
    const char *slash = strrchr(file_path, '/');
    const char *fname = slash ? slash + 1 : file_path;
    if (strlen(fname) > FILENAME_MAXLEN) {
        fprintf(stderr, "File name too long (max %d)\n", FILENAME_MAXLEN);
        return 1;
    }
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        perror("Unable to open the file");
        return 1;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        fclose(fp);
        return 1;
    }
    struct timeval tv;
    tv.tv_sec = DEFAULT_TIMEOUT_MS / 1000;
    tv.tv_usec = (DEFAULT_TIMEOUT_MS % 1000) * 1000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        perror("setsockopt SO_RCVTIMEO error");
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)dst_port);
    if (inet_pton(AF_INET, dst_ip, &dst.sin_addr) != 1) {
        fprintf(stderr, "Invalid IP: %s\n", dst_ip);
        fclose(fp);
        close(sock);
        return 1;
    }
    uint8_t buffer[MAX_PACKET_SIZE];
    uint32_t seq = 0;
    size_t nread;
    while ((nread = fread(buffer + sizeof(struct PacketHeader), 1, SEGMENT_SIZE, fp)) > 0) {
        struct PacketHeader *hdr = (struct PacketHeader *)buffer;
        memset(hdr, 0, sizeof(*hdr));
        hdr->seq = htonl(seq);
        hdr->data_len = htons((uint16_t)nread);
        hdr->eof = 0;
        strncpy(hdr->file, fname, FILENAME_MAXLEN);
        hdr->checksum = 0;
        hdr->checksum = htonl(csum32((uint8_t *)hdr, sizeof(*hdr)) + csum32(buffer + sizeof(*hdr), (uint32_t)nread));
        ssize_t pkt_len = sizeof(*hdr) + (ssize_t)nread;
        for (;;) {
            if (!maybe_drop(loss_prob)) {
                if (sendto(sock, buffer, pkt_len, 0, (struct sockaddr *)&dst, sizeof(dst)) != pkt_len) {
                    perror("sendto");
                } else {
                    printf("Sent seq=%u len=%zu\n", seq, nread);
                }
            } else
                printf("DROP (sim) seq=%u\n", seq);
            struct AckPacket ack;
            socklen_t slen = sizeof(dst);
            ssize_t rc = recvfrom(sock, &ack, sizeof(ack), 0, (struct sockaddr *)&dst, &slen);
            if (rc < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    printf("Timeout waiting for ACK %u, retransmitting...\n", seq);
                    continue;
                } else {
                    perror("recvfrom error");
                    continue;
                }
            }
            if ((size_t)rc != sizeof(ack)) {
                printf("Short ACK received (%zd), ignoring\n", rc);
                continue;
            }
            uint32_t saved = ack.checksum;
            ack.checksum = 0;
            uint32_t chk = csum32((uint8_t *)&ack, sizeof(ack));
            if (chk != saved) {
                printf("Bad ACK checksum, retransmitting seq=%u\n", seq);
                continue;
            }
            uint32_t ack_seq = ntohl(ack.ack_seq);
            if (ack_seq == seq) {
                printf("ACK %u OK\n", seq);
                seq++;
                break;
            } else
                printf("Unexpected ACK %u (wanted %u), retransmitting...\n", ack_seq, seq);
        }
    }
    {
        struct PacketHeader hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.seq = htonl(seq);
        hdr.data_len = htons(0);
        hdr.eof = 1;
        strncpy(hdr.file, fname, FILENAME_MAXLEN);
        hdr.checksum = 0;
        hdr.checksum = htonl(csum32((uint8_t *)&hdr, sizeof(hdr)));
        for (;;) {
            if (!maybe_drop(loss_prob)) {
                if (sendto(sock, &hdr, sizeof(hdr), 0, (struct sockaddr *)&dst, sizeof(dst)) != sizeof(hdr))
                    perror("sendto EOF error");
                else
                    printf("Sent EOF seq=%u\n", seq);
            } else
                printf("DROP (sim) EOF seq=%u\n", seq);
            struct AckPacket ack;
            socklen_t slen = sizeof(dst);
            ssize_t rc = recvfrom(sock, &ack, sizeof(ack), 0, (struct sockaddr *)&dst, &slen);
            if (rc < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    printf("Timeout waiting for EOF-ACK, retransmitting EOF...\n");
                    continue;
                } else {
                    perror("recvfrom EOF error");
                    continue;
                }
            }
            if ((size_t)rc == sizeof(ack)) {
                uint32_t saved = ack.checksum;
                ack.checksum = 0;
                uint32_t chk = csum32((uint8_t *)&ack, sizeof(ack));
                if (chk == saved && ntohl(ack.ack_seq) == seq) {
                    printf("EOF ACK received. Done.\n");
                    break;
                }
            }
            printf("Bad EOF ACK, retrying...\n");
        }
    }
    fclose(fp);
    close(sock);
    return 0;
}
