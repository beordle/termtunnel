/**
 * Copyright (c) 2022 Jindong Zhang
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef TERMTUNNEL_LWIPOPTS_H
#define TERMTUNNEL_LWIPOPTS_H
#include "config.h"
#define LWIP_COMPAT_SOCKETS 0
#define LWIP_POSIX_SOCKETS_IO_NAMES 0
#define MEMP_MEM_MALLOC 1
#define MEM_LIBC_MALLOC 1

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_DBG_MIN_LEVEL 0
#define TAPIF_DEBUG LWIP_DBG_ON
#define TUNIF_DEBUG LWIP_DBG_OFF
#define UNIXIF_DEBUG LWIP_DBG_OFF
#define DELIF_DEBUG LWIP_DBG_OFF
#define SIO_FIFO_DEBUG LWIP_DBG_OFF
#define TCPDUMP_DEBUG LWIP_DBG_ON

#define PPP_DEBUG LWIP_DBG_OFF
#define MEM_DEBUG LWIP_DBG_OFF
#define MEMP_DEBUG LWIP_DBG_OFF
#define PBUF_DEBUG LWIP_DBG_OFF
#define API_LIB_DEBUG LWIP_DBG_ON
#define API_MSG_DEBUG LWIP_DBG_ON
#define TCPIP_DEBUG LWIP_DBG_ON
#define NETIF_DEBUG LWIP_DBG_ON
#define SOCKETS_DEBUG LWIP_DBG_ON
#define DEMO_DEBUG LWIP_DBG_ON
#define IP_DEBUG LWIP_DBG_ON
#define IP_REASS_DEBUG LWIP_DBG_ON
#define RAW_DEBUG LWIP_DBG_ON
#define ICMP_DEBUG LWIP_DBG_ON
#define UDP_DEBUG LWIP_DBG_ON
#define TCP_DEBUG LWIP_DBG_ON
#define TCP_INPUT_DEBUG LWIP_DBG_ON
#define TCP_OUTPUT_DEBUG LWIP_DBG_ON
#define TCP_RTO_DEBUG LWIP_DBG_ON
#define TCP_CWND_DEBUG LWIP_DBG_ON
#define TCP_WND_DEBUG LWIP_DBG_ON
#define TCP_FR_DEBUG LWIP_DBG_ON
#define TCP_QLEN_DEBUG LWIP_DBG_ON
#define TCP_RST_DEBUG LWIP_DBG_ON

extern unsigned char debug_flags;
#define LWIP_DBG_TYPES_ON debug_flags

#define NO_SYS 0
#define LWIP_SOCKET (NO_SYS == 0)
#define LWIP_NETCONN (NO_SYS == 0)

#define ETHARP_TRUST_IP_MAC 0

/* ---------- Memory options ---------- */
/* MEM_ALIGNMENT: should be set to the alignment of the CPU for which
   lwIP is compiled. 4 byte alignment -> define MEM_ALIGNMENT to 4, 2
   byte alignment -> define MEM_ALIGNMENT to 2. */
/* MSVC port: intel processors don't need 4-byte alignment,
   but are faster that way! */
#define MEM_ALIGNMENT 4

/* MEM_SIZE: the size of the heap memory. If the application will send
a lot of data that needs to be copied, this should be set high. */
#define MEM_SIZE 819200000

//修改
/* MEMP_NUM_PBUF: the number of memp struct pbufs. If the application
   sends a lot of data out of ROM (or other static memory), this
   should be set high. */
#define MEMP_NUM_PBUF 200000
/* MEMP_NUM_RAW_PCB: the number of UDP protocol control blocks. One
   per active RAW "connection". */
#define MEMP_NUM_RAW_PCB 3
/* MEMP_NUM_UDP_PCB: the number of UDP protocol control blocks. One
   per active UDP "connection". */
#define MEMP_NUM_UDP_PCB 4
/* MEMP_NUM_TCP_PCB: the number of simulatenously active TCP
   connections. */
#define MEMP_NUM_TCP_PCB 320
/* MEMP_NUM_TCP_PCB_LISTEN: the number of listening TCP
   connections. */
#define MEMP_NUM_TCP_PCB_LISTEN 32
/* MEMP_NUM_TCP_SEG: the number of simultaneously queued TCP
   segments. */
#define MEMP_NUM_TCP_SEG 32000
// 修改
/* MEMP_NUM_SYS_TIMEOUT: the number of simulateously active
   timeouts. */
#define MEMP_NUM_SYS_TIMEOUT 10

/* The following four are used only with the sequential API and can be
   set to 0 if the application only will use the raw API. */
/* MEMP_NUM_NETBUF: the number of struct netbufs. */
#define MEMP_NUM_NETBUF 200000
/* MEMP_NUM_NETCONN: the number of struct netconns. */
#ifdef __CYGWIN__
#define MEMP_NUM_NETCONN 20
#else
#define MEMP_NUM_NETCONN 200  // number of socket
#endif
/* MEMP_NUM_TCPIP_MSG_*: the number of struct tcpip_msg, which is used
   for sequential API communication and incoming packets. Used in
   src/api/tcpip.c. */

#define LWIP_NETCONN_SEM_PER_THREAD 0
#define LWIP_NETCONN_FULLDUPLEX 0
#define MEMP_NUM_TCPIP_MSG_API 160000
#define MEMP_NUM_TCPIP_MSG_INPKT 160000

/* ---------- Pbuf options ---------- */
/* PBUF_POOL_SIZE: the number of buffers in the pbuf pool. */
#define PBUF_POOL_SIZE (120 * 4000)

/* PBUF_POOL_BUFSIZE: the size of each pbuf in the pbuf pool. */
#define PBUF_POOL_BUFSIZE (128 * 10)

/* PBUF_LINK_HLEN: the number of bytes that should be allocated for a
   link level header. */
#define PBUF_LINK_HLEN 16

/** SYS_LIGHTWEIGHT_PROT
 * define SYS_LIGHTWEIGHT_PROT in lwipopts.h if you want inter-task protection
 * for certain critical regions during buffer allocation, deallocation and
 * memory allocation and deallocation.
 */
#define SYS_LIGHTWEIGHT_PROT 1

/* ---------- TCP options ---------- */
#define LWIP_TCP 1
#define TCP_TTL 255

#define LWIP_TCP_KEEPALIVE  1
/* Controls if TCP should queue segments that arrive out of
   order. Define to 0 if your device is low on memory. */
#define TCP_QUEUE_OOSEQ 1

// MTU - IP header - TCP header
#define TCP_MSS 1500
//(VIR_MTU-40)

/* TCP sender buffer space (bytes). */
#define TCP_SND_BUF (65535)

/* TCP sender buffer space (pbufs). This must be at least = 2 *
   TCP_SND_BUF/TCP_MSS for things to work. */
#define TCP_SND_QUEUELEN (8 * TCP_SND_BUF / TCP_MSS)

/* TCP writable space (bytes). This must be less than or equal
   to TCP_SND_BUF. It is the amount of space which must be
   available in the tcp snd_buf for select to return writable */
#define TCP_SNDLOWAT (TCP_SND_BUF / 2)

/* TCP receive window. */
#define TCP_WND (65535 * 1000)
#define LWIP_WND_SCALE 1
#define TCP_RCV_SCALE 10
/* Maximum number of retransmissions of data segments. */
#define TCP_MAXRTX 6

/* Maximum number of retransmissions of SYN segments. */
#define TCP_SYNMAXRTX 4

/* ---------- ARP options ---------- */
#define LWIP_ARP 1
#define ARP_TABLE_SIZE 10
#define ARP_QUEUEING 1

/* ---------- IP options ---------- */
/* Define IP_FORWARD to 1 if you wish to have the ability to forward
   IP packets across network interfaces. If you are going to run lwIP
   on a device with only one network interface, define this to 0. */
#define IP_FORWARD 1

/* IP reassembly and segmentation.These are orthogonal even
 * if they both deal with IP fragments */
#define IP_REASSEMBLY 1
#define IP_REASS_MAX_PBUFS 40
#define MEMP_NUM_REASSDATA 40
#define IP_FRAG 1

/* ---------- ICMP options ---------- */
#define ICMP_TTL 255

/* ---------- DHCP options ---------- */
/* Define LWIP_DHCP to 1 if you want DHCP configuration of
   interfaces. */
#define LWIP_DHCP 0

/* 1 if you want to do an ARP check on the offered address
   (recommended if using DHCP). */
#define DHCP_DOES_ARP_CHECK (LWIP_DHCP)

/* ---------- AUTOIP options ------- */
#define LWIP_AUTOIP 0

/* ---------- SNMP options ---------- */
/** @todo SNMP is experimental for now
    @note UDP must be available for SNMP transport */
#ifndef LWIP_SNMP
#define LWIP_SNMP 0
#endif

#ifndef SNMP_PRIVATE_MIB
#define SNMP_PRIVATE_MIB 0
#endif

/* ---------- UDP options ---------- */
#define LWIP_UDP 0
#define UDP_TTL 255

/* ---------- RAW options ---------- */
#define LWIP_RAW 0
#define RAW_TTL 255

/* ---------- Statistics options ---------- */
/* individual STATS options can be turned off by defining them to 0
 * (e.g #define TCP_STATS 0). All of them are turned off if LWIP_STATS
 * is 0
 * */

#define LWIP_STATS 1
#define PPP_SUPPORT 0 
#endif
