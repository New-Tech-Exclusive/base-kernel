/*
 * UDP Layer
 * User Datagram Protocol
 */

#include "net.h"
#include "kernel.h"

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

// ============================================================================
// UDP IMPLEMENTATION
// ============================================================================

int udp_input(net_interface_t* netif, packet_t* pkt)
{
    if (pkt->len < sizeof(udp_header_t)) return -1;
    
    udp_header_t* udp = (udp_header_t*)pkt->data;
    pkt->l4_header = udp;
    
    // Strip header
    pkt->data += sizeof(udp_header_t);
    pkt->len -= sizeof(udp_header_t);
    
    uint16_t dest_port = ntohs(udp->dest_port);
    
    KDEBUG("UDP: Packet received for port %d", dest_port);
    
    // Find socket bound to this port and queue packet
    // (Socket layer not implemented in this step)
    
    return 0;
}

int udp_send(sockaddr_in_t* src, sockaddr_in_t* dest, void* data, uint32_t len)
{
    packet_t* pkt = net_alloc_packet(NET_MAX_PACKET_SIZE);
    if (!pkt) return -1;
    
    // Reserve headers
    pkt->data += sizeof(eth_header_t) + sizeof(ipv4_header_t) + sizeof(udp_header_t);
    
    // Copy data
    memcpy(pkt->data, data, len);
    pkt->len = len;
    
    // Prepend UDP header
    pkt->data -= sizeof(udp_header_t);
    pkt->len += sizeof(udp_header_t);
    
    udp_header_t* udp = (udp_header_t*)pkt->data;
    udp->src_port = htons(src->port);
    udp->dest_port = htons(dest->port);
    udp->length = htons(len + sizeof(udp_header_t));
    udp->checksum = 0; // Optional in IPv4
    
    return ipv4_output(pkt, dest->ip, IPPROTO_UDP);
}
