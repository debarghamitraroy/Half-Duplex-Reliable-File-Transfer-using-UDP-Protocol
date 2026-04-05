// rftp_receiver.c (server)
// Usage: ./rftp_receiver <port> <save_directory>

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAXLINE 65536
#define DEFAULT_BUF_SIZE 1024
#define DEFAULT_WINDOW 8

// flags
#define FLAG_SYNC 1
#define FLAG_SYNC_ACK 2
#define FLAG_DATA 4
#define FLAG_ACK 8
#define FLAG_FIN 16
#define FLAG_ABORT 32

struct pkt_header {
    uint32_t seq;
    uint16_t len;
    uint16_t flags;
} __attribute__((packed));

static volatile int running = 1;
static int sockfd = -1;
static struct sockaddr_in peeraddr;
static socklen_t peerlen = 0;

static pthread_t recv_thread;

static void sigint_handler(int signo) {
    (void)signo;
    running = 0;
    if (sockfd >= 0)
        close(sockfd);
    printf("\n[receiver] SIGINT: shutting down gracefully.\n");
    exit(0);
}

// parse simple key=value;... string
static int parse_sync(const char *buf, char **filename, uint64_t *filesize, uint32_t *bufsize, uint16_t *win) {
    // expected "fn=...;sz=...;bs=...;win=..."
    *filename = NULL;
    char *dup = strdup(buf);
    char *saveptr = NULL;
    char *tok = strtok_r(dup, ";", &saveptr);
    while (tok) {
        if (strncmp(tok, "fn=", 3) == 0) {
            *filename = strdup(tok + 3);
        } else if (strncmp(tok, "sz=", 3) == 0) {
            *filesize = strtoull(tok + 3, NULL, 10);
        } else if (strncmp(tok, "bs=", 3) == 0) {
            *bufsize = (uint32_t)atoi(tok + 3);
        } else if (strncmp(tok, "win=", 4) == 0) {
            *win = (uint16_t)atoi(tok + 4);
        }
        tok = strtok_r(NULL, ";", &saveptr);
    }
    free(dup);
    return (*filename != NULL) ? 0 : -1;
}

static ssize_t sendto_addr(const void *buf, size_t len, int flags, const struct sockaddr *dest, socklen_t dlen) {
    return sendto(sockfd, buf, len, flags, dest, dlen);
}

static ssize_t recvfrom_addr(void *buf, size_t len, int flags, struct sockaddr *src, socklen_t *slen) {
    return recvfrom(sockfd, buf, len, flags, src, slen);
}

struct incoming_state {
    char *save_dir;
    char filename[1024];
    uint64_t filesize;
    uint32_t bufsize;
    uint16_t window;
    FILE *fp;
    uint32_t next_seq_expected; // next byte-block seq expected (cumulative)
};

static struct incoming_state ist;

static void send_sync_ack(uint32_t start_seq) {
    // Build response: "type=SYNC_ACK;start=NN"
    char out[256];
    snprintf(out, sizeof(out), "type=SYNC_ACK;start=%u", start_seq);
    sendto_addr(out, strlen(out), 0, (struct sockaddr *)&peeraddr, peerlen);
}

static void send_ack(uint32_t ack_seq) {
    struct pkt_header h;
    h.seq = htonl(ack_seq);
    h.len = htons(0);
    h.flags = htons(FLAG_ACK);
    sendto_addr(&h, sizeof(h), 0, (struct sockaddr *)&peeraddr, peerlen);
}

static void *recv_loop(void *arg) {
    (void)arg;
    unsigned char buf[MAXLINE];
    struct sockaddr_in src;
    socklen_t slen = sizeof(src);

    while (running) {
        ssize_t r = recvfrom_addr(buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
        if (r <= 0) {
            if (errno == EINTR)
                continue;
            perror("[receiver] recvfrom");
            break;
        }

        // bind peeraddr first time we see incoming
        memcpy(&peeraddr, &src, sizeof(src));
        peerlen = slen;

        // Distinguish: text sync vs binary data
        // We check if first byte is printable and contains "fn="
        if ((unsigned char)buf[0] >= 32 && memmem(buf, r, "fn=", 3) != NULL) {
            // SYNC
            char *fn = NULL;
            uint64_t sz = 0;
            uint32_t bs = DEFAULT_BUF_SIZE;
            uint16_t win = DEFAULT_WINDOW;
            char tmp[r + 1];
            memcpy(tmp, buf, r);
            tmp[r] = 0;
            if (parse_sync(tmp, &fn, &sz, &bs, &win) == 0) {
                printf("[receiver] SYNC request for '%s' size=%" PRIu64 " buf=%u win=%u\n", fn, sz, bs, win);
                // Prepare save path
                char path[2048];
                snprintf(path, sizeof(path), "%s/%s", ist.save_dir, fn);

                // ensure directories exist for nested names
                // simple approach: create directories in path
                char *p = strdup(path);
                for (char *q = p + 1; *q; q++) {
                    if (*q == '/') {
                        *q = 0;
                        mkdir(p, 0755);
                        *q = '/';
                    }
                }
                free(p);

                // check existing file size to determine resume point
                uint64_t existing = 0;
                struct stat st;
                if (stat(path, &st) == 0) {
                    existing = st.st_size;
                } else {
                    // file doesn't exist: ensure parent directory exists
                    // will create later when opening
                }

                // compute start_seq (packet index) to request
                uint32_t start_seq = (uint32_t)(existing / bs);
                // store incoming state
                strncpy(ist.filename, fn, sizeof(ist.filename) - 1);
                ist.filesize = sz;
                ist.bufsize = bs;
                ist.window = win;
                ist.next_seq_expected = start_seq; // next seq write index expected for cumulative ack

                // Open file for update (binary)
                // Use O_CREAT|O_RDWR
                char tmpdir[2048];
                snprintf(tmpdir, sizeof(tmpdir), "%s/%s", ist.save_dir, fn);
                int fd = open(tmpdir, O_CREAT | O_RDWR, 0644);
                if (fd < 0) {
                    perror("[receiver] open");
                    // respond with start_seq 0 but indicate error via negative? we'll
                    // just send start_seq 0
                    start_seq = 0;
                } else {
                    // set file size at least existing
                    lseek(fd, 0, SEEK_SET);
                    // no truncation
                    close(fd);
                }

                // reply with SYNC_ACK with start seq
                send_sync_ack(start_seq);
                free(fn);
            } else {
                fprintf(stderr, "[receiver] bad sync packet\n");
            }
            continue;
        }

        // else it's binary with header
        if (r >= (ssize_t)sizeof(struct pkt_header)) {
            struct pkt_header h;
            memcpy(&h, buf, sizeof(h));
            uint32_t seq = ntohl(h.seq);
            uint16_t len = ntohs(h.len);
            uint16_t flags = ntohs(h.flags);
            unsigned char *payload = buf + sizeof(h);

            if (flags & FLAG_DATA) {
                // compute file path
                char path[2048];
                snprintf(path, sizeof(path), "%s/%s", ist.save_dir, ist.filename);
                // ensure file exists
                int fd = open(path, O_CREAT | O_RDWR, 0644);
                if (fd < 0) {
                    perror("[receiver] open data file");
                    continue;
                }
                // write at offset = seq * bufsize
                off_t offset = (off_t)seq * ist.bufsize;
                if (lseek(fd, offset, SEEK_SET) < 0) {
                    perror("[receiver] lseek");
                    close(fd);
                    continue;
                }
                ssize_t w = write(fd, payload, len);
                if (w < 0) {
                    perror("[receiver] write");
                    close(fd);
                    continue;
                }
                fsync(fd);
                close(fd);

                // if this was the next expected seq, advance next_seq_expected until
                // gap
                if (seq == ist.next_seq_expected) {
                    // increase next_seq_expected until we find missing (quick method:
                    // check file size)
                    struct stat st;
                    if (stat(path, &st) == 0) {
                        uint64_t full_bytes = st.st_size;
                        ist.next_seq_expected = (uint32_t)(full_bytes / ist.bufsize);
                    } else {
                        ist.next_seq_expected++;
                    }
                }
                // send ACK of cumulative next_seq_expected (meaning all <
                // next_seq_expected are received)
                send_ack(ist.next_seq_expected);
                // print progress
                uint64_t written_bytes = ((uint64_t)ist.next_seq_expected) * ist.bufsize;
                if (written_bytes > ist.filesize)
                    written_bytes = ist.filesize;
                printf("[receiver] wrote seq=%u len=%u; progress: %" PRIu64 "/%" PRIu64 "\n", seq, len, written_bytes, ist.filesize);
            } else if (flags & FLAG_FIN) {
                printf("[receiver] FIN received from sender. Closing file transfers.\n");
                // send FIN ACK then break if needed
                send_ack(0);
                // continue running to accept more transfers
            } else if (flags & FLAG_ABORT) {
                printf("[receiver] sender aborted transfer.\n");
            } else if (flags & FLAG_ACK) {
                // unlikely receiver sees ack
            }
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <save_dir>\n", argv[0]);
        return 1;
    }
    signal(SIGINT, sigint_handler);

    int port = atoi(argv[1]);
    char *save_dir = argv[2];
    ist.save_dir = strdup(save_dir);

    // create save directory if not exists
    mkdir(save_dir, 0755);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    printf("[receiver] Listening on port %d, saving to '%s'\n", port, save_dir);

    // spawn recv thread
    if (pthread_create(&recv_thread, NULL, recv_loop, NULL) != 0) {
        perror("pthread_create");
        close(sockfd);
        return 1;
    }

    // main thread just waits (receiver does the work)
    while (running) {
        sleep(1);
    }

    pthread_join(recv_thread, NULL);
    close(sockfd);
    return 0;
}
