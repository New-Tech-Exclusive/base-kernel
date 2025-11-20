/*
 * IPv4 Layer
 * Packet routing and ICMP
 */

#include "net.h"
#include "kernel.h"

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// IPv4 and ICMP Headers defined in net.h

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

// ============================================================================
// CHECKSUM
// ============================================================================

uint16_t checksum(void* data, uint32_t len)
{
    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)data;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if (len > 0) {
        sum += *(uint8_t*)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

// ============================================================================
// ICMP IMPLEMENTATION
// ============================================================================

static int icmp_input(net_interface_t* netif, packet_t* pkt)
{
    if (pkt->len < sizeof(icmp_header_t)) return -1;
    
    icmp_header_t* icmp = (icmp_header_t*)pkt->data;
    
    if (icmp->type == ICMP_ECHO_REQUEST) {
        KDEBUG("ICMP: Echo Request from %s", "remote"); // Need to pass src ip
        
        // Reuse packet for reply
        // Swap IPs is handled by ipv4_output logic usually, but here we reuse buffer
        
        // We need to send a reply.
        // Allocate new packet for reply to be clean
        packet_t* reply = net_alloc_packet(NET_MAX_PACKET_SIZE);
        if (!reply) return -1;
        
        // Copy payload
        uint32_t payload_len = pkt->len - sizeof(icmp_header_t);
        
        // Reserve headers
        reply->data += sizeof(eth_header_t) + sizeof(ipv4_header_t);
        
        icmp_header_t* rep_icmp = (icmp_header_t*)reply->data;
        rep_icmp->type = ICMP_ECHO_REPLY;
        rep_icmp->code = 0;
        rep_icmp->id = icmp->id;
        rep_icmp->sequence = icmp->sequence;
        rep_icmp->checksum = 0;
        
        // Copy data
        memcpy(reply->data + sizeof(icmp_header_t), 
               pkt->data + sizeof(icmp_header_t), payload_len);
               
        reply->len = sizeof(icmp_header_t) + payload_len;
        
        // Calculate checksum
        rep_icmp->checksum = checksum(rep_icmp, reply->len);
        
        // Get source IP from original IP header (which is in l3_header)
        ipv4_header_t* orig_ip = (ipv4_header_t*)pkt->l3_header;
        
        return ipv4_output(reply, orig_ip->src_ip, IPPROTO_ICMP);
    }
    
    return 0;
}

// ============================================================================
// IPv4 IMPLEMENTATION
// ============================================================================

int ipv4_input(net_interface_t* netif, packet_t* pkt)
{
    if (pkt->len < sizeof(ipv4_header_t)) return -1;
    
    ipv4_header_t* ip = (ipv4_header_t*)pkt->data;
    pkt->l3_header = ip;
    
    if (ip->version != 4) return -1;
    
    uint32_t header_len = ip->ihl * 4;
    if (pkt->len < header_len) return -1;
    
    // Check destination
    // Accept if it matches interface IP, or is broadcast, or loopback
    if (ip->dest_ip != netif->ip_addr && 
        ip->dest_ip != 0xFFFFFFFF && 
        netif->ip_addr != 0) { // 0.0.0.0 accepts everything (DHCP)
        
        // Not for us. In a router, we would forward here.
        return 0;
    }
    
    // Strip header
    pkt->data += header_len;
    pkt->len -= header_len;
    
    // Dispatch
    switch (ip->protocol) {
        case IPPROTO_ICMP:
            return icmp_input(netif, pkt);
        case IPPROTO_TCP:
            return tcp_input(netif, pkt);
        case IPPROTO_UDP:
            return udp_input(netif, pkt);
        default:
            // KDEBUG("IPv4: Unknown protocol %d", ip->protocol);
            return 0;
    }
}

int ipv4_output(packet_t* pkt, ip_addr_t dest_ip, uint8_t protocol)
{
    // Route lookup (simplified)
    net_interface_t* netif = net_get_default_interface();
    
    // If loopback destination
    if ((dest_ip & 0xFF000000) == 0x7F000000) { // 127.x.x.x
        netif = net_get_interface("lo");
    }
    
    if (!netif) {
        KERROR("IPv4: No route to host");
        net_free_packet(pkt);
        return -1;
    }
    
    // Prepend IP header
    if (pkt->data - pkt->head < sizeof(ipv4_header_t)) {
        KERROR("IPv4: No headroom");
        net_free_packet(pkt);
        return -1;
    }
    
    pkt->data -= sizeof(ipv4_header_t);
    pkt->len += sizeof(ipv4_header_t);
    
    ipv4_header_t* ip = (ipv4_header_t*)pkt->data;
    ip->version = 4;
    ip->ihl = 5;
    ip->tos = 0;
    ip->total_len = htons(pkt->len);
    ip->id = htons(0); // Should increment
    ip->frag_off = htons(0x4000); // Don't fragment
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->src_ip = netif->ip_addr;
    ip->dest_ip = dest_ip;
    ip->checksum = 0;
    ip->checksum = checksum(ip, sizeof(ipv4_header_t));
    
    // Resolve MAC
    mac_addr_t dest_mac;
    
    if (netif->flags & 0x08) { // Loopback
        memset(dest_mac.addr, 0, 6);
    } else {
        // ARP Lookup
        // If local subnet, resolve dest_ip. If remote, resolve gateway.
        ip_addr_t next_hop = dest_ip;
        // Simplified subnet check
        if ((dest_ip & netif->netmask) != (netif->ip_addr & netif->netmask)) {
            next_hop = netif->gateway;
        }
        
        // Need ARP lookup function exposed
        extern int arp_lookup(ip_addr_t ip, mac_addr_t* mac);
        if (arp_lookup(next_hop, &dest_mac) < 0) {
            // ARP miss - should queue packet and send ARP request
            // For now, just drop and warn
            KWARN("IPv4: ARP miss for %x", next_hop);
            net_free_packet(pkt);
            return -1;
        }
    }
    
    return ethernet_output(netif, pkt, dest_mac, ETH_P_IP);
}
