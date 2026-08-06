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
#include <sys/types.h>
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
    int print_packets;
    int hex_len;
    int rcvbuf_mb;
};

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void format_time(uint64_t ns, char *buf, size_t len)
{
    time_t sec = (time_t)(ns / 1000000000ull);
    struct tm tmv;
    localtime_r(&sec, &tmv);
    int n = snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d.%09" PRIu64,
                     tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                     tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                     (uint64_t)(ns % 1000000000ull));
    if (n < 0 || (size_t)n >= len) {
        if (len) {
            buf[len - 1] = '\0';
        }
    }
}

static uint16_t rd_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint16_t rd_le16(const uint8_t *p)
{
    return ((uint16_t)p[1] << 8) | (uint16_t)p[0];
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t rd_le32(const uint8_t *p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

static void print_hex_ascii(const uint8_t *buf, size_t len, size_t max_len)
{
    size_t n = len < max_len ? len : max_len;
    for (size_t off = 0; off < n; off += 16) {
        printf("  %04zx  ", off);
        for (size_t i = 0; i < 16; ++i) {
            if (off + i < n) {
                printf("%02x ", buf[off + i]);
            } else {
                printf("   ");
            }
        }
        printf(" |");
        for (size_t i = 0; i < 16 && off + i < n; ++i) {
            unsigned char c = buf[off + i];
            putchar((c >= 32 && c <= 126) ? (int)c : '.');
        }
        printf("|\n");
    }
    if (len > max_len) {
        printf("  ... truncated, packet_len=%zu hex_len=%zu\n", len, max_len);
    }
}

static void print_integer_probe(const uint8_t *buf, size_t len)
{
    size_t n = len < 32 ? len : 32;
    printf("  integer_probe:");
    for (size_t off = 0; off + 4 <= n; off += 4) {
        printf(" off%02zu be32=%" PRIu32 " le32=%" PRIu32,
               off, rd_be32(buf + off), rd_le32(buf + off));
    }
    if (len >= 2) {
        printf(" be16[0]=%" PRIu16 " le16[0]=%" PRIu16,
               rd_be16(buf), rd_le16(buf));
    }
    printf("\n");
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
            "\n"
            "Minimal raw UDP multicast probe for Guosen FPGA SZ market data.\n"
            "It only joins a group and prints packet metadata plus hex/ascii bytes.\n"
            "\n"
            "Options:\n"
            "  --group IP             multicast group, default 239.35.80.1 (Dongguan SZ snapshot primary)\n"
            "  --port N               multicast UDP port, default 37100\n"
            "  --iface-ip IP          local market-data interface IP, default 11.11.11.11\n"
            "  --ifname NAME          optional interface name for SO_BINDTODEVICE/join by index\n"
            "  --bind-ip IP           local bind IP, default 0.0.0.0\n"
            "  --bind-port N          local bind port, default same as --port\n"
            "  --seconds N            run seconds, 0 means until Ctrl-C, default 30\n"
            "  --print-packets N      print first N packets, default 20\n"
            "  --hex-len N            bytes to hex dump per printed packet, default 128\n"
            "  --rcvbuf-mb N          receive buffer MB, default 64\n"
            "  --help                 show this help\n"
            "\n"
            "Common SZ channels from the manual:\n"
            "  snapshot primary       --group 239.35.80.1  --port 37100\n"
            "  tick-by-tick primary   --group 239.35.81.1  --port 37101\n"
            "  index primary          --group 239.35.82.1  --port 37102\n"
            "  fund snapshot primary  --group 239.35.85.1  --port 37105\n"
            "  HK connect primary     --group 239.35.90.1  --port 37110\n",
            argv0);
}

static void init_config(struct config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->group, sizeof(cfg->group), "239.35.80.1");
    cfg->port = 37100;
    snprintf(cfg->iface_ip, sizeof(cfg->iface_ip), "11.11.11.11");
    snprintf(cfg->bind_ip, sizeof(cfg->bind_ip), "0.0.0.0");
    cfg->bind_port = 0;
    cfg->seconds = 30;
    cfg->print_packets = 20;
    cfg->hex_len = 128;
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
        OPT_PRINT_PACKETS,
        OPT_HEX_LEN,
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
        {"print-packets", required_argument, NULL, OPT_PRINT_PACKETS},
        {"hex-len", required_argument, NULL, OPT_HEX_LEN},
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
        case OPT_PRINT_PACKETS:
            cfg->print_packets = atoi(optarg);
            break;
        case OPT_HEX_LEN:
            cfg->hex_len = atoi(optarg);
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

    if (cfg->port <= 0 || cfg->port > 65535) {
        fprintf(stderr, "bad port: %d\n", cfg->port);
        return -1;
    }
    if (cfg->bind_port < 0 || cfg->bind_port > 65535) {
        fprintf(stderr, "bad bind-port: %d\n", cfg->bind_port);
        return -1;
    }
    if (cfg->seconds < 0 || cfg->print_packets < 0 || cfg->hex_len < 0 || cfg->rcvbuf_mb < 0) {
        fprintf(stderr, "negative numeric option is invalid\n");
        return -1;
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

    fprintf(stderr,
            "listening group=%s port=%d iface_ip=%s ifname=%s bind=%s:%d seconds=%d print_packets=%d hex_len=%d\n",
            cfg.group, cfg.port, cfg.iface_ip[0] ? cfg.iface_ip : "-",
            cfg.ifname[0] ? cfg.ifname : "-", cfg.bind_ip,
            cfg.bind_port > 0 ? cfg.bind_port : cfg.port,
            cfg.seconds, cfg.print_packets, cfg.hex_len);

    uint8_t buf[MAX_PACKET];
    uint64_t start = now_ns();
    uint64_t last_stats = start;
    uint64_t packets = 0;
    uint64_t bytes = 0;
    uint64_t printed = 0;

    while (!g_stop) {
        uint64_t now = now_ns();
        if (cfg.seconds > 0 && now - start >= (uint64_t)cfg.seconds * 1000000000ull) {
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
            now = now_ns();
            if (now - last_stats >= 5000000000ull) {
                double elapsed = (double)(now - start) / 1000000000.0;
                fprintf(stderr, "stats elapsed=%.3f packets=%" PRIu64 " bytes=%" PRIu64 "\n",
                        elapsed, packets, bytes);
                last_stats = now;
            }
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

        now = now_ns();
        packets++;
        bytes += (uint64_t)n;

        if (printed < (uint64_t)cfg.print_packets) {
            char ts[64];
            char src_ip[INET_ADDRSTRLEN];
            format_time(now, ts, sizeof(ts));
            inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));
            printf("packet #%" PRIu64 " time=%s src=%s:%u len=%zd\n",
                   packets, ts, src_ip, ntohs(src.sin_port), n);
            print_integer_probe(buf, (size_t)n);
            print_hex_ascii(buf, (size_t)n, (size_t)cfg.hex_len);
            fflush(stdout);
            printed++;
        }

        if (now - last_stats >= 5000000000ull) {
            double elapsed = (double)(now - start) / 1000000000.0;
            double pps = elapsed > 0.0 ? (double)packets / elapsed : 0.0;
            double mbps = elapsed > 0.0 ? (double)bytes * 8.0 / elapsed / 1000000.0 : 0.0;
            fprintf(stderr,
                    "stats elapsed=%.3f packets=%" PRIu64 " bytes=%" PRIu64 " pps=%.3f mbps=%.3f\n",
                    elapsed, packets, bytes, pps, mbps);
            last_stats = now;
        }
    }

    uint64_t end = now_ns();
    double elapsed = (double)(end - start) / 1000000000.0;
    fprintf(stderr,
            "done elapsed=%.3f packets=%" PRIu64 " bytes=%" PRIu64 " pps=%.3f mbps=%.3f\n",
            elapsed, packets, bytes,
            elapsed > 0.0 ? (double)packets / elapsed : 0.0,
            elapsed > 0.0 ? (double)bytes * 8.0 / elapsed / 1000000.0 : 0.0);

    close(fd);
    return 0;
}
