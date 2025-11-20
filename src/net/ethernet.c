/*
 * Ethernet Layer
 * Frame processing and ARP
 */

#include "net.h"
#include "kernel.h"

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Ethernet Header defined in net.h

// ARP Header
typedef struct {
    uint16_t hw_type;      // Hardware type (1 = Ethernet)
    uint16_t proto_type;   // Protocol type (IPv4 = 0x0800)
    uint8_t hw_len;        // Hardware address length (6)
    uint8_t proto_len;     // Protocol address length (4)
    uint16_t opcode;       // Opcode (1=request, 2=reply)
    uint8_t sender_hw[6];  // Sender MAC
    uint32_t sender_ip;    // Sender IP
    uint8_t target_hw[6];  // Target MAC
    uint32_t target_ip;    // Target IP
} __attribute__((packed)) arp_header_t;

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

// ARP Cache Entry
typedef struct {
    ip_addr_t ip;
    mac_addr_t mac;
    uint64_t timestamp;
    int valid;
} arp_entry_t;

#define ARP_CACHE_SIZE 64
static arp_entry_t arp_cache[ARP_CACHE_SIZE];

// ============================================================================
// ARP IMPLEMENTATION
// ============================================================================

void arp_update_cache(ip_addr_t ip, uint8_t* mac)
{
    // Update existing
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac.addr, mac, 6);
            arp_cache[i].timestamp = sys_get_ticks();
            return;
        }
    }
    
    // Add new (find empty or oldest)
    int idx = -1;
    uint64_t oldest = -1;
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            idx = i;
            break;
        }
        if (arp_cache[i].timestamp < oldest) {
            oldest = arp_cache[i].timestamp;
            idx = i;
        }
    }
    
    if (idx >= 0) {
        arp_cache[idx].ip = ip;
        memcpy(arp_cache[idx].mac.addr, mac, 6);
        arp_cache[idx].timestamp = sys_get_ticks();
        arp_cache[idx].valid = 1;
    }
}

int arp_lookup(ip_addr_t ip, mac_addr_t* mac)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            *mac = arp_cache[i].mac;
            return 0;
        }
    }
    return -1;
}

static int arp_input(net_interface_t* netif, packet_t* pkt)
{
    if (pkt->len < sizeof(arp_header_t)) return -1;
    
    arp_header_t* arp = (arp_header_t*)pkt->data;
    
    uint16_t hw_type = ntohs(arp->hw_type);
    uint16_t proto_type = ntohs(arp->proto_type);
    uint16_t opcode = ntohs(arp->opcode);
    
    if (hw_type != 1 || proto_type != ETH_P_IP) return -1;
    
    // Update cache with sender info
    arp_update_cache(arp->sender_ip, arp->sender_hw);
    
    // If it's a request for us, send reply
    if (opcode == ARP_OP_REQUEST && arp->target_ip == netif->ip_addr) {
        KDEBUG("ARP: Request for %x from %x", arp->target_ip, arp->sender_ip);
        
        packet_t* reply = net_alloc_packet(sizeof(eth_header_t) + sizeof(arp_header_t));
        if (!reply) return -1;
        
        // Reserve space for Ethernet header
        reply->data += sizeof(eth_header_t);
        reply->len = sizeof(arp_header_t);
        
        arp_header_t* rep_arp = (arp_header_t*)reply->data;
        rep_arp->hw_type = htons(1);
        rep_arp->proto_type = htons(ETH_P_IP);
        rep_arp->hw_len = 6;
        rep_arp->proto_len = 4;
        rep_arp->opcode = htons(ARP_OP_REPLY);
        
        // My info
        memcpy(rep_arp->sender_hw, netif->mac_addr.addr, 6);
        rep_arp->sender_ip = netif->ip_addr;
        
        // Target info (requester)
        memcpy(rep_arp->target_hw, arp->sender_hw, 6);
        rep_arp->target_ip = arp->sender_ip;
        
        // Send it
        mac_addr_t dest_mac;
        memcpy(dest_mac.addr, arp->sender_hw, 6);
        ethernet_output(netif, reply, dest_mac, ETH_P_ARP);
    }
    
    return 0;
}

// ============================================================================
// ETHERNET IMPLEMENTATION
// ============================================================================

int ethernet_input(net_interface_t* netif, packet_t* pkt)
{
    if (pkt->len < sizeof(eth_header_t)) {
        return -1;
    }
    
    eth_header_t* eth = (eth_header_t*)pkt->data;
    pkt->l2_header = eth;
    
    // Strip header
    pkt->data += sizeof(eth_header_t);
    pkt->len -= sizeof(eth_header_t);
    
    uint16_t type = ntohs(eth->type);
    pkt->protocol = type;
    
    // Dispatch based on type
    switch (type) {
        case ETH_P_IP:
            return ipv4_input(netif, pkt);
        case ETH_P_ARP:
            return arp_input(netif, pkt);
        case ETH_P_IPV6:
            // IPv6 not implemented yet
            return 0;
        default:
            // KDEBUG("ETH: Unknown protocol type %04x", type);
            return 0;
    }
}

int ethernet_output(net_interface_t* netif, packet_t* pkt, mac_addr_t dest_mac, uint16_t type)
{
    // Prepend Ethernet header
    if (pkt->data - pkt->head < sizeof(eth_header_t)) {
        KERROR("ETH: Not enough headroom for header");
        return -1;
    }
    
    pkt->data -= sizeof(eth_header_t);
    pkt->len += sizeof(eth_header_t);
    
    eth_header_t* eth = (eth_header_t*)pkt->data;
    memcpy(eth->dest, dest_mac.addr, 6);
    memcpy(eth->src, netif->mac_addr.addr, 6);
    eth->type = htons(type);
    
    return net_tx_packet(netif, pkt);
}
