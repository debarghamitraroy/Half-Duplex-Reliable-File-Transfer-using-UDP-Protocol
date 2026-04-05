#include "common.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <listen_ip> <port> [output_dir] [loss_prob%%]\n", prog);
    fprintf(stderr, "Example: %s 0.0.0.0 8080 ./output 0\n", prog);
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

static void make_out_path(char *dst, size_t dstlen, const char *outdir, const char *fname) {
    char safe_name[FILENAME_MAXLEN + 1];
    strncpy(safe_name, fname, FILENAME_MAXLEN);
    safe_name[FILENAME_MAXLEN] = '\0';
    if (!outdir || strcmp(outdir, ".") == 0) {
        snprintf(dst, dstlen, "received_%s", safe_name);
    } else {
        size_t l = strlen(outdir);
        if (l > 0 && outdir[l - 1] == '/')
            snprintf(dst, dstlen, "%sreceived_%s", outdir, safe_name);
        else
            snprintf(dst, dstlen, "%s/received_%s", outdir, safe_name);
    }
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 5) {
        usage(argv[0]);
        return 1;
    }
    const char *listen_ip = argv[1];
    int listen_port = atoi(argv[2]);
    const char *outdir = (argc >= 4) ? argv[3] : ".";
    double loss_prob = (argc == 5) ? parse_loss_prob(argv[4]) : 0.0;
    srand((unsigned)time(NULL));
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)listen_port);
    if (inet_pton(AF_INET, listen_ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IP: %s\n", listen_ip);
        close(sock);
        return 1;
    }
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Unable to bind");
        close(sock);
        return 1;
    }
    printf("Receiver listening on %s:%d\n", listen_ip, listen_port);
    uint32_t expected_seq = 0;
    FILE *fp = NULL;
    char out_path[1024] = {0};
    uint8_t pktbuf[MAX_PACKET_SIZE];
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    for (;;) {
        ssize_t rc = recvfrom(sock, pktbuf, sizeof(pktbuf), 0, (struct sockaddr *)&sender, &sender_len);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            perror("recvfrom error");
            break;
        }
        if ((size_t)rc < sizeof(struct PacketHeader)) {
            printf("Short packet (%zd), ignoring\n", rc);
            continue;
        }
        struct PacketHeader *hdr = (struct PacketHeader *)pktbuf;
        uint32_t saved = hdr->checksum;
        hdr->checksum = 0;
        uint32_t calc = csum32((uint8_t *)hdr, sizeof(*hdr));
        uint16_t data_len = ntohs(hdr->data_len);
        if (sizeof(*hdr) + data_len != (size_t)rc) {
            printf("Length mismatch (hdr says %u, got %zd), ignoring\n", data_len, rc - (ssize_t)sizeof(*hdr));
            continue;
        }
        calc += csum32(pktbuf + sizeof(*hdr), data_len);
        if (htonl(calc) != saved) {
            printf("Bad checksum for incoming packet, ignoring\n");
            continue;
        }
        uint32_t seq = ntohl(hdr->seq);
        struct AckPacket ack;
        ack.ack_seq = htonl(seq);
        ack.checksum = 0;
        ack.checksum = csum32((uint8_t *)&ack, sizeof(ack));
        if (!maybe_drop(loss_prob)) {
            sendto(sock, &ack, sizeof(ack), 0, (struct sockaddr *)&sender, sender_len);
            printf("Sent ACK %u\n", seq);
        } else
            printf("DROP (sim) ACK %u\n", seq);
        if (!fp) {
            make_out_path(out_path, sizeof(out_path), outdir, hdr->file);
            fp = fopen(out_path, "wb");
            if (!fp) {
                perror("Unable to open output folder");
                break;
            }
            printf("Writing to %s\n", out_path);
        }
        if (hdr->eof) {
            printf("EOF packet received (seq=%u). Closing file.\n", seq);
            if (fp) {
                fclose(fp);
                fp = NULL;
            }
            expected_seq = 0;
            memset(out_path, 0, sizeof(out_path));
            break;
        }
        if (seq == expected_seq) {
            size_t wrote = fwrite(pktbuf + sizeof(*hdr), 1, data_len, fp);
            if (wrote != data_len) {
                perror("fwrite error");
                break;
            }
            printf("Received seq=%u (%u bytes)\n", seq, data_len);
            expected_seq++;
        } else {
            printf("Out-of-order/duplicate packet seq=%u (expected %u). Ignored.\n", seq, expected_seq);
        }
    }
    if (fp)
        fclose(fp);
    close(sock);
    return 0;
}
