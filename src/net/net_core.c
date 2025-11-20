/*
 * Network Core Subsystem
 * Packet management and interface handling
 */

#include "net.h"
#include "kernel.h"

// ============================================================================
// GLOBALS
// ============================================================================

static net_interface_t* interfaces = NULL;
static net_interface_t* default_interface = NULL;

// Packet pool (simplified slab allocator for packets)
// In a real kernel, this would use the slab allocator we implemented
static packet_t* packet_pool = NULL; 
static uint32_t packet_count = 0;

// ============================================================================
// PACKET MANAGEMENT
// ============================================================================

packet_t* net_alloc_packet(uint32_t size)
{
    // Allocate packet structure + buffer
    // For now, we just use kmalloc. In production, use a pool.
    packet_t* pkt = kmalloc_tracked(sizeof(packet_t), "net_packet");
    if (!pkt) return NULL;
    
    uint32_t alloc_size = size > NET_MAX_PACKET_SIZE ? size : NET_MAX_PACKET_SIZE;
    pkt->head = kmalloc_tracked(alloc_size, "net_buffer");
    if (!pkt->head) {
        kfree_tracked(pkt);
        return NULL;
    }
    
    pkt->data = pkt->head;
    pkt->tail = pkt->head;
    pkt->end = pkt->head + alloc_size;
    pkt->len = 0;
    pkt->total_len = alloc_size;
    pkt->next = NULL;
    pkt->prev = NULL;
    pkt->netif = NULL;
    
    return pkt;
}

void net_free_packet(packet_t* pkt)
{
    if (!pkt) return;
    
    if (pkt->head) {
        kfree_tracked(pkt->head);
    }
    kfree_tracked(pkt);
}

// ============================================================================
// INTERFACE MANAGEMENT
// ============================================================================

int net_register_interface(net_interface_t* netif)
{
    if (!netif) return -1;
    
    netif->next = interfaces;
    interfaces = netif;
    
    if (!default_interface && !(netif->flags & 0x08)) { // 0x08 = LOOPBACK
        default_interface = netif;
    }
    
    KINFO("NET: Registered interface %s (MAC: %02x:%02x:%02x:%02x:%02x:%02x)",
          netif->name,
          netif->mac_addr.addr[0], netif->mac_addr.addr[1], netif->mac_addr.addr[2],
          netif->mac_addr.addr[3], netif->mac_addr.addr[4], netif->mac_addr.addr[5]);
          
    return 0;
}

net_interface_t* net_get_interface(const char* name)
{
    net_interface_t* curr = interfaces;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

net_interface_t* net_get_default_interface(void)
{
    return default_interface;
}

// ============================================================================
// PACKET FLOW
// ============================================================================

int net_rx_packet(net_interface_t* netif, packet_t* pkt)
{
    if (!netif || !pkt) return -1;
    
    netif->rx_packets++;
    netif->rx_bytes += pkt->len;
    pkt->netif = netif;
    
    // Pass to Ethernet layer
    return ethernet_input(netif, pkt);
}

int net_tx_packet(net_interface_t* netif, packet_t* pkt)
{
    if (!netif || !pkt) return -1;
    
    // If interface has a send function, call it
    if (netif->send_packet) {
        netif->tx_packets++;
        netif->tx_bytes += pkt->len;
        return netif->send_packet(netif, pkt);
    }
    
    return -1;
}

// ============================================================================
// LOOPBACK DEVICE
// ============================================================================

static int loopback_send(net_interface_t* netif, packet_t* pkt)
{
    // Loopback just feeds it back into RX
    // We need to clone it or be careful about ownership.
    // For simplicity, we'll just pass it back (assuming caller gives up ownership)
    
    // In a real stack, we'd queue this to run in a softirq context
    // to avoid stack overflow. Here we recurse directly for simplicity.
    
    KDEBUG("LOOPBACK: Bouncing packet %d bytes", pkt->len);
    return net_rx_packet(netif, pkt);
}

static net_interface_t loopback_if;

void net_init_loopback(void)
{
    memset(&loopback_if, 0, sizeof(net_interface_t));
    strcpy(loopback_if.name, "lo");
    loopback_if.ip_addr = 0x7F000001; // 127.0.0.1
    loopback_if.netmask = 0xFF000000; // 255.0.0.0
    loopback_if.flags = 0x09; // UP | LOOPBACK
    loopback_if.send_packet = loopback_send;
    
    net_register_interface(&loopback_if);
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void net_init(void)
{
    KINFO("Initializing Network Subsystem...");
    
    net_init_loopback();
    tcp_init();
    
    KINFO("Network Subsystem Initialized.");
}

// ============================================================================
// UTILS
// ============================================================================

uint16_t htons(uint16_t v) {
    return (v >> 8) | (v << 8);
}

uint16_t ntohs(uint16_t v) {
    return htons(v);
}

uint32_t htonl(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | 
           ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
}

uint32_t ntohl(uint32_t v) {
    return htonl(v);
}

char* ip_to_str(ip_addr_t ip, char* buf)
{
    // IP is stored in host byte order in our struct for simplicity,
    // but usually it's network order. Let's assume host order here.
    // 0x7F000001 -> 127.0.0.1
    
    uint8_t* p = (uint8_t*)&ip;
    // On Little Endian x86:
    // 0x7F000001 stored as 01 00 00 7F
    // So p[0]=1, p[3]=127.
    // We want to print MSB first (127).
    
    // Wait, standard is network byte order (Big Endian).
    // If we store as 0x7F000001 literal, on LE it is 01 00 00 7F.
    // Let's assume we store in Network Byte Order everywhere to be standard.
    
    // If ip is 127.0.0.1, in NBO it is 0x7F000001.
    // On LE machine, that int is read as 0x0100007F? No.
    // 127.0.0.1 -> bytes 127, 0, 0, 1.
    // In NBO (Big Endian), that is 0x7F000001.
    
    sprintf(buf, "%d.%d.%d.%d",
            (ip >> 24) & 0xFF,
            (ip >> 16) & 0xFF,
            (ip >> 8) & 0xFF,
            ip & 0xFF);
            
    return buf;
}
