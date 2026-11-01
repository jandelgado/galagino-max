#ifndef GALAGINO_LWIPOPTS_H
#define GALAGINO_LWIPOPTS_H

// lwIP for the game loader of remote builds (hal/rp2350/wifi.cpp): DHCP,
// one TCP socket downloading at a time. tcpip thread under FreeRTOS, its
// core lock is the CYW43 async_context lock (pico_lwip_freertos). Memory is
// static (~20 KB): lwIP has no deinit, its timers keep running between
// downloads.

#define NO_SYS                      0
#define LWIP_SOCKET                 1
#define LWIP_NETCONN                1
#define LWIP_TIMEVAL_PRIVATE        0  // newlib's struct timeval
#define LWIP_SO_RCVTIMEO            1
#define LWIP_TCPIP_CORE_LOCKING     1
#define LWIP_TCPIP_CORE_LOCKING_INPUT 1

#define TCPIP_THREAD_NAME           "tcpip"
#define TCPIP_THREAD_STACKSIZE      2048  // bytes
#define TCPIP_THREAD_PRIO           2
#define TCPIP_MBOX_SIZE             8
#define DEFAULT_THREAD_STACKSIZE    1024
#define DEFAULT_RAW_RECVMBOX_SIZE   8
#define DEFAULT_UDP_RECVMBOX_SIZE   8
#define DEFAULT_TCP_RECVMBOX_SIZE   8
#define DEFAULT_ACCEPTMBOX_SIZE     8

#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define MEMP_NUM_TCP_SEG            16
#define MEMP_NUM_ARP_QUEUE          4
#define MEMP_NUM_NETCONN            4
#define MEMP_NUM_TCP_PCB            2
#define MEMP_NUM_UDP_PCB            2
#define PBUF_POOL_SIZE              8

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    0
#define LWIP_DHCP                   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_TCP_KEEPALIVE          0
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define LWIP_CHKSUM_ALGORITHM       3

// receive window of four segments: the loader writes flash between reads
#define TCP_MSS                     1460
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (2 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0
#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0

#endif // GALAGINO_LWIPOPTS_H
