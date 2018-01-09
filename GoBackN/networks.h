
// 	Writen - HMS April 2017
//  Supports TCP and UDP - both client and server


#ifndef __NETWORKS_H__
#define __NETWORKS_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BACKLOG 10
#define LONG_TIME 10
#define SHORT_TIME 1
#define MAX_TRIES 10
#define HDR_LEN 7
#define MAX_PAYLOAD 1400
#define TIMER_SET 1
#define FILE_LEN 100
#define START_SEQ_NUM 1

// FLAGS
#define DATA_FLAG 3
#define RR 5
#define SREJ 6
#define EOF_FLAG 9
#define EOF_ACK 10
#define SEND_ARGS_FLAG 4

// STATES
#define FILENAME 1
#define SEND_DATA 2
#define FILE_STATUS 3
#define RECV_DATA 4
#define WINDOW_CLOSED 5
#define WAIT_FOR_EOF_ACK 6
#define RECV_ACK 7
#define SETUP 8
#define RESEND_WINDOW 9
#define DONE 10

typedef struct connection Connection;

struct connection
{
   int32_t sk_num;
   struct sockaddr_in remote;
   uint32_t len;
};

struct packets {
   int32_t seq_num; // 4 bytes
   u_char packet[HDR_LEN + MAX_PAYLOAD]; // 1407 bytes
};

int safeRecv2(int socketNum, void * buf, int len, int flags);
int safeSend2(int socketNum, void * buf, int len, int flags);
int safeRecvfrom(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int * addrLen);
int safeSendto(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int addrLen);
int32_t select_call(int32_t socketNum, int32_t seconds, int32_t microseconds, int32_t set_null);
int processSelect(Connection * client, int *retryCount, int selectTimeoutState, int dataReadyState, int doneState);
int crcCheck(u_char * pkt);
void printPkt(u_char * pkt, int bytes_read);
int32_t safeSend(u_char * pkt, uint32_t len, Connection * connection);
int32_t safeRecv(int recv_sk_num, u_char * data_buf, int len, Connection * connection);

// for the server side
int tcpServerSetup(int portNumber);
int tcpAccept(int server_socket, int debugFlag);
int udpServerSetup(int portNumber);
int32_t udp_server(int portNumber);

// for the client side
int tcpClientSetup(char * serverName, char * port, int debugFlag);
int setupUdpClientToServer(struct sockaddr_in6 *server, char * hostName, int portNumber);
int32_t udp_client_setup(char * hostname, uint16_t port_num, Connection * connection);

#endif
