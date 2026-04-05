// rftp_sender.c (client)
// Usage: ./rftp_sender <receiver_ip> <port> <send_folder>

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
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAXLINE 65536
#define DEFAULT_BUF_SIZE 1024
#define DEFAULT_WINDOW 8
#define DEFAULT_TIMEOUT_MS 300 // retransmit timeout ms
#define DEFAULT_K 8 // max retransmissions per packet

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
static struct sockaddr_in servaddr;
static socklen_t servlen;

static uint32_t window_size = DEFAULT_WINDOW;
static uint32_t buf_size = DEFAULT_BUF_SIZE;
static uint32_t timeout_ms = DEFAULT_TIMEOUT_MS;
static int K_attempts = DEFAULT_K;

struct send_packet {
    uint32_t seq;
    uint16_t len;
    uint16_t flags;
    unsigned char *data;
    int sent; // 0 not yet sent, 1 sent
    int acked; // 0/1
    int attempts;
    long long last_sent_ts_ms;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_ack = PTHREAD_COND_INITIALIZER;

static uint32_t base_seq = 0; // next seq expected ack
static uint32_t next_seq = 0; // next seq to send
static struct send_packet *window = NULL;
static size_t window_capacity = 0;

static uint32_t total_packets = 0;
static struct send_packet *all_packets = NULL;

static void msleep(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

static long long now_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void sigint_handler(int signo) {
    (void)signo;
    running = 0;
    if (sockfd >= 0)
        close(sockfd);
    printf("\n[sender] SIGINT: aborting and shutting down.\n");
    exit(0);
}

static ssize_t sendto_serv(const void *buf, size_t len, int flags) {
    return sendto(sockfd, buf, len, flags, (struct sockaddr *)&servaddr, servlen);
}

static ssize_t recvfrom_serv(void *buf, size_t len, int flags) {
    struct sockaddr_in src;
    socklen_t sl = sizeof(src);
    ssize_t r = recvfrom(sockfd, buf, len, flags, (struct sockaddr *)&src, &sl);
    (void)src;
    return r;
}

// receiver thread: handles incoming ACKs and SYNC_ACK
static void *recv_thread_fn(void *arg) {
    (void)arg;
    unsigned char buf[MAXLINE];
    while (running) {
        ssize_t r = recvfrom_serv(buf, sizeof(buf), 0);
        if (r <= 0) {
            if (errno == EINTR)
                continue;
            perror("[sender] recvfrom");
            break;
        }
        // if textual with "type=SYNC_ACK" parse it
        if ((unsigned char)buf[0] >= 32 && memmem(buf, r, "type=SYNC_ACK", 12) != NULL) {
            // parse start=NN
            char tmp[r + 1];
            memcpy(tmp, buf, r);
            tmp[r] = 0;
            char *p = strstr(tmp, "start=");
            if (p) {
                uint32_t start = (uint32_t)atoi(p + 6);
                // communicate via condition variable or global
                pthread_mutex_lock(&lock);
                // base_seq and next_seq will be set by main sync logic
                base_seq = start;
                next_seq = start;
                pthread_cond_signal(&cond_ack);
                pthread_mutex_unlock(&lock);
            }
            continue;
        }
        // else could be an ACK header
        if (r >= (ssize_t)sizeof(struct pkt_header)) {
            struct pkt_header h;
            memcpy(&h, buf, sizeof(h));
            uint32_t ack_seq = ntohl(h.seq);
            uint16_t flags = ntohs(h.flags);
            if (flags & FLAG_ACK) {
                pthread_mutex_lock(&lock);
                // cumulative ack: everything < ack_seq is acknowledged
                if (ack_seq > base_seq) {
                    // mark packets acked
                    for (uint32_t s = base_seq; s < ack_seq && s < total_packets; ++s) {
                        if (!all_packets[s].acked) {
                            all_packets[s].acked = 1;
                        }
                    }
                    base_seq = ack_seq;
                    pthread_cond_signal(&cond_ack);
                }
                pthread_mutex_unlock(&lock);
            }
        }
    }
    return NULL;
}

// prepare packets in memory for a file segment
static int prepare_file_packets(const char *filepath, const char *relname,
                                uint64_t filesize, uint32_t bufsize,
                                uint32_t *out_total_packets) {
    // compute total packets
    uint32_t tpk = (uint32_t)((filesize + bufsize - 1) / bufsize);
    *out_total_packets = tpk;
    all_packets = calloc(tpk, sizeof(struct send_packet));
    if (!all_packets)
        return -1;
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("[sender] open file");
        return -1;
    }
    for (uint32_t i = 0; i < tpk; ++i) {
        off_t off = (off_t)i * bufsize;
        size_t need = bufsize;
        if ((uint64_t)off + need > filesize)
            need = (size_t)(filesize - off);
        unsigned char *buf = malloc(need);
        if (!buf) {
            perror("malloc");
            close(fd);
            return -1;
        }
        if (lseek(fd, off, SEEK_SET) < 0)
            perror("lseek");
        ssize_t r = read(fd, buf, need);
        if (r < 0) {
            perror("read");
            free(buf);
            close(fd);
            return -1;
        }
        all_packets[i].seq = i;
        all_packets[i].len = (uint16_t)r;
        all_packets[i].flags = FLAG_DATA;
        all_packets[i].data = buf;
        all_packets[i].sent = 0;
        all_packets[i].acked = 0;
        all_packets[i].attempts = 0;
        all_packets[i].last_sent_ts_ms = 0;
    }
    close(fd);
    return 0;
}

static void free_packets(uint32_t tpk) {
    if (!all_packets)
        return;
    for (uint32_t i = 0; i < tpk; ++i) {
        if (all_packets[i].data)
            free(all_packets[i].data);
    }
    free(all_packets);
    all_packets = NULL;
}

// main send logic for a file
static int send_file(const char *filepath, const char *relname,
                     uint32_t bufsize, uint32_t win) {
    // get file size
    struct stat st;
    if (stat(filepath, &st) < 0) {
        perror("stat");
        return -1;
    }
    uint64_t filesize = st.st_size;
    printf("[sender] Sending '%s' size=%" PRIu64 " buf=%u win=%u\n", relname,
           filesize, bufsize, win);

    // prepare sync packet: "fn=...;sz=...;bs=...;win=..."
    char sync[2048];
    snprintf(sync, sizeof(sync), "fn=%s;sz=%" PRIu64 ";bs=%u;win=%u", relname,
             filesize, bufsize, win);
    sendto_serv(sync, strlen(sync), 0);

    // wait for SYNC_ACK from receiver which sets base_seq via recv_thread
    pthread_mutex_lock(&lock);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5; // 5s wait for sync_ack
    int ret = pthread_cond_timedwait(&cond_ack, &lock, &ts);
    if (ret != 0) {
        fprintf(stderr, "[sender] no SYNC_ACK received, aborting file %s\n",
                relname);
        pthread_mutex_unlock(&lock);
        return -1;
    }
    // base_seq and next_seq are set by recv thread
    uint32_t start_seq = base_seq;
    pthread_mutex_unlock(&lock);

    // prepare packets
    uint32_t tpk = 0;
    if (prepare_file_packets(filepath, relname, filesize, bufsize, &tpk) < 0) {
        fprintf(stderr, "[sender] failed to buffer file\n");
        return -1;
    }
    total_packets = tpk;

    // mark already acknowledged packets < start_seq as acked to resume
    for (uint32_t i = 0; i < start_seq && i < tpk; ++i) {
        all_packets[i].acked = 1;
    }
    base_seq = start_seq;
    next_seq = start_seq;

    // create window
    window_capacity = win;
    // start a local send loop with timeout/retransmit
    long long start_ts = now_ms();

    while (base_seq < tpk && running) {
        pthread_mutex_lock(&lock);
        // send any packets in window that are unsent and within window
        while (next_seq < tpk && next_seq < base_seq + win) {
            if (!all_packets[next_seq].sent) {
                // build packet
                struct pkt_header h;
                h.seq = htonl(all_packets[next_seq].seq);
                h.len = htons(all_packets[next_seq].len);
                h.flags = htons(all_packets[next_seq].flags);
                // assemble buffer
                size_t bufsz = sizeof(h) + all_packets[next_seq].len;
                unsigned char *buf = malloc(bufsz);
                memcpy(buf, &h, sizeof(h));
                memcpy(buf + sizeof(h), all_packets[next_seq].data,
                       all_packets[next_seq].len);
                ssize_t s = sendto_serv(buf, bufsz, 0);
                free(buf);
                if (s < 0) {
                    perror("[sender] sendto");
                } else {
                    all_packets[next_seq].sent = 1;
                    all_packets[next_seq].attempts++;
                    all_packets[next_seq].last_sent_ts_ms = now_ms();
                    // printf("[sender] sent seq=%u len=%u\n", all_packets[next_seq].seq,
                    // all_packets[next_seq].len);
                }
            }
            next_seq++;
        }

        // check for timeouts and retransmit
        long long cur = now_ms();
        for (uint32_t s = base_seq; s < tpk && s < base_seq + win; ++s) {
            if (all_packets[s].acked)
                continue;
            if (all_packets[s].sent) {
                if (cur - all_packets[s].last_sent_ts_ms >= timeout_ms) {
                    if (all_packets[s].attempts >= K_attempts) {
                        fprintf(stderr,
                                "[sender] packet %u exceeded K attempts. Aborting file.\n",
                                s);
                        // send abort to receiver
                        struct pkt_header h;
                        h.seq = htonl(0);
                        h.len = htons(0);
                        h.flags = htons(FLAG_ABORT);
                        sendto_serv(&h, sizeof(h), 0);
                        pthread_mutex_unlock(&lock);
                        free_packets(tpk);
                        return -1;
                    }
                    // retransmit
                    struct pkt_header h;
                    h.seq = htonl(all_packets[s].seq);
                    h.len = htons(all_packets[s].len);
                    h.flags = htons(all_packets[s].flags);
                    size_t bufsz = sizeof(h) + all_packets[s].len;
                    unsigned char *buf = malloc(bufsz);
                    memcpy(buf, &h, sizeof(h));
                    memcpy(buf + sizeof(h), all_packets[s].data, all_packets[s].len);
                    ssize_t ss = sendto_serv(buf, bufsz, 0);
                    free(buf);
                    if (ss >= 0) {
                        all_packets[s].attempts++;
                        all_packets[s].last_sent_ts_ms = now_ms();
                        printf("[sender] retransmitted seq=%u attempt=%d\n", s,
                               all_packets[s].attempts);
                    }
                }
            } else {
                // hasn't been sent yet and within window -- it'll be sent by above loop
                // next iteration
            }
        }

        // advance base_seq if acked (recv thread updates base_seq)
        // Wait for either ack or timeout
        struct timespec waitts;
        clock_gettime(CLOCK_REALTIME, &waitts);
        waitts.tv_sec += 0;
        waitts.tv_nsec += 100 * 1000000; // 100ms
        if (waitts.tv_nsec >= 1000000000) {
            waitts.tv_sec += 1;
            waitts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&cond_ack, &lock, &waitts);

        // find new base_seq by checking acked flags
        uint32_t newbase = base_seq;
        while (newbase < tpk && all_packets[newbase].acked)
            newbase++;
        if (newbase != base_seq) {
            base_seq = newbase;
            // optionally print progress
            uint64_t bytes_sent = (uint64_t)base_seq * bufsize;
            if (bytes_sent > filesize)
                bytes_sent = filesize;
            printf("[sender] progress: %" PRIu64 "/%" PRIu64 " packets acked=%u/%u\n",
                   bytes_sent, filesize, base_seq, tpk);
        }
        pthread_mutex_unlock(&lock);
    }

    // send FIN for file completion
    struct pkt_header hf;
    hf.seq = htonl(0);
    hf.len = htons(0);
    hf.flags = htons(FLAG_FIN);
    sendto_serv(&hf, sizeof(hf), 0);

    long long elapsed = now_ms() - start_ts;
    printf("[sender] file '%s' done in %lld ms\n", relname, elapsed);

    free_packets(tpk);
    return 0;
}

static int send_folder(const char *folder) {
    DIR *d = opendir(folder);
    if (!d) {
        perror("opendir");
        return -1;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && running) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", folder, de->d_name);
        struct stat st;
        if (stat(path, &st) < 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            // recursively send subdirectory by walking deeper (simple recursion)
            send_folder(path);
        } else if (S_ISREG(st.st_mode)) {
            // relative name is path without leading folder prefix
            // build relname by removing initial folder + '/'
            const char *rel = path + strlen(folder) + 1;
            if (rel < path)
                rel = de->d_name;
            int r = send_file(path, rel, buf_size, window_size);
            if (r < 0) {
                fprintf(stderr, "[sender] Failed to send file '%s'\n", path);
            }
        }
    }
    closedir(d);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <send_folder>\n", argv[0]);
        return 1;
    }
    signal(SIGINT, sigint_handler);

    char *server_ip = argv[1];
    int port = atoi(argv[2]);
    char *send_folder_path = argv[3];

    // create socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &servaddr.sin_addr) != 1) {
        struct hostent *he = gethostbyname(server_ip);
        if (!he) {
            perror("gethostbyname");
            return 1;
        }
        memcpy(&servaddr.sin_addr, he->h_addr_list[0], he->h_length);
    }
    servlen = sizeof(servaddr);

    // start recv thread
    pthread_t rthr;
    if (pthread_create(&rthr, NULL, recv_thread_fn, NULL) != 0) {
        perror("pthread_create");
        close(sockfd);
        return 1;
    }

    // walk folder and send files
    send_folder(send_folder_path);

    // send final FIN to tell receiver we're done
    struct pkt_header h;
    h.seq = htonl(0);
    h.len = htons(0);
    h.flags = htons(FLAG_FIN);
    sendto_serv(&h, sizeof(h), 0);

    running = 0;
    pthread_join(rthr, NULL);
    close(sockfd);
    return 0;
}
