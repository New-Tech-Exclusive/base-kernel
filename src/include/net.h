/*
 * Network Stack Header
 * Core definitions for the kernel networking subsystem
 */

#ifndef NET_H
#define NET_H

#include "kernel.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

#define NET_MAX_PACKET_SIZE    2048
#define NET_PACKET_POOL_SIZE   1024

// Protocol constants
#define ETH_P_IP   0x0800
#define ETH_P_ARP  0x0806
#define ETH_P_IPV6 0x86DD

#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

// Ethernet Header
typedef struct {
    uint8_t dest[6];
    uint8_t src[6];
    uint16_t type;
} __attribute__((packed)) eth_header_t;

// IPv4 Header
typedef struct {
    uint8_t ihl : 4;
    uint8_t version : 4;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed)) ipv4_header_t;

// ICMP Header
typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed)) icmp_header_t;
typedef struct {
    uint8_t addr[6];
} __attribute__((packed)) mac_addr_t;

// IP Address (IPv4)
typedef uint32_t ip_addr_t;

// Network Packet Buffer (similar to sk_buff in Linux)
typedef struct packet {
    struct packet* next;       // Linked list for queues
    struct packet* prev;
    
    uint8_t* head;             // Start of buffer
    uint8_t* data;             // Current data pointer
    uint8_t* tail;             // End of data
    uint8_t* end;              // End of buffer
    
    uint32_t len;              // Data length
    uint32_t total_len;        // Total buffer length
    
    struct net_interface* netif; // Receiving/Sending interface
    uint16_t protocol;         // Ethernet protocol type
    
    // Layer headers (pointers into data)
    void* l2_header;           // Ethernet header
    void* l3_header;           // IP header
    void* l4_header;           // TCP/UDP header
} packet_t;

// Network Interface
typedef struct net_interface {
    char name[16];
    mac_addr_t mac_addr;
    ip_addr_t ip_addr;
    ip_addr_t netmask;
    ip_addr_t gateway;
    
    uint32_t flags;            // UP, RUNNING, LOOPBACK, etc.
    
    // Driver callbacks
    int (*send_packet)(struct net_interface* netif, packet_t* pkt);
    
    // Stats
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
    
    struct net_interface* next;
} net_interface_t;

// Socket Address
typedef struct {
    ip_addr_t ip;
    uint16_t port;
} sockaddr_in_t;

// ============================================================================
// CORE FUNCTIONS (net_core.c)
// ============================================================================

void net_init(void);
packet_t* net_alloc_packet(uint32_t size);
void net_free_packet(packet_t* pkt);
int net_register_interface(net_interface_t* netif);
net_interface_t* net_get_interface(const char* name);
net_interface_t* net_get_default_interface(void);
int net_rx_packet(net_interface_t* netif, packet_t* pkt);
int net_tx_packet(net_interface_t* netif, packet_t* pkt);

// ============================================================================
// ETHERNET (ethernet.c)
// ============================================================================

int ethernet_input(net_interface_t* netif, packet_t* pkt);
int ethernet_output(net_interface_t* netif, packet_t* pkt, mac_addr_t dest_mac, uint16_t type);

// ============================================================================
// IPv4 (ipv4.c)
// ============================================================================

int ipv4_input(net_interface_t* netif, packet_t* pkt);
int ipv4_output(packet_t* pkt, ip_addr_t dest_ip, uint8_t protocol);
uint16_t checksum(void* data, uint32_t len);

// ============================================================================
// UDP (udp.c)
// ============================================================================

int udp_input(net_interface_t* netif, packet_t* pkt);
int udp_send(sockaddr_in_t* src, sockaddr_in_t* dest, void* data, uint32_t len);

// ============================================================================
// TCP (tcp.c)
// ============================================================================

void tcp_init(void);
int tcp_input(net_interface_t* netif, packet_t* pkt);

// ============================================================================
// UTILS
// ============================================================================

uint16_t htons(uint16_t hostshort);
uint16_t ntohs(uint16_t netshort);
uint32_t htonl(uint32_t hostlong);
uint32_t ntohl(uint32_t netlong);
char* ip_to_str(ip_addr_t ip, char* buf);

#endif // NET_H
