#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef SO_RCVBUFFORCE
#define SO_RCVBUFFORCE 33
#endif

#define MAX_PACKET 65536

static volatile sig_atomic_t g_stop = 0;

struct config {
    char group[64];
    int port;
    char iface_ip[64];
    char ifname[IFNAMSIZ];
    char bind_ip[64];
    int bind_port;
    int seconds;
    int max_rows;
    int rcvbuf_mb;
    char csv_path[256];
};

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static uint64_t realtime_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint32_t le32(const uint8_t *p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p)
{
    return ((uint64_t)le32(p)) | ((uint64_t)le32(p + 4) << 32);
}

static void copy_ascii(char *dst, size_t dst_len, const uint8_t *src, size_t src_len)
{
    size_t j = 0;
    for (size_t i = 0; i < src_len && j + 1 < dst_len; ++i) {
        unsigned char c = src[i];
        if (c >= 32 && c <= 126) {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
}

static int set_socket_buffers(int fd, int mb)
{
    if (mb <= 0) {
        return 0;
    }
    int bytes = mb * 1024 * 1024;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &bytes, sizeof(bytes)) == 0) {
        return 0;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == 0) {
        return 0;
    }
    return -1;
}

static int open_multicast_socket(const struct config *cfg)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif

    if (cfg->ifname[0]) {
        if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
                       cfg->ifname, strlen(cfg->ifname) + 1) != 0) {
            perror("SO_BINDTODEVICE");
            close(fd);
            return -1;
        }
    }

    if (set_socket_buffers(fd, cfg->rcvbuf_mb) != 0) {
        perror("SO_RCVBUF/SO_RCVBUFFORCE");
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons((uint16_t)(cfg->bind_port > 0 ? cfg->bind_port : cfg->port));
    if (inet_pton(AF_INET, cfg->bind_ip, &bind_addr.sin_addr) != 1) {
        fprintf(stderr, "bad bind ip: %s\n", cfg->bind_ip);
        close(fd);
        return -1;
    }
    if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    struct ip_mreqn mreq;
    memset(&mreq, 0, sizeof(mreq));
    if (inet_pton(AF_INET, cfg->group, &mreq.imr_multiaddr) != 1) {
        fprintf(stderr, "bad multicast group: %s\n", cfg->group);
        close(fd);
        return -1;
    }
    if (cfg->iface_ip[0]) {
        if (inet_pton(AF_INET, cfg->iface_ip, &mreq.imr_address) != 1) {
            fprintf(stderr, "bad iface ip: %s\n", cfg->iface_ip);
            close(fd);
            return -1;
        }
    }
    if (cfg->ifname[0]) {
        mreq.imr_ifindex = (int)if_nametoindex(cfg->ifname);
        if (mreq.imr_ifindex == 0) {
            fprintf(stderr, "bad ifname: %s\n", cfg->ifname);
            close(fd);
            return -1;
        }
    }

    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
        perror("IP_ADD_MEMBERSHIP");
        close(fd);
        return -1;
    }

    return fd;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  --group IP        default 239.35.81.1\n"
            "  --port N          default 37101\n"
            "  --iface-ip IP     default 11.11.11.11\n"
            "  --ifname NAME     optional interface name\n"
            "  --bind-ip IP      default 0.0.0.0\n"
            "  --bind-port N     default same as --port\n"
            "  --seconds N       default 10, 0 means until Ctrl-C/max rows\n"
            "  --max-rows N      default 200, 0 means unlimited\n"
            "  --csv PATH        write CSV to file, default stdout\n"
            "  --rcvbuf-mb N     default 64\n",
            argv0);
}

static void init_config(struct config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->group, sizeof(cfg->group), "239.35.81.1");
    cfg->port = 37101;
    snprintf(cfg->iface_ip, sizeof(cfg->iface_ip), "11.11.11.11");
    snprintf(cfg->bind_ip, sizeof(cfg->bind_ip), "0.0.0.0");
    cfg->seconds = 10;
    cfg->max_rows = 200;
    cfg->rcvbuf_mb = 64;
}

static int parse_args(int argc, char **argv, struct config *cfg)
{
    enum {
        OPT_GROUP = 1000,
        OPT_PORT,
        OPT_IFACE_IP,
        OPT_IFNAME,
        OPT_BIND_IP,
        OPT_BIND_PORT,
        OPT_SECONDS,
        OPT_MAX_ROWS,
        OPT_CSV,
        OPT_RCVBUF_MB,
        OPT_HELP
    };
    static const struct option opts[] = {
        {"group", required_argument, NULL, OPT_GROUP},
        {"port", required_argument, NULL, OPT_PORT},
        {"iface-ip", required_argument, NULL, OPT_IFACE_IP},
        {"ifname", required_argument, NULL, OPT_IFNAME},
        {"bind-ip", required_argument, NULL, OPT_BIND_IP},
        {"bind-port", required_argument, NULL, OPT_BIND_PORT},
        {"seconds", required_argument, NULL, OPT_SECONDS},
        {"max-rows", required_argument, NULL, OPT_MAX_ROWS},
        {"csv", required_argument, NULL, OPT_CSV},
        {"rcvbuf-mb", required_argument, NULL, OPT_RCVBUF_MB},
        {"help", no_argument, NULL, OPT_HELP},
        {0, 0, 0, 0}
    };

    for (;;) {
        int opt = getopt_long(argc, argv, "", opts, NULL);
        if (opt == -1) {
            break;
        }
        switch (opt) {
        case OPT_GROUP:
            snprintf(cfg->group, sizeof(cfg->group), "%s", optarg);
            break;
        case OPT_PORT:
            cfg->port = atoi(optarg);
            break;
        case OPT_IFACE_IP:
            snprintf(cfg->iface_ip, sizeof(cfg->iface_ip), "%s", optarg);
            break;
        case OPT_IFNAME:
            snprintf(cfg->ifname, sizeof(cfg->ifname), "%s", optarg);
            break;
        case OPT_BIND_IP:
            snprintf(cfg->bind_ip, sizeof(cfg->bind_ip), "%s", optarg);
            break;
        case OPT_BIND_PORT:
            cfg->bind_port = atoi(optarg);
            break;
        case OPT_SECONDS:
            cfg->seconds = atoi(optarg);
            break;
        case OPT_MAX_ROWS:
            cfg->max_rows = atoi(optarg);
            break;
        case OPT_CSV:
            snprintf(cfg->csv_path, sizeof(cfg->csv_path), "%s", optarg);
            break;
        case OPT_RCVBUF_MB:
            cfg->rcvbuf_mb = atoi(optarg);
            break;
        case OPT_HELP:
            usage(argv[0]);
            exit(0);
        default:
            usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct config cfg;
    init_config(&cfg);
    if (parse_args(argc, argv, &cfg) != 0) {
        return 2;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int fd = open_multicast_socket(&cfg);
    if (fd < 0) {
        return 1;
    }

    FILE *out = stdout;
    if (cfg.csv_path[0]) {
        out = fopen(cfg.csv_path, "w");
        if (!out) {
            perror("fopen csv");
            close(fd);
            return 1;
        }
    }
    setvbuf(out, NULL, _IOLBF, 0);

    fprintf(out,
            "rx_epoch_ns,decode_done_epoch_ns,decode_cost_ns,inter_rx_ns,src_ip,src_port,udp_len,"
            "udp_packet_idx,record_idx,record_count,"
            "seq,seq_gap,type_u32,tag0,tag1,tag2,symbol,raw20_u64,"
            "u32_28,u32_32,u32_36,u32_40,u32_44,u32_48,flags_55_56\n");

    fprintf(stderr,
            "listening tick group=%s port=%d iface_ip=%s ifname=%s seconds=%d max_rows=%d csv=%s\n",
            cfg.group, cfg.port, cfg.iface_ip, cfg.ifname[0] ? cfg.ifname : "-",
            cfg.seconds, cfg.max_rows, cfg.csv_path[0] ? cfg.csv_path : "stdout");

    uint8_t buf[MAX_PACKET];
    uint64_t start_mono = mono_ns();
    uint64_t last_stats_mono = start_mono;
    uint64_t last_rx_mono = 0;
    uint32_t last_seq = 0;
    bool have_seq = false;
    uint64_t packets = 0;
    uint64_t bytes = 0;
    uint64_t rows = 0;

    while (!g_stop) {
        uint64_t now_mono = mono_ns();
        if (cfg.seconds > 0 && now_mono - start_mono >= (uint64_t)cfg.seconds * 1000000000ull) {
            break;
        }
        if (cfg.max_rows > 0 && rows >= (uint64_t)cfg.max_rows) {
            break;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = {1, 0};
        int rc = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }
        if (rc == 0) {
            continue;
        }

        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &src_len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            break;
        }

        uint64_t rx_epoch = realtime_ns();
        uint64_t rx_mono = mono_ns();
        packets++;
        bytes += (uint64_t)n;

        if (n >= 72) {
            uint32_t record_count = (uint32_t)(n / 72);
            if (n % 72 != 0) {
                record_count = 1;
            }
            char src_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));
            uint64_t decode_start = mono_ns();
            uint64_t inter_rx = last_rx_mono ? (rx_mono - last_rx_mono) : 0;
            last_rx_mono = rx_mono;
            for (uint32_t record_idx = 0; record_idx < record_count; ++record_idx) {
                const uint8_t *rec = buf + ((size_t)record_idx * 72);
                uint32_t seq = le32(rec + 0);
                uint32_t type_u32 = le32(rec + 4);
                uint64_t raw20 = le64(rec + 20);
                uint32_t seq_gap = 0;
                if (have_seq && seq != last_seq + 1) {
                    seq_gap = seq - last_seq - 1;
                }
                have_seq = true;
                last_seq = seq;

                char symbol[16];
                char flags[8];
                copy_ascii(symbol, sizeof(symbol), rec + 11, 6);
                copy_ascii(flags, sizeof(flags), rec + 55, 2);

                uint64_t decode_done_mono = mono_ns();
                uint64_t decode_done_epoch = rx_epoch + (decode_done_mono - rx_mono);

                fprintf(out,
                        "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s,%u,%zd,"
                        "%" PRIu64 ",%" PRIu32 ",%" PRIu32 ","
                        "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%u,%u,%u,%s,%" PRIu64 ","
                        "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%s\n",
                        rx_epoch, decode_done_epoch, decode_done_mono - decode_start, inter_rx,
                        src_ip, ntohs(src.sin_port), n,
                        packets, record_idx, record_count,
                        seq, seq_gap, type_u32, rec[8], rec[9], rec[10], symbol, raw20,
                        le32(rec + 28), le32(rec + 32), le32(rec + 36), le32(rec + 40),
                        le32(rec + 44), le32(rec + 48), flags);
                rows++;
                if (cfg.max_rows > 0 && rows >= (uint64_t)cfg.max_rows) {
                    break;
                }
            }
        }

        now_mono = mono_ns();
        if (now_mono - last_stats_mono >= 5000000000ull) {
            double elapsed = (double)(now_mono - start_mono) / 1000000000.0;
            fprintf(stderr,
                    "stats elapsed=%.3f packets=%" PRIu64 " rows=%" PRIu64 " bytes=%" PRIu64
                    " pps=%.3f mbps=%.3f\n",
                    elapsed, packets, rows, bytes,
                    elapsed > 0.0 ? (double)packets / elapsed : 0.0,
                    elapsed > 0.0 ? (double)bytes * 8.0 / elapsed / 1000000.0 : 0.0);
            last_stats_mono = now_mono;
        }
    }

    uint64_t end_mono = mono_ns();
    double elapsed = (double)(end_mono - start_mono) / 1000000000.0;
    fprintf(stderr,
            "done elapsed=%.3f packets=%" PRIu64 " rows=%" PRIu64 " bytes=%" PRIu64
            " pps=%.3f mbps=%.3f\n",
            elapsed, packets, rows, bytes,
            elapsed > 0.0 ? (double)packets / elapsed : 0.0,
            elapsed > 0.0 ? (double)bytes * 8.0 / elapsed / 1000000.0 : 0.0);

    if (out != stdout) {
        fclose(out);
    }
    close(fd);
    return 0;
}
