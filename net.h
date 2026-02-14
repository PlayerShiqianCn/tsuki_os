#ifndef NET_H
#define NET_H

typedef struct {
    int present;
    int initialized;
    int tx_ready;
    int rx_ready;
    unsigned int tx_ok_count;
    unsigned int rx_ok_count;
    unsigned int ping_ok_count;
    unsigned int dns_ok_count;
    unsigned int tcp_ok_count;
    unsigned int curl_ok_count;
    unsigned short vendor_id;
    unsigned short device_id;
    unsigned char bus;
    unsigned char slot;
    unsigned char func;
    unsigned char irq_line;
    unsigned char mac[6];
} NetDriverInfo;

void net_init(void);
const NetDriverInfo* net_get_info(void);
int net_send_test_frame(void);
int net_ping_ipv4(unsigned char a, unsigned char b, unsigned char c, unsigned char d);
int net_dns_query_a(const char* host, unsigned char out_ip[4]);
void net_set_local_ip(unsigned char a, unsigned char b, unsigned char c, unsigned char d);
void net_get_local_ip(unsigned char out_ip[4]);
void net_set_gateway(unsigned char a, unsigned char b, unsigned char c, unsigned char d);
void net_get_gateway(unsigned char out_ip[4]);
void net_set_dns_server(unsigned char a, unsigned char b, unsigned char c, unsigned char d);
void net_get_dns_server(unsigned char out_ip[4]);
int net_http_get(const char* host, const char* path, char* out, int out_max, int* out_status_code);

#endif
