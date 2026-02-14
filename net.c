#include "net.h"
#include "pci.h"
#include "utils.h"

#define E1000_VENDOR_INTEL 0x8086
#define E1000_DEV_82540EM  0x100E
#define E1000_DEV_82545EM  0x100F
#define E1000_DEV_82574L   0x10D3
#define E1000_DEV_82579    0x1502

#define E1000_REG_CTRL   0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_IMC    0x00D8

#define E1000_REG_RCTL   0x0100
#define E1000_REG_TCTL   0x0400
#define E1000_REG_TIPG   0x0410
#define E1000_REG_TDBAL  0x3800
#define E1000_REG_TDBAH  0x3804
#define E1000_REG_TDLEN  0x3808
#define E1000_REG_TDH    0x3810
#define E1000_REG_TDT    0x3818

#define E1000_REG_RDBAL  0x2800
#define E1000_REG_RDBAH  0x2804
#define E1000_REG_RDLEN  0x2808
#define E1000_REG_RDH    0x2810
#define E1000_REG_RDT    0x2818

#define E1000_REG_RAL0   0x5400
#define E1000_REG_RAH0   0x5404

#define E1000_RCTL_EN     0x00000002
#define E1000_RCTL_BAM    0x00008000
#define E1000_RCTL_SECRC  0x04000000

#define E1000_TCTL_EN    0x00000002
#define E1000_TCTL_PSP   0x00000008
#define E1000_TCTL_CT    (0x10u << 4)
#define E1000_TCTL_COLD  (0x40u << 12)

#define E1000_TX_CMD_EOP  0x01
#define E1000_TX_CMD_IFCS 0x02
#define E1000_TX_CMD_RS   0x08
#define E1000_TX_STATUS_DD 0x01

#define E1000_RX_STATUS_DD  0x01
#define E1000_RX_STATUS_EOP 0x02

#define E1000_TX_DESC_COUNT 8
#define E1000_TX_BUF_SIZE 2048
#define E1000_RX_DESC_COUNT 32
#define E1000_RX_BUF_SIZE 2048

#define ETH_TYPE_ARP   0x0806
#define ETH_TYPE_IPV4  0x0800
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2
#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17
#define DNS_PORT       53
#define TCP_FLAG_FIN   0x01
#define TCP_FLAG_SYN   0x02
#define TCP_FLAG_RST   0x04
#define TCP_FLAG_PSH   0x08
#define TCP_FLAG_ACK   0x10

typedef struct __attribute__((packed)) {
    unsigned long long addr;
    unsigned short length;
    unsigned char cso;
    unsigned char cmd;
    unsigned char status;
    unsigned char css;
    unsigned short special;
} E1000TxDesc;

typedef struct __attribute__((packed)) {
    unsigned long long addr;
    unsigned short length;
    unsigned short checksum;
    unsigned char status;
    unsigned char errors;
    unsigned short special;
} E1000RxDesc;

static NetDriverInfo g_net_info;
static int g_net_scanned = 0;
static volatile unsigned char* g_e1000_mmio = 0;
static unsigned int g_tx_tail = 0;
static unsigned int g_rx_tail = 0;
static unsigned short g_icmp_seq = 1;
static unsigned short g_ip_ident = 1;
static unsigned char g_local_ip[4] = {10, 0, 2, 15};
static unsigned char g_gateway_ip[4] = {10, 0, 2, 2};
static unsigned char g_dns_ip[4] = {10, 0, 2, 3};

static E1000TxDesc g_tx_desc[E1000_TX_DESC_COUNT] __attribute__((aligned(16)));
static unsigned char g_tx_buf[E1000_TX_DESC_COUNT][E1000_TX_BUF_SIZE] __attribute__((aligned(16)));
static E1000RxDesc g_rx_desc[E1000_RX_DESC_COUNT] __attribute__((aligned(16)));
static unsigned char g_rx_buf[E1000_RX_DESC_COUNT][E1000_RX_BUF_SIZE] __attribute__((aligned(16)));

static int is_e1000(unsigned short vendor, unsigned short device) {
    if (vendor != E1000_VENDOR_INTEL) return 0;
    if (device == E1000_DEV_82540EM) return 1;
    if (device == E1000_DEV_82545EM) return 1;
    if (device == E1000_DEV_82574L) return 1;
    if (device == E1000_DEV_82579) return 1;
    return 0;
}

static int find_e1000_pci(PciDeviceInfo* out) {
    PciDeviceInfo info;
    for (unsigned int bus = 0; bus < 1; bus++) {
        for (unsigned int slot = 0; slot < 32; slot++) {
            for (unsigned int func = 0; func < 8; func++) {
                if (!pci_probe_device((unsigned char)bus, (unsigned char)slot, (unsigned char)func, &info)) {
                    continue;
                }
                if (is_e1000(info.vendor_id, info.device_id)) {
                    if (out) *out = info;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static unsigned int e1000_read(unsigned int reg) {
    if (!g_e1000_mmio) return 0;
    volatile unsigned int* p = (volatile unsigned int*)(g_e1000_mmio + reg);
    return *p;
}

static void e1000_write(unsigned int reg, unsigned int value) {
    if (!g_e1000_mmio) return;
    volatile unsigned int* p = (volatile unsigned int*)(g_e1000_mmio + reg);
    *p = value;
}

static void e1000_read_mac(unsigned char mac[6]) {
    unsigned int ral = e1000_read(E1000_REG_RAL0);
    unsigned int rah = e1000_read(E1000_REG_RAH0);
    mac[0] = (unsigned char)(ral & 0xFF);
    mac[1] = (unsigned char)((ral >> 8) & 0xFF);
    mac[2] = (unsigned char)((ral >> 16) & 0xFF);
    mac[3] = (unsigned char)((ral >> 24) & 0xFF);
    mac[4] = (unsigned char)(rah & 0xFF);
    mac[5] = (unsigned char)((rah >> 8) & 0xFF);
}

static void e1000_init_tx(void) {
    for (int i = 0; i < E1000_TX_DESC_COUNT; i++) {
        g_tx_desc[i].addr = (unsigned int)&g_tx_buf[i][0];
        g_tx_desc[i].length = 0;
        g_tx_desc[i].cso = 0;
        g_tx_desc[i].cmd = 0;
        g_tx_desc[i].status = E1000_TX_STATUS_DD;
        g_tx_desc[i].css = 0;
        g_tx_desc[i].special = 0;
    }

    e1000_write(E1000_REG_TDBAL, (unsigned int)&g_tx_desc[0]);
    e1000_write(E1000_REG_TDBAH, 0);
    e1000_write(E1000_REG_TDLEN, (unsigned int)sizeof(g_tx_desc));
    e1000_write(E1000_REG_TDH, 0);
    e1000_write(E1000_REG_TDT, 0);
    e1000_write(E1000_REG_TIPG, 0x0060200A);
    e1000_write(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT | E1000_TCTL_COLD);
    g_tx_tail = 0;
}

static void e1000_init_rx(void) {
    for (int i = 0; i < E1000_RX_DESC_COUNT; i++) {
        g_rx_desc[i].addr = (unsigned int)&g_rx_buf[i][0];
        g_rx_desc[i].length = 0;
        g_rx_desc[i].checksum = 0;
        g_rx_desc[i].status = 0;
        g_rx_desc[i].errors = 0;
        g_rx_desc[i].special = 0;
    }

    e1000_write(E1000_REG_RDBAL, (unsigned int)&g_rx_desc[0]);
    e1000_write(E1000_REG_RDBAH, 0);
    e1000_write(E1000_REG_RDLEN, (unsigned int)sizeof(g_rx_desc));
    e1000_write(E1000_REG_RDH, 0);
    e1000_write(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1);
    e1000_write(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
    g_rx_tail = 0;
}

static unsigned short be16_read(const unsigned char* p) {
    return (unsigned short)(((unsigned short)p[0] << 8) | p[1]);
}

static unsigned int be32_read(const unsigned char* p) {
    return ((unsigned int)p[0] << 24)
        | ((unsigned int)p[1] << 16)
        | ((unsigned int)p[2] << 8)
        | (unsigned int)p[3];
}

static void be16_write(unsigned char* p, unsigned short v) {
    p[0] = (unsigned char)((v >> 8) & 0xFF);
    p[1] = (unsigned char)(v & 0xFF);
}

static void be32_write(unsigned char* p, unsigned int v) {
    p[0] = (unsigned char)((v >> 24) & 0xFF);
    p[1] = (unsigned char)((v >> 16) & 0xFF);
    p[2] = (unsigned char)((v >> 8) & 0xFF);
    p[3] = (unsigned char)(v & 0xFF);
}

static int bytes_equal(const unsigned char* a, const unsigned char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static int ip_is_public_routable(const unsigned char ip[4]) {
    // 过滤不可公网直连网段，避免 DNS 返回保留地址时连不上
    if (!ip) return 0;

    if (ip[0] == 0 || ip[0] == 10 || ip[0] == 127) return 0;
    if (ip[0] >= 224) return 0;

    if (ip[0] == 100 && (ip[1] >= 64 && ip[1] <= 127)) return 0; // 100.64.0.0/10
    if (ip[0] == 169 && ip[1] == 254) return 0;                  // 169.254.0.0/16
    if (ip[0] == 172 && (ip[1] >= 16 && ip[1] <= 31)) return 0;  // 172.16.0.0/12
    if (ip[0] == 192 && ip[1] == 168) return 0;                  // 192.168.0.0/16
    if (ip[0] == 198 && (ip[1] == 18 || ip[1] == 19)) return 0;  // 198.18.0.0/15

    return 1;
}

static unsigned short checksum16(const unsigned char* data, unsigned int len) {
    unsigned int sum = 0;
    unsigned int i = 0;

    while (i + 1 < len) {
        sum += ((unsigned int)data[i] << 8) | data[i + 1];
        i += 2;
    }
    if (i < len) {
        sum += ((unsigned int)data[i] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (unsigned short)(~sum);
}

static unsigned short tcp_checksum_ipv4(const unsigned char src_ip[4], const unsigned char dst_ip[4],
                                        const unsigned char* tcp, unsigned short tcp_len) {
    unsigned int sum = 0;

    sum += ((unsigned int)src_ip[0] << 8) | src_ip[1];
    sum += ((unsigned int)src_ip[2] << 8) | src_ip[3];
    sum += ((unsigned int)dst_ip[0] << 8) | dst_ip[1];
    sum += ((unsigned int)dst_ip[2] << 8) | dst_ip[3];
    sum += (unsigned int)IP_PROTO_TCP;
    sum += tcp_len;

    for (unsigned int i = 0; i + 1 < tcp_len; i += 2) {
        sum += ((unsigned int)tcp[i] << 8) | tcp[i + 1];
    }
    if (tcp_len & 1) {
        sum += ((unsigned int)tcp[tcp_len - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (unsigned short)(~sum);
}

static unsigned short udp_checksum_ipv4(const unsigned char src_ip[4], const unsigned char dst_ip[4],
                                        const unsigned char* udp, unsigned short udp_len) {
    unsigned int sum = 0;

    sum += ((unsigned int)src_ip[0] << 8) | src_ip[1];
    sum += ((unsigned int)src_ip[2] << 8) | src_ip[3];
    sum += ((unsigned int)dst_ip[0] << 8) | dst_ip[1];
    sum += ((unsigned int)dst_ip[2] << 8) | dst_ip[3];
    sum += (unsigned int)IP_PROTO_UDP;
    sum += udp_len;

    for (unsigned int i = 0; i + 1 < udp_len; i += 2) {
        sum += ((unsigned int)udp[i] << 8) | udp[i + 1];
    }
    if (udp_len & 1) {
        sum += ((unsigned int)udp[udp_len - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    unsigned short cs = (unsigned short)(~sum);
    if (cs == 0) cs = 0xFFFFu;
    return cs;
}

static void choose_next_hop(const unsigned char dst_ip[4], unsigned char next_hop[4]) {
    // 简单 /24 路由: 本地网段直连，否则走默认网关
    if (dst_ip[0] == g_local_ip[0] && dst_ip[1] == g_local_ip[1] && dst_ip[2] == g_local_ip[2]) {
        memcpy(next_hop, dst_ip, 4);
    } else {
        memcpy(next_hop, g_gateway_ip, 4);
    }
}

static int e1000_send_frame(const unsigned char* frame, unsigned short len) {
    if (!g_net_info.initialized || !g_net_info.tx_ready) return 0;
    if (!frame || len == 0) return 0;

    unsigned short tx_len = len;
    if (tx_len < 60) tx_len = 60;
    if (tx_len > E1000_TX_BUF_SIZE) return 0;

    unsigned int idx = g_tx_tail;
    volatile E1000TxDesc* d = (volatile E1000TxDesc*)&g_tx_desc[idx];
    if ((d->status & E1000_TX_STATUS_DD) == 0) return 0;

    memset(&g_tx_buf[idx][0], 0, tx_len);
    memcpy(&g_tx_buf[idx][0], frame, len);

    d->addr = (unsigned int)&g_tx_buf[idx][0];
    d->length = tx_len;
    d->cso = 0;
    d->cmd = (unsigned char)(E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS);
    d->status = 0;
    d->css = 0;
    d->special = 0;

    g_tx_tail = (idx + 1) % E1000_TX_DESC_COUNT;
    e1000_write(E1000_REG_TDT, g_tx_tail);

    int timeout = 1000000;
    while (timeout-- > 0) {
        if (d->status & E1000_TX_STATUS_DD) {
            g_net_info.tx_ok_count++;
            return 1;
        }
    }
    return 0;
}

static int e1000_rx_peek(unsigned char** data, unsigned short* len, unsigned char* status, unsigned char* errors) {
    if (!g_net_info.initialized || !g_net_info.rx_ready) return 0;

    volatile E1000RxDesc* d = (volatile E1000RxDesc*)&g_rx_desc[g_rx_tail];
    if ((d->status & E1000_RX_STATUS_DD) == 0) return 0;

    if (data) *data = &g_rx_buf[g_rx_tail][0];
    if (len) *len = d->length;
    if (status) *status = d->status;
    if (errors) *errors = d->errors;
    return 1;
}

static void e1000_rx_pop(void) {
    volatile E1000RxDesc* d = (volatile E1000RxDesc*)&g_rx_desc[g_rx_tail];
    unsigned char st = d->status;
    unsigned char err = d->errors;
    if ((st & E1000_RX_STATUS_DD) && (st & E1000_RX_STATUS_EOP) && err == 0) {
        g_net_info.rx_ok_count++;
    }

    d->status = 0;
    d->errors = 0;
    d->length = 0;
    d->checksum = 0;
    d->special = 0;

    e1000_write(E1000_REG_RDT, g_rx_tail);
    g_rx_tail = (g_rx_tail + 1) % E1000_RX_DESC_COUNT;
}

static void e1000_rx_drain(int max_frames) {
    while (max_frames-- > 0) {
        if (!e1000_rx_peek(0, 0, 0, 0)) break;
        e1000_rx_pop();
    }
}

static int send_ipv4_packet(const unsigned char dst_mac[6], const unsigned char dst_ip[4],
                            unsigned char protocol, const unsigned char* payload, unsigned short payload_len) {
    unsigned short ip_total_len = (unsigned short)(20 + payload_len);
    unsigned short frame_len = (unsigned short)(14 + ip_total_len);
    if (frame_len > 600) return 0;

    unsigned char frame[600];
    unsigned char* ip = frame + 14;

    memset(frame, 0, sizeof(frame));
    memcpy(frame + 0, dst_mac, 6);
    memcpy(frame + 6, g_net_info.mac, 6);
    be16_write(frame + 12, ETH_TYPE_IPV4);

    ip[0] = 0x45;
    ip[1] = 0x00;
    be16_write(ip + 2, ip_total_len);
    be16_write(ip + 4, g_ip_ident++);
    be16_write(ip + 6, 0x0000);
    ip[8] = 64;
    ip[9] = protocol;
    be16_write(ip + 10, 0);
    memcpy(ip + 12, g_local_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    be16_write(ip + 10, checksum16(ip, 20));

    if (payload_len > 0 && payload) {
        memcpy(ip + 20, payload, payload_len);
    }

    return e1000_send_frame(frame, frame_len);
}

static int send_udp_packet(const unsigned char dst_mac[6], const unsigned char dst_ip[4],
                           unsigned short src_port, unsigned short dst_port,
                           const unsigned char* payload, unsigned short payload_len) {
    unsigned short udp_len = (unsigned short)(8 + payload_len);
    if (udp_len > 560) return 0;

    unsigned char udp[560];
    memset(udp, 0, sizeof(udp));
    be16_write(udp + 0, src_port);
    be16_write(udp + 2, dst_port);
    be16_write(udp + 4, udp_len);
    be16_write(udp + 6, 0);
    if (payload_len > 0 && payload) {
        memcpy(udp + 8, payload, payload_len);
    }
    be16_write(udp + 6, udp_checksum_ipv4(g_local_ip, dst_ip, udp, udp_len));

    return send_ipv4_packet(dst_mac, dst_ip, IP_PROTO_UDP, udp, udp_len);
}

static int send_tcp_packet(const unsigned char dst_mac[6], const unsigned char dst_ip[4],
                           unsigned short src_port, unsigned short dst_port,
                           unsigned int seq, unsigned int ack,
                           unsigned char flags, const unsigned char* payload, unsigned short payload_len) {
    unsigned short tcp_len = (unsigned short)(20 + payload_len);
    if (tcp_len > 580) return 0;

    unsigned char tcp[580];
    memset(tcp, 0, sizeof(tcp));

    be16_write(tcp + 0, src_port);
    be16_write(tcp + 2, dst_port);
    be32_write(tcp + 4, seq);
    be32_write(tcp + 8, ack);
    tcp[12] = (unsigned char)(5u << 4); // data offset = 20 bytes
    tcp[13] = flags;
    be16_write(tcp + 14, 0x4000);        // window
    be16_write(tcp + 16, 0);
    be16_write(tcp + 18, 0);
    if (payload_len > 0 && payload) {
        memcpy(tcp + 20, payload, payload_len);
    }
    be16_write(tcp + 16, tcp_checksum_ipv4(g_local_ip, dst_ip, tcp, tcp_len));

    return send_ipv4_packet(dst_mac, dst_ip, IP_PROTO_TCP, tcp, tcp_len);
}

static int dns_encode_qname(const char* host, unsigned char* out, int max_len) {
    if (!host || !host[0] || !out || max_len < 2) return 0;

    int out_pos = 0;
    int label_len_pos = out_pos++;
    int label_len = 0;

    for (int i = 0;; i++) {
        char ch = host[i];
        if (ch == '.' || ch == '\0') {
            if (label_len <= 0 || label_len > 63) return 0;
            out[label_len_pos] = (unsigned char)label_len;
            if (ch == '\0') {
                if (out_pos >= max_len) return 0;
                out[out_pos++] = 0;
                return out_pos;
            }
            if (out_pos >= max_len) return 0;
            label_len_pos = out_pos++;
            label_len = 0;
            continue;
        }

        if (ch == ' ' || ch == '\t') return 0;
        if (out_pos >= max_len) return 0;
        out[out_pos++] = (unsigned char)ch;
        label_len++;
    }
}

static int dns_skip_name(const unsigned char* msg, int msg_len, int offset) {
    int safety = 0;
    while (offset < msg_len && safety < msg_len) {
        unsigned char len = msg[offset];
        if ((len & 0xC0) == 0xC0) {
            if (offset + 1 >= msg_len) return -1;
            return offset + 2;
        }
        if (len == 0) return offset + 1;
        offset++;
        if (offset + len > msg_len) return -1;
        offset += len;
        safety++;
    }
    return -1;
}

static int dns_extract_first_a(const unsigned char* msg, unsigned short msg_len,
                               unsigned short txid, unsigned char out_ip[4]) {
    if (!msg || msg_len < 12 || !out_ip) return 0;
    if (be16_read(msg + 0) != txid) return 0;

    unsigned short flags = be16_read(msg + 2);
    if ((flags & 0x8000) == 0) return 0;
    if ((flags & 0x000F) != 0) return 0;

    unsigned short qdcount = be16_read(msg + 4);
    unsigned short ancount = be16_read(msg + 6);
    unsigned short nscount = be16_read(msg + 8);
    unsigned short arcount = be16_read(msg + 10);
    int off = 12;

    for (unsigned int i = 0; i < qdcount; i++) {
        off = dns_skip_name(msg, msg_len, off);
        if (off < 0 || off + 4 > msg_len) return 0;
        off += 4;
    }

    unsigned int answer_end = ancount;
    unsigned int authority_end = answer_end + nscount;
    unsigned int total_rr = authority_end + arcount;

    unsigned char first_answer_a[4];
    unsigned char first_additional_a[4];
    int has_answer_a = 0;
    int has_additional_a = 0;

    for (unsigned int i = 0; i < total_rr; i++) {
        off = dns_skip_name(msg, msg_len, off);
        if (off < 0 || off + 10 > msg_len) return 0;

        unsigned short type = be16_read(msg + off); off += 2;
        unsigned short cls = be16_read(msg + off); off += 2;
        unsigned int ttl = be32_read(msg + off); off += 4;
        unsigned short rdlen = be16_read(msg + off); off += 2;
        (void)ttl;

        if (off + rdlen > msg_len) return 0;
        if (type == 1 && cls == 1 && rdlen == 4) {
            const unsigned char* cand = msg + off;
            if (i < answer_end) {
                if (!has_answer_a) {
                    memcpy(first_answer_a, cand, 4);
                    has_answer_a = 1;
                }
                if (ip_is_public_routable(cand)) {
                    memcpy(out_ip, cand, 4);
                    return 1;
                }
            } else if (i >= authority_end) {
                if (!has_additional_a) {
                    memcpy(first_additional_a, cand, 4);
                    has_additional_a = 1;
                }
                if (!has_answer_a && ip_is_public_routable(cand)) {
                    memcpy(out_ip, cand, 4);
                    return 1;
                }
            }
        }
        off += rdlen;
    }

    if (has_answer_a) {
        memcpy(out_ip, first_answer_a, 4);
        return 1;
    }
    if (has_additional_a) {
        memcpy(out_ip, first_additional_a, 4);
        return 1;
    }

    return 0;
}

static int wait_dns_reply(unsigned short src_port, unsigned short txid, unsigned char out_ip[4]) {
    int timeout = 12000000;
    while (timeout-- > 0) {
        unsigned char* rx = 0;
        unsigned short rx_len = 0;
        unsigned char rx_status = 0;
        unsigned char rx_errors = 0;
        if (!e1000_rx_peek(&rx, &rx_len, &rx_status, &rx_errors)) continue;

        int matched = 0;
        if (rx_errors == 0 && (rx_status & E1000_RX_STATUS_EOP) && rx_len >= 14 + 20 + 8) {
            unsigned short eth_type = be16_read(rx + 12);
            if (eth_type == ETH_TYPE_IPV4) {
                unsigned char* ip = rx + 14;
                unsigned int version = (ip[0] >> 4) & 0xF;
                unsigned int ihl = (ip[0] & 0xF) * 4;
                if (version == 4 && ihl >= 20 && rx_len >= 14 + ihl + 8 && ip[9] == IP_PROTO_UDP) {
                    unsigned short ip_total_len = be16_read(ip + 2);
                    if (ip_total_len >= ihl + 8 && ip_total_len <= (unsigned short)(rx_len - 14)) {
                        unsigned char* src = ip + 12;
                        unsigned char* dst = ip + 16;
                        (void)src;
                        if (bytes_equal(dst, g_local_ip, 4)) {
                            unsigned char* udp = ip + ihl;
                            unsigned short udp_src = be16_read(udp + 0);
                            unsigned short udp_dst = be16_read(udp + 2);
                            unsigned short udp_len = be16_read(udp + 4);
                            if (udp_src == DNS_PORT && udp_dst == src_port &&
                                udp_len >= 8 && udp_len <= (unsigned short)(ip_total_len - ihl)) {
                                unsigned short dns_len = (unsigned short)(udp_len - 8);
                                if (dns_len > 0) {
                                    matched = dns_extract_first_a(udp + 8, dns_len, txid, out_ip);
                                }
                            }
                        }
                    }
                }
            }
        }

        e1000_rx_pop();
        if (matched) return 1;
    }
    return 0;
}

static int send_dns_query(const unsigned char dns_mac[6], unsigned short src_port,
                          unsigned short txid, const char* host,
                          const unsigned char dns_ip[4]) {
    unsigned char dns_payload[300];
    memset(dns_payload, 0, sizeof(dns_payload));

    be16_write(dns_payload + 0, txid);
    be16_write(dns_payload + 2, 0x0100); // RD=1
    be16_write(dns_payload + 4, 1);
    be16_write(dns_payload + 6, 0);
    be16_write(dns_payload + 8, 0);
    be16_write(dns_payload + 10, 0);

    int pos = 12;
    int qname_len = dns_encode_qname(host, dns_payload + pos, (int)sizeof(dns_payload) - pos);
    if (qname_len <= 0) return 0;
    pos += qname_len;

    if (pos + 4 > (int)sizeof(dns_payload)) return 0;
    be16_write(dns_payload + pos, 1); pos += 2; // QTYPE=A
    be16_write(dns_payload + pos, 1); pos += 2; // QCLASS=IN

    return send_udp_packet(dns_mac, dns_ip, src_port, DNS_PORT, dns_payload, (unsigned short)pos);
}

static int parse_tcp_segment_match(const unsigned char* frame, unsigned short frame_len,
                                   const unsigned char src_ip[4], const unsigned char dst_ip[4],
                                   unsigned short src_port, unsigned short dst_port,
                                   unsigned int* out_seq, unsigned int* out_ack,
                                   unsigned char* out_flags,
                                   const unsigned char** out_payload, unsigned short* out_payload_len) {
    if (!frame || frame_len < 14 + 20 + 20) return 0;
    if (be16_read(frame + 12) != ETH_TYPE_IPV4) return 0;

    const unsigned char* ip = frame + 14;
    unsigned int version = (ip[0] >> 4) & 0xF;
    unsigned int ihl = (ip[0] & 0xF) * 4;
    if (version != 4 || ihl < 20) return 0;
    if (frame_len < 14 + ihl + 20) return 0;
    if (ip[9] != IP_PROTO_TCP) return 0;
    if (!bytes_equal(ip + 12, src_ip, 4)) return 0;
    if (!bytes_equal(ip + 16, dst_ip, 4)) return 0;

    unsigned short ip_total_len = be16_read(ip + 2);
    if (ip_total_len < ihl + 20) return 0;
    if (ip_total_len > (unsigned short)(frame_len - 14)) return 0;

    const unsigned char* tcp = ip + ihl;
    if (be16_read(tcp + 0) != src_port) return 0;
    if (be16_read(tcp + 2) != dst_port) return 0;

    unsigned int tcp_hdr_len = ((unsigned int)(tcp[12] >> 4) & 0xF) * 4;
    if (tcp_hdr_len < 20) return 0;
    if (ip_total_len < (unsigned short)(ihl + tcp_hdr_len)) return 0;

    unsigned short tcp_seg_len = (unsigned short)(ip_total_len - ihl);
    unsigned short payload_len = (unsigned short)(tcp_seg_len - tcp_hdr_len);

    if (out_seq) *out_seq = be32_read(tcp + 4);
    if (out_ack) *out_ack = be32_read(tcp + 8);
    if (out_flags) *out_flags = tcp[13];
    if (out_payload) *out_payload = tcp + tcp_hdr_len;
    if (out_payload_len) *out_payload_len = payload_len;
    return 1;
}

static int append_text_buf(char* buf, int idx, int max_len, const char* text) {
    if (!buf || !text || max_len <= 0) return idx;
    while (*text && idx < max_len) {
        buf[idx++] = *text++;
    }
    return idx;
}

static int parse_ipv4_literal(const char* s, unsigned char out_ip[4]) {
    if (!s || !out_ip) return 0;

    int part = 0;
    int value = 0;
    int digits = 0;

    for (const char* p = s;; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            digits++;
            if (digits > 3 || value > 255) return 0;
            continue;
        }
        if (c == '.' || c == '\0') {
            if (digits == 0 || part > 3) return 0;
            out_ip[part++] = (unsigned char)value;
            value = 0;
            digits = 0;
            if (c == '\0') break;
            continue;
        }
        return 0;
    }

    return part == 4;
}

static int parse_http_status_code(const char* resp, int len) {
    if (!resp || len < 12) return 0;
    if (!(resp[0] == 'H' && resp[1] == 'T' && resp[2] == 'T' && resp[3] == 'P')) return 0;

    int i = 0;
    while (i < len && resp[i] != ' ') i++;
    if (i + 3 >= len) return 0;
    i++;
    if (resp[i] < '0' || resp[i] > '9') return 0;
    if (resp[i + 1] < '0' || resp[i + 1] > '9') return 0;
    if (resp[i + 2] < '0' || resp[i + 2] > '9') return 0;
    return (resp[i] - '0') * 100 + (resp[i + 1] - '0') * 10 + (resp[i + 2] - '0');
}

static int arp_resolve(const unsigned char target_ip[4], unsigned char target_mac[6]) {
    unsigned char frame[60];
    memset(frame, 0, sizeof(frame));

    for (int i = 0; i < 6; i++) frame[i] = 0xFF;
    for (int i = 0; i < 6; i++) frame[6 + i] = g_net_info.mac[i];
    be16_write(frame + 12, ETH_TYPE_ARP);

    unsigned char* arp = frame + 14;
    be16_write(arp + 0, 0x0001);
    be16_write(arp + 2, ETH_TYPE_IPV4);
    arp[4] = 6;
    arp[5] = 4;
    be16_write(arp + 6, ARP_OP_REQUEST);
    memcpy(arp + 8, g_net_info.mac, 6);
    memcpy(arp + 14, g_local_ip, 4);
    memset(arp + 18, 0, 6);
    memcpy(arp + 24, target_ip, 4);

    for (int attempt = 0; attempt < 3; attempt++) {
        if (!e1000_send_frame(frame, (unsigned short)sizeof(frame))) continue;

        int timeout = 2000000;
        while (timeout-- > 0) {
            unsigned char* rx = 0;
            unsigned short rx_len = 0;
            unsigned char rx_status = 0;
            unsigned char rx_errors = 0;
            if (!e1000_rx_peek(&rx, &rx_len, &rx_status, &rx_errors)) continue;

            int matched = 0;
            if (rx_errors == 0 && (rx_status & E1000_RX_STATUS_EOP) && rx_len >= 42) {
                unsigned short eth_type = be16_read(rx + 12);
                if (eth_type == ETH_TYPE_ARP) {
                    unsigned char* a = rx + 14;
                    if (be16_read(a + 0) == 0x0001 &&
                        be16_read(a + 2) == ETH_TYPE_IPV4 &&
                        a[4] == 6 &&
                        a[5] == 4 &&
                        be16_read(a + 6) == ARP_OP_REPLY &&
                        bytes_equal(a + 14, target_ip, 4) &&
                        bytes_equal(a + 24, g_local_ip, 4)) {
                        memcpy(target_mac, a + 8, 6);
                        matched = 1;
                    }
                }
            }

            e1000_rx_pop();
            if (matched) return 1;
        }
    }
    return 0;
}

static int send_icmp_echo(const unsigned char dst_mac[6], const unsigned char dst_ip[4], unsigned short ident, unsigned short seq) {
    unsigned char frame[14 + 20 + 8 + 32];
    unsigned char* ip = frame + 14;
    unsigned char* icmp = ip + 20;
    const unsigned short payload_len = 32;
    const unsigned short ip_total_len = 20 + 8 + payload_len;
    const unsigned short frame_len = 14 + ip_total_len;

    memset(frame, 0, sizeof(frame));
    memcpy(frame + 0, dst_mac, 6);
    memcpy(frame + 6, g_net_info.mac, 6);
    be16_write(frame + 12, ETH_TYPE_IPV4);

    ip[0] = 0x45;
    ip[1] = 0x00;
    be16_write(ip + 2, ip_total_len);
    be16_write(ip + 4, g_ip_ident++);
    be16_write(ip + 6, 0x0000);
    ip[8] = 64;
    ip[9] = 1;
    be16_write(ip + 10, 0);
    memcpy(ip + 12, g_local_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    be16_write(ip + 10, checksum16(ip, 20));

    icmp[0] = 8;
    icmp[1] = 0;
    be16_write(icmp + 2, 0);
    be16_write(icmp + 4, ident);
    be16_write(icmp + 6, seq);
    for (unsigned int i = 0; i < payload_len; i++) {
        icmp[8 + i] = (unsigned char)(0x41 + (i & 0x0F));
    }
    be16_write(icmp + 2, checksum16(icmp, 8 + payload_len));

    return e1000_send_frame(frame, frame_len);
}

static int wait_icmp_reply(const unsigned char src_ip[4], unsigned short ident, unsigned short seq) {
    int timeout = 4000000;
    while (timeout-- > 0) {
        unsigned char* rx = 0;
        unsigned short rx_len = 0;
        unsigned char rx_status = 0;
        unsigned char rx_errors = 0;
        if (!e1000_rx_peek(&rx, &rx_len, &rx_status, &rx_errors)) continue;

        int matched = 0;
        if (rx_errors == 0 && (rx_status & E1000_RX_STATUS_EOP) && rx_len >= 14 + 20 + 8) {
            unsigned short eth_type = be16_read(rx + 12);
            if (eth_type == ETH_TYPE_IPV4) {
                unsigned char* ip = rx + 14;
                unsigned int version = (ip[0] >> 4) & 0xF;
                unsigned int ihl = (ip[0] & 0xF) * 4;
                if (version == 4 && ihl >= 20 && rx_len >= 14 + ihl + 8 && ip[9] == 1) {
                    unsigned short ip_total_len = be16_read(ip + 2);
                    if (ip_total_len >= ihl + 8 && ip_total_len <= (unsigned short)(rx_len - 14)) {
                        unsigned char* src = ip + 12;
                        unsigned char* dst = ip + 16;
                        if (bytes_equal(src, src_ip, 4) && bytes_equal(dst, g_local_ip, 4)) {
                            unsigned char* icmp = ip + ihl;
                            if (icmp[0] == 0 && icmp[1] == 0 &&
                                be16_read(icmp + 4) == ident &&
                                be16_read(icmp + 6) == seq) {
                                matched = 1;
                            }
                        }
                    }
                }
            }
        }

        e1000_rx_pop();
        if (matched) return 1;
    }
    return 0;
}

void net_init(void) {
    if (g_net_scanned) return;
    g_net_scanned = 1;

    PciDeviceInfo dev;
    memset(&g_net_info, 0, sizeof(g_net_info));

    if (!find_e1000_pci(&dev)) {
        // 没找到 e1000 时，保留“网卡存在性”探测信息
        if (!pci_find_first_by_class(0x02, 0xFF, &dev)) {
            return;
        }
        g_net_info.present = 1;
        g_net_info.vendor_id = dev.vendor_id;
        g_net_info.device_id = dev.device_id;
        g_net_info.bus = dev.bus;
        g_net_info.slot = dev.slot;
        g_net_info.func = dev.func;
        g_net_info.irq_line = dev.irq_line;
        g_net_info.initialized = 0;
        g_net_info.tx_ready = 0;
        g_net_info.rx_ready = 0;
        return;
    }

    g_net_info.present = 1;
    g_net_info.vendor_id = dev.vendor_id;
    g_net_info.device_id = dev.device_id;
    g_net_info.bus = dev.bus;
    g_net_info.slot = dev.slot;
    g_net_info.func = dev.func;
    g_net_info.irq_line = dev.irq_line;

    if (!is_e1000(dev.vendor_id, dev.device_id)) {
        // 先只支持 e1000
        g_net_info.initialized = 0;
        g_net_info.tx_ready = 0;
        g_net_info.rx_ready = 0;
        return;
    }

    unsigned int bar0 = dev.bar[0];
    if (bar0 & 0x1) {
        // BAR0 是 I/O 类型时，当前版本不支持
        g_net_info.initialized = 0;
        g_net_info.tx_ready = 0;
        g_net_info.rx_ready = 0;
        return;
    }

    unsigned int mmio = bar0 & 0xFFFFFFF0u;
    if (mmio == 0) {
        g_net_info.initialized = 0;
        g_net_info.tx_ready = 0;
        g_net_info.rx_ready = 0;
        return;
    }

    // 开启 Memory Space + Bus Master
    unsigned short cmd = pci_config_read_word(dev.bus, dev.slot, dev.func, 0x04);
    cmd |= 0x0006;
    pci_config_write_word(dev.bus, dev.slot, dev.func, 0x04, cmd);

    g_e1000_mmio = (volatile unsigned char*)mmio;

    // 屏蔽设备中断，先走轮询
    e1000_write(E1000_REG_IMC, 0xFFFFFFFFu);
    (void)e1000_read(E1000_REG_STATUS);
    (void)e1000_read(E1000_REG_CTRL);

    e1000_init_tx();
    e1000_init_rx();
    e1000_read_mac(g_net_info.mac);

    g_net_info.initialized = 1;
    g_net_info.tx_ready = 1;
    g_net_info.rx_ready = 1;
}

const NetDriverInfo* net_get_info(void) {
    return &g_net_info;
}

void net_set_local_ip(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
    g_local_ip[0] = a;
    g_local_ip[1] = b;
    g_local_ip[2] = c;
    g_local_ip[3] = d;
}

void net_get_local_ip(unsigned char out_ip[4]) {
    if (!out_ip) return;
    out_ip[0] = g_local_ip[0];
    out_ip[1] = g_local_ip[1];
    out_ip[2] = g_local_ip[2];
    out_ip[3] = g_local_ip[3];
}

void net_set_gateway(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
    g_gateway_ip[0] = a;
    g_gateway_ip[1] = b;
    g_gateway_ip[2] = c;
    g_gateway_ip[3] = d;
}

void net_get_gateway(unsigned char out_ip[4]) {
    if (!out_ip) return;
    out_ip[0] = g_gateway_ip[0];
    out_ip[1] = g_gateway_ip[1];
    out_ip[2] = g_gateway_ip[2];
    out_ip[3] = g_gateway_ip[3];
}

void net_set_dns_server(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
    g_dns_ip[0] = a;
    g_dns_ip[1] = b;
    g_dns_ip[2] = c;
    g_dns_ip[3] = d;
}

void net_get_dns_server(unsigned char out_ip[4]) {
    if (!out_ip) return;
    out_ip[0] = g_dns_ip[0];
    out_ip[1] = g_dns_ip[1];
    out_ip[2] = g_dns_ip[2];
    out_ip[3] = g_dns_ip[3];
}

int net_send_test_frame(void) {
    if (!g_net_info.initialized || !g_net_info.tx_ready) return 0;

    // 构造一个最小以太帧（60 bytes，不含FCS）
    unsigned char frame[60];
    memset(frame, 0, sizeof(frame));

    // dst: broadcast
    for (int i = 0; i < 6; i++) frame[i] = 0xFF;
    // src: 当前网卡 MAC（若读不到会是 0）
    for (int i = 0; i < 6; i++) frame[6 + i] = g_net_info.mac[i];
    // EtherType: 0x88B5 (实验类型)
    frame[12] = 0x88;
    frame[13] = 0xB5;
    // payload
    const char tag[] = "MYOS-E1000-TX";
    for (unsigned int i = 0; i < sizeof(tag) - 1 && (14 + i) < sizeof(frame); i++) {
        frame[14 + i] = (unsigned char)tag[i];
    }

    return e1000_send_frame(frame, (unsigned short)sizeof(frame));
}

int net_ping_ipv4(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
    if (!g_net_info.initialized || !g_net_info.tx_ready || !g_net_info.rx_ready) return 0;

    unsigned char target_ip[4];
    target_ip[0] = a;
    target_ip[1] = b;
    target_ip[2] = c;
    target_ip[3] = d;

    unsigned char next_hop[4];
    choose_next_hop(target_ip, next_hop);

    unsigned char target_mac[6];
    e1000_rx_drain(64);
    if (!arp_resolve(next_hop, target_mac)) return 0;

    unsigned short ident = 0x4D59; // "MY"
    unsigned short seq = g_icmp_seq++;
    if (seq == 0) seq = g_icmp_seq++;

    if (!send_icmp_echo(target_mac, target_ip, ident, seq)) return 0;
    if (!wait_icmp_reply(target_ip, ident, seq)) return 0;

    g_net_info.ping_ok_count++;
    return 1;
}

int net_dns_query_a(const char* host, unsigned char out_ip[4]) {
    if (!host || !host[0] || !out_ip) return 0;
    if (!g_net_info.initialized || !g_net_info.tx_ready || !g_net_info.rx_ready) return 0;

    unsigned char dns_candidates[2][4];
    int dns_candidate_count = 0;
    dns_candidates[dns_candidate_count][0] = g_dns_ip[0];
    dns_candidates[dns_candidate_count][1] = g_dns_ip[1];
    dns_candidates[dns_candidate_count][2] = g_dns_ip[2];
    dns_candidates[dns_candidate_count][3] = g_dns_ip[3];
    dns_candidate_count++;
    if (!(g_dns_ip[0] == 10 && g_dns_ip[1] == 0 && g_dns_ip[2] == 2 && g_dns_ip[3] == 3)) {
        dns_candidates[dns_candidate_count][0] = 10;
        dns_candidates[dns_candidate_count][1] = 0;
        dns_candidates[dns_candidate_count][2] = 2;
        dns_candidates[dns_candidate_count][3] = 3;
        dns_candidate_count++;
    }

    for (int c = 0; c < dns_candidate_count; c++) {
        unsigned char* dns_ip = dns_candidates[c];
        unsigned char next_hop[4];
        choose_next_hop(dns_ip, next_hop);

        unsigned char dns_mac[6];
        e1000_rx_drain(64);
        if (!arp_resolve(next_hop, dns_mac)) continue;

        unsigned short src_port = (unsigned short)(40000u + ((g_icmp_seq + c) & 0x0FFFu));
        unsigned short txid = (unsigned short)(0xA500u ^ g_ip_ident ^ g_icmp_seq ^ (unsigned short)(c << 8));

        for (int attempt = 0; attempt < 3; attempt++) {
            if (!send_dns_query(dns_mac, src_port, txid, host, dns_ip)) break;
            if (wait_dns_reply(src_port, txid, out_ip)) {
                g_net_info.dns_ok_count++;
                return 1;
            }
            txid++;
        }
    }
    return 0;
}

int net_http_get(const char* host, const char* path, char* out, int out_max, int* out_status_code) {
    if (!host || !host[0] || !out || out_max <= 1) return 0;
    if (!path || !path[0]) path = "/";
    if (!g_net_info.initialized || !g_net_info.tx_ready || !g_net_info.rx_ready) return 0;

    out[0] = '\0';
    if (out_status_code) *out_status_code = 0;

    unsigned char dst_ip[4];
    if (!parse_ipv4_literal(host, dst_ip)) {
        if (!net_dns_query_a(host, dst_ip)) return 0;
    }

    unsigned char next_hop[4];
    choose_next_hop(dst_ip, next_hop);

    unsigned char dst_mac[6];
    if (!arp_resolve(next_hop, dst_mac)) return 0;

    unsigned short src_port = (unsigned short)(43000u + (g_icmp_seq & 0x0FFFu));
    unsigned short dst_port = 80;
    unsigned int client_seq = 0x13572468u ^ ((unsigned int)src_port << 8) ^ g_ip_ident;
    if (client_seq == 0) client_seq = 1;
    unsigned int server_seq_next = 0;
    int connected = 0;

    e1000_rx_drain(128);
    for (int syn_try = 0; syn_try < 3 && !connected; syn_try++) {
        if (!send_tcp_packet(dst_mac, dst_ip, src_port, dst_port, client_seq, 0, TCP_FLAG_SYN, 0, 0)) {
            return 0;
        }

        int syn_timeout = 30000000;
        while (syn_timeout-- > 0) {
            unsigned char* rx = 0;
            unsigned short rx_len = 0;
            unsigned char rx_status = 0;
            unsigned char rx_errors = 0;
            if (!e1000_rx_peek(&rx, &rx_len, &rx_status, &rx_errors)) continue;

            int matched = 0;
            unsigned int seg_seq = 0;
            unsigned int seg_ack = 0;
            unsigned char seg_flags = 0;
            const unsigned char* seg_payload = 0;
            unsigned short seg_payload_len = 0;

            if (rx_errors == 0 && (rx_status & E1000_RX_STATUS_EOP)) {
                matched = parse_tcp_segment_match(rx, rx_len, dst_ip, g_local_ip, dst_port, src_port,
                                                  &seg_seq, &seg_ack, &seg_flags, &seg_payload, &seg_payload_len);
                (void)seg_payload;
                (void)seg_payload_len;
            }

            e1000_rx_pop();

            if (!matched) continue;
            if (seg_flags & TCP_FLAG_RST) return 0;
            if ((seg_flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
                seg_ack == client_seq + 1) {
                server_seq_next = seg_seq + 1;
                connected = 1;
                break;
            }
        }
    }
    if (!connected) return 0;
    g_net_info.tcp_ok_count++;

    client_seq += 1;
    if (!send_tcp_packet(dst_mac, dst_ip, src_port, dst_port, client_seq, server_seq_next, TCP_FLAG_ACK, 0, 0)) {
        return 0;
    }

    char req[512];
    int req_len = 0;
    memset(req, 0, sizeof(req));
    req_len = append_text_buf(req, req_len, (int)sizeof(req), "GET ");
    if (path[0] != '/') req_len = append_text_buf(req, req_len, (int)sizeof(req), "/");
    req_len = append_text_buf(req, req_len, (int)sizeof(req), path);
    req_len = append_text_buf(req, req_len, (int)sizeof(req), " HTTP/1.0\r\nHost: ");
    req_len = append_text_buf(req, req_len, (int)sizeof(req), host);
    req_len = append_text_buf(req, req_len, (int)sizeof(req), "\r\nUser-Agent: myos-curl/0.1\r\nConnection: close\r\n\r\n");
    if (req_len <= 0 || req_len >= (int)sizeof(req)) return 0;

    if (!send_tcp_packet(dst_mac, dst_ip, src_port, dst_port, client_seq, server_seq_next,
                         (unsigned char)(TCP_FLAG_ACK | TCP_FLAG_PSH),
                         (const unsigned char*)req, (unsigned short)req_len)) {
        return 0;
    }
    client_seq += (unsigned int)req_len;

    int out_len = 0;
    int got_data = 0;
    int saw_fin = 0;
    int wait_timeout = 60000000;

    while (wait_timeout-- > 0) {
        unsigned char* rx = 0;
        unsigned short rx_len = 0;
        unsigned char rx_status = 0;
        unsigned char rx_errors = 0;
        if (!e1000_rx_peek(&rx, &rx_len, &rx_status, &rx_errors)) continue;

        int matched = 0;
        unsigned int seg_seq = 0;
        unsigned int seg_ack = 0;
        unsigned char seg_flags = 0;
        const unsigned char* seg_payload = 0;
        unsigned short seg_payload_len = 0;

        if (rx_errors == 0 && (rx_status & E1000_RX_STATUS_EOP)) {
            matched = parse_tcp_segment_match(rx, rx_len, dst_ip, g_local_ip, dst_port, src_port,
                                              &seg_seq, &seg_ack, &seg_flags, &seg_payload, &seg_payload_len);
        }

        e1000_rx_pop();
        if (!matched) continue;
        if (seg_flags & TCP_FLAG_RST) return 0;

        unsigned int accepted = 0;
        if (seg_payload_len > 0) {
            if (seg_seq == server_seq_next) {
                accepted = seg_payload_len;
                int copy_len = seg_payload_len;
                if (copy_len > out_max - 1 - out_len) copy_len = out_max - 1 - out_len;
                if (copy_len > 0) {
                    memcpy(out + out_len, seg_payload, copy_len);
                    out_len += copy_len;
                    out[out_len] = '\0';
                    got_data = 1;
                }
            } else if (seg_seq < server_seq_next) {
                unsigned int overlap = server_seq_next - seg_seq;
                if (overlap < seg_payload_len) {
                    accepted = seg_payload_len - overlap;
                    int copy_len = (int)accepted;
                    if (copy_len > out_max - 1 - out_len) copy_len = out_max - 1 - out_len;
                    if (copy_len > 0) {
                        memcpy(out + out_len, seg_payload + overlap, copy_len);
                        out_len += copy_len;
                        out[out_len] = '\0';
                        got_data = 1;
                    }
                }
            }
        }

        if (accepted > 0) {
            server_seq_next += accepted;
        }

        if (seg_flags & TCP_FLAG_FIN) {
            unsigned int fin_seq = seg_seq + seg_payload_len;
            if (fin_seq == server_seq_next) {
                server_seq_next += 1;
            } else if (fin_seq > server_seq_next) {
                server_seq_next = fin_seq + 1;
            }
            saw_fin = 1;
        }

        if (!send_tcp_packet(dst_mac, dst_ip, src_port, dst_port, client_seq, server_seq_next, TCP_FLAG_ACK, 0, 0)) {
            return 0;
        }

        if (saw_fin) {
            if (!send_tcp_packet(dst_mac, dst_ip, src_port, dst_port, client_seq, server_seq_next,
                                 (unsigned char)(TCP_FLAG_FIN | TCP_FLAG_ACK), 0, 0)) {
                return 0;
            }
            client_seq += 1;
            send_tcp_packet(dst_mac, dst_ip, src_port, dst_port, client_seq, server_seq_next, TCP_FLAG_ACK, 0, 0);
            break;
        }
    }

    if (!got_data) return 0;
    int code = parse_http_status_code(out, out_len);
    if (out_status_code) *out_status_code = code;
    g_net_info.curl_ok_count++;
    return 1;
}
