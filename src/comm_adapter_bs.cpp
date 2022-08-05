#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <fstream>
#include <sys/time.h>
#include <sched.h>
#include <unistd.h>
#include <sstream>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <unistd.h>
#include <linux/if_ether.h>
#include "zlib.h"
#include "lz4.h"

// #define COMM_DEBUG // Activate it to debug
#define COMM_TEST // Activate it to test the program

#define PERR(txt, par...) \
    printf("ERROR: (%s / %s): " txt "\n", __FILE__, __FUNCTION__, ##par)
#define PERRNO(txt) \
    printf("ERROR: (%s / %s): " txt ": %s\n", __FILE__, __FUNCTION__, strerror(errno))

// Compress data
#define UNCOMPRESS 0
#define ZLIB_COMPRESS 1
#define LZ4_COMPRESS 2

// Program frequency (Hz)
#define FREQUENCY 50

// Socket
typedef struct multiSocket_tag
{
    struct sockaddr_in destAddress;
    int socketID;
    bool compressedData;
} multiSocket_t;
typedef struct nw_config
{
    char multicast_ip[16];
    char *iface;
    unsigned int port;
    uint8_t compress_type;
} config;
multiSocket_t multiSocket;

// Config
config nw_config;

// New thread for receive data
pthread_t recv_thread;
pthread_attr_t thread_attr;
uint8_t end_signal_counter;

// Data
char data[128] = "3123456908its";
unsigned long int actual_data_size = 15;
unsigned long int recv_data_size;
unsigned long int max_recv_data_size = 128;
char recv_data[128];

int if_NameToIndex(char *ifname, char *address)
{
    int fd;
    struct ifreq if_info;
    int if_index;

    memset(&if_info, 0, sizeof(if_info));
    strncpy(if_info.ifr_name, ifname, IFNAMSIZ - 1);

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        PERRNO("socket");
        return -1;
    }
    if (ioctl(fd, SIOCGIFINDEX, &if_info) == -1)
    {
        PERRNO("ioctl");
        close(fd);
        return -1;
    }
    if_index = if_info.ifr_ifindex;

    if (ioctl(fd, SIOCGIFADDR, &if_info) == -1)
    {
        PERRNO("ioctl");
        close(fd);
        return -1;
    }

    close(fd);

    sprintf(address, "%d.%d.%d.%d\n",
            (int)((unsigned char *)if_info.ifr_hwaddr.sa_data)[2],
            (int)((unsigned char *)if_info.ifr_hwaddr.sa_data)[3],
            (int)((unsigned char *)if_info.ifr_hwaddr.sa_data)[4],
            (int)((unsigned char *)if_info.ifr_hwaddr.sa_data)[5]);
#ifdef COMM_DEBUG
    printf("**** Using device %s -> Ethernet %s\n", if_info.ifr_name, address);
#endif

    return if_index;
}
int openSocket()
{
    struct sockaddr_in multicastAddress;
    struct ip_mreqn mreqn;
    struct ip_mreq mreq;
    int opt;
    char address[16]; // IPV4: xxx.xxx.xxx.xxx\0

    bzero(&multicastAddress, sizeof(struct sockaddr_in));
    multicastAddress.sin_family = AF_INET;
    multicastAddress.sin_port = htons(nw_config.port);
    multicastAddress.sin_addr.s_addr = INADDR_ANY;

    bzero(&multiSocket.destAddress, sizeof(struct sockaddr_in));
    multiSocket.destAddress.sin_family = AF_INET;
    multiSocket.destAddress.sin_port = htons(nw_config.port);
    multiSocket.destAddress.sin_addr.s_addr = inet_addr(nw_config.multicast_ip);

    if ((multiSocket.socketID = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        PERRNO("socket");
        return -1;
    }

    memset((void *)&mreqn, 0, sizeof(mreqn));
    mreqn.imr_ifindex = if_NameToIndex(nw_config.iface, address);
    if ((setsockopt(multiSocket.socketID, SOL_IP, IP_MULTICAST_IF, &mreqn, sizeof(mreqn))) == -1)
    {
        PERRNO("setsockopt 1");
        return -1;
    }

    opt = 1;
    if ((setsockopt(multiSocket.socketID, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) == -1)
    {
        PERRNO("setsockopt 2");
        return -1;
    }

    memset((void *)&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(nw_config.multicast_ip);
    mreq.imr_interface.s_addr = inet_addr(address);
    // fprintf(stderr, "Index: %d (port %d, %s / %s)\n", multiSocket.socketID, 4321, "224.168.1.80", address);

    if ((setsockopt(multiSocket.socketID, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq))) == -1)
    {
        PERRNO("setsockopt 3");
        printf("\nerrno %d\n", errno);
        return -1;
    }

    /* Disable reception of our own multicast */
    opt = 0;
    if ((setsockopt(multiSocket.socketID, IPPROTO_IP, IP_MULTICAST_LOOP, &opt, sizeof(opt))) == -1)
    {
        PERRNO("setsockopt");
        return -1;
    }

    if (bind(multiSocket.socketID, (struct sockaddr *)&multicastAddress, sizeof(struct sockaddr_in)) == -1)
    {
        PERRNO("bind");
        return -1;
    }

    return 0;
}
void closeSocket()
{
    if (multiSocket.socketID != -1)
        shutdown(multiSocket.socketID, SHUT_RDWR);
}
void loadConfig()
{
    // Hardcode
    nw_config.iface = "wlp0s20f3";
    strcpy(nw_config.multicast_ip, "224.168.1.80");
    nw_config.port = 2482;
    nw_config.compress_type = UNCOMPRESS;
    // nw_config.compress_type = LZ4_COMPRESS;
}

int sendData()
{
    if (nw_config.compress_type == UNCOMPRESS)
    {
        // Just send
        int nsent = sendto(multiSocket.socketID, data, actual_data_size, 0, (struct sockaddr *)&multiSocket.destAddress, sizeof(struct sockaddr));
#ifdef COMM_TEST
        // printf("[send] size %d, data %s\n", actual_data_size, data);
#endif
        if (nsent == actual_data_size)
            return 0;
        else
            return -1;
    }
    else if (nw_config.compress_type == ZLIB_COMPRESS)
    {
        // Compress with ZLIB before send
        unsigned long int compressed_data_size = compressBound(actual_data_size);
        char compressed_data[compressed_data_size];
#ifdef COMM_DEBUG
        printf("Before: %d and %d -> %s\n", sizeof(data), sizeof(compressed_data), data);
#endif
        compress((Bytef *)compressed_data, &compressed_data_size, (Bytef *)data, (unsigned long)actual_data_size);
#ifdef COMM_DEBUG
        printf("After: %d and %d -> %s\n", sizeof(data), sizeof(compressed_data), compressed_data);
        // Debug
        unsigned long int data_raw_size = compressBound(compressed_data_size);
        char data_raw[data_raw_size];
        uncompress((Bytef *)data_raw, &data_raw_size, (Bytef *)compressed_data, (unsigned long)compressed_data_size);
        printf("Uncompress: %d and %d -> %s\n", sizeof(data_raw), sizeof(compressed_data), data_raw);
#endif
        int nsent = sendto(multiSocket.socketID, compressed_data, compressed_data_size, 0, (struct sockaddr *)&multiSocket.destAddress, sizeof(struct sockaddr));
#ifdef COMM_TEST
        printf("[send] size %d, data %s\n", compressed_data_size, compressed_data);
#endif
        if (nsent == compressed_data_size)
            return 0;
        else
            return -1;
    }
    else if (nw_config.compress_type == LZ4_COMPRESS)
    {
        // Compress with LZ4 before send
        uint8_t compressed_data_size = LZ4_compressBound(actual_data_size);
        char compressed_data[compressed_data_size];
#ifdef COMM_DEBUG
        printf("Before: %d and %d -> %s\n", sizeof(data), sizeof(compressed_data), data);
#endif
        compressed_data_size = LZ4_compress_default(data, &compressed_data[0], actual_data_size, compressed_data_size);
#ifdef COMM_DEBUG
        printf("After: %d and %d -> %s\n", sizeof(data), sizeof(compressed_data), compressed_data);
        // Debug
        unsigned long int data_raw_size = LZ4_compressBound(compressed_data_size);
        char data_raw[data_raw_size];
        data_raw_size = LZ4_decompress_safe(compressed_data, &data_raw[0], compressed_data_size, data_raw_size);
        printf("Uncompress: %d and %d -> %s\n", sizeof(data_raw), sizeof(compressed_data), data_raw);
#endif
        int nsent = sendto(multiSocket.socketID, compressed_data, compressed_data_size, 0, (struct sockaddr *)&multiSocket.destAddress, sizeof(struct sockaddr));
#ifdef COMM_TEST
        printf("[send] size %d, data %s\n", compressed_data_size, compressed_data);
#endif
        if (nsent == compressed_data_size)
            return 0;
        else
            return -1;
    }

    return 0;
}
void *receiveData(void *socket_id)
{
    // char *prev_src = "qweqwe";
    while (end_signal_counter == 0)
    {
        struct sockaddr src_addr;
        socklen_t addr_len;

        char recv_buffer[recv_data_size];
        int nrecv = recvfrom(multiSocket.socketID, (void *)recv_buffer, max_recv_data_size, 0, &src_addr, &addr_len);
#ifdef COMM_DEBUG
        printf("Buffer, nrecv: %d -> %s\n", nrecv, recv_buffer);
#endif
        if (nrecv > -1)
        {
            bzero(recv_data, max_recv_data_size);
            if (nw_config.compress_type == UNCOMPRESS)
            {
                // strncpy(recv_data, recv_buffer, nrecv);
                memcpy(recv_data, recv_buffer, nrecv);
                recv_data_size = nrecv;
            }
            else if (nw_config.compress_type == ZLIB_COMPRESS)
            {
                recv_data_size = compressBound(nrecv);
                uncompress((Bytef *)recv_data, &recv_data_size, (Bytef *)recv_buffer, (unsigned long)nrecv);
            }
            else if (nw_config.compress_type == LZ4_COMPRESS)
            {
                // recv_data_size = LZ4_compressBound(nrecv);
                recv_data_size = nrecv;
                printf("size: %d and %d\n", recv_data_size, nrecv);
                recv_data_size = LZ4_decompress_safe(recv_buffer, &recv_data[0], nrecv, recv_data_size);
            }
            char *src_ip = inet_ntoa(((struct sockaddr_in *)&src_addr)->sin_addr);
#if defined(COMM_DEBUG) || defined(COMM_TEST)
            printf("[recv] nrecv: %d -> %s \n", recv_data_size, recv_data);
            printf("[recv] recv_from: %s\n", src_ip);
#endif
            // printf("error %d\n", strcmp(src_ip, prev_src));
            // if (strcmp(src_ip, prev_src) == 0)
            // {
            //     printf("data valid\n");
            // }
            // strcpy(prev_src, src_ip);
        }
    }
    printf("Recv thread exited\n");
    pthread_exit(NULL);
    return NULL;
}

void signalHandler(int sig)
{
    printf("Terminate with custom signal handler\n");
    closeSocket();
    end_signal_counter++;
    if (end_signal_counter == 3) // Force close if other thread cannot be killed by signal
        abort();
}

int main()
{
    printf("Start..\n");
    struct sched_param proc_sched;

    loadConfig();

    signal(SIGINT, signalHandler);

    /* Assign a real-time priority to process */
    proc_sched.sched_priority = 60;
    if ((sched_setscheduler(getpid(), SCHED_FIFO, &proc_sched)) < 0)
    {
        PERRNO("setscheduler");
        return -1;
    }

    if (openSocket() == -1)
    {
        PERR("openMulticastSocket");
        return -1;
    }

    end_signal_counter = 0;

    // Create new thread to receive data
    pthread_attr_init(&thread_attr);
    pthread_attr_setinheritsched(&thread_attr, PTHREAD_INHERIT_SCHED);
    if (pthread_create(&recv_thread, &thread_attr, receiveData, (void *)&multiSocket.socketID) != 0)
    {
        PERRNO("pthread_create");
        closeSocket();
        return -1;
    }

#ifdef COMM_TEST
    int counter;
    while (end_signal_counter == 0)
    {
        if (++counter % 100000000 == 0)
        {
            // printf("counters: %d\n", counter);
            if (sendData() == -1)
            {
                PERR("sendData");
                return -1;
            }
        }
    }
#endif
    printf("Success\n");

    return 0;
}