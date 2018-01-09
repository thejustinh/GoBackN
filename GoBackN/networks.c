
// Hugh Smith April 2017
// Network code to support TCP/UDP client and server connections

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "networks.h"
#include "gethostbyname.h"
#include "cpe464.h"
#include "libcpe464/networks/checksum.h"

int safeRecvfrom(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int * addrLen)
{
	int returnValue = 0;
	if ((returnValue = recvfrom(socketNum, buf, (size_t) len, flags, srcAddr, (socklen_t *) addrLen)) < 0)
	{
		perror("recvfrom: ");
		exit(-1);
	}
	
	return returnValue;
}

int safeSendto(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int addrLen)
{
	int returnValue = 0;
	if ((returnValue = sendto(socketNum, buf, (size_t) len, flags, srcAddr, (socklen_t) addrLen)) < 0)
	{
		perror("sendto: ");
		exit(-1);
	}
	
	return returnValue;
}

int safeRecv2(int socketNum, void * buf, int len, int flags)
{
	int returnValue = 0;
	if ((returnValue = recv(socketNum, buf, (size_t) len, flags)) < 0)
	{
		perror("recv: ");
		exit(-1);
	}
	
	return returnValue;
}

int safeSend2(int socketNum, void * buf, int len, int flags)
{
	int returnValue = 0;
	if ((returnValue = send(socketNum, buf, (size_t) len, flags)) < 0)
	{
		perror("send: ");
		exit(-1);
	}
	
	return returnValue;
}


// This function sets the server socket. The function returns the server
// socket number and prints the port number to the screen.  

int tcpServerSetup(int portNumber)
{
	int server_socket= 0;
	struct sockaddr_in6 server;     
	socklen_t len= sizeof(server);  

	server_socket= socket(AF_INET6, SOCK_STREAM, 0);
	if(server_socket < 0)
	{
		perror("socket call");
		exit(1);
	}

	server.sin6_family= AF_INET6;         		
	server.sin6_addr = in6addr_any;   
	server.sin6_port= htons(portNumber);         

	// bind the name (address) to a port 
	if (bind(server_socket, (struct sockaddr *) &server, sizeof(server)) < 0)
	{
		perror("bind call");
		exit(-1);
	}
	
	// get the port name and print it out
	if (getsockname(server_socket, (struct sockaddr*)&server, &len) < 0)
	{
		perror("getsockname call");
		exit(-1);
	}

	if (listen(server_socket, BACKLOG) < 0)
	{
		perror("listen call");
		exit(-1);
	}
	
	printf("Server Port Number %d \n", ntohs(server.sin6_port));
	
	return server_socket;
}

// This function waits for a client to ask for services.  It returns
// the client socket number.   

int tcpAccept(int server_socket, int debugFlag)
{
	struct sockaddr_in6 clientInfo;   
	int clientInfoSize = sizeof(clientInfo);
	int client_socket= 0;

	if ((client_socket = accept(server_socket, (struct sockaddr*) &clientInfo, (socklen_t *) &clientInfoSize)) < 0)
	{
		perror("accept call");
		exit(-1);
	}
	  
	if (debugFlag)
	{
		printf("Client accepted.  Client IP: %s Client Port Number: %d\n",  
				getIPAddressString6(clientInfo.sin6_addr.s6_addr), ntohs(clientInfo.sin6_port));
	}
	

	return(client_socket);
}

int tcpClientSetup(char * serverName, char * port, int debugFlag)
{
	// This is used by the client to connect to a server using TCP
	
	int socket_num;
	uint8_t * ipAddress = NULL;
	struct sockaddr_in6 server;      
	
	// create the socket
	if ((socket_num = socket(AF_INET6, SOCK_STREAM, 0)) < 0)
	{
		perror("socket call");
		exit(-1);
	}

	// setup the server structure
	server.sin6_family = AF_INET6;
	server.sin6_port = htons(atoi(port));
	
	// get the address of the server 
	if ((ipAddress = gethostbyname6(serverName, &server)) == NULL)
	{
		exit(-1);
	}

	if(connect(socket_num, (struct sockaddr*)&server, sizeof(server)) < 0)
	{
		perror("connect call");
		exit(-1);
	}

	if (debugFlag)
	{
		printf("Connected to %s IP: %s Port Number: %d\n", serverName, getIPAddressString6(ipAddress), atoi(port));
	}
	
	return socket_num;
}

int udpServerSetup(int portNumber)
{
	struct sockaddr_in6 server;
	int socketNum = 0;
	int serverAddrLen = 0;	
	
	// create the socket
	if ((socketNum = socket(AF_INET6,SOCK_DGRAM,0)) < 0)
	{
		perror("socket() call error");
		exit(-1);
	}
	
	// set up the socket
	server.sin6_family = AF_INET6;    		// internet (IPv6 or IPv4) family
	server.sin6_addr = in6addr_any ;  		// use any local IP address
	server.sin6_port = htons(portNumber);   // if 0 = os picks 

	// bind the name (address) to a port
	if (bind(socketNum,(struct sockaddr *) &server, sizeof(server)) < 0)
	{
		perror("bind() call error");
		exit(-1);
	}

	/* Get the port number */
	serverAddrLen = sizeof(server);
	getsockname(socketNum,(struct sockaddr *) &server,  &serverAddrLen);
	printf("Server using Port #: %d\n", ntohs(server.sin6_port));

	return socketNum;	
	
}

int32_t select_call(int32_t socketNum, int32_t seconds, int32_t microseconds, int32_t set_null)
{
   fd_set fdvar;
   struct timeval aTimeout;
   struct timeval * timeout = NULL;

   if (set_null == 1)
   {
      aTimeout.tv_sec = seconds; 
      aTimeout.tv_usec = microseconds;
      timeout = &aTimeout;
   }

   FD_ZERO(&fdvar);
   FD_SET(socketNum, &fdvar);

   if (select(socketNum + 1, (fd_set *) &fdvar, (fd_set *) 0, (fd_set *) 0, timeout) < 0)
   {
      perror("select");
      exit(-1);
   }

   if (FD_ISSET(socketNum, &fdvar))
   {
      return 1;
   } 

   return 0;
}

int processSelect(Connection * client, int *retryCount, int selectTimeoutState, int dataReadyState, int doneState)
{
   //Returns:
   // doneState if calling this function exceeds MAX_TRIES
   // selectTimeoutState if the select times out without receiving anything
   // dataReadyState if select() returns indicating that data is ready for read
   
   int returnVal;

   (*retryCount)++;
   if (*retryCount >= MAX_TRIES)
   {
      printf("Send data %d times, no ACK. Other side is down\n", MAX_TRIES);
      returnVal = doneState;
   }
   else
   {
      if (select_call(client->sk_num, SHORT_TIME, 0, 1) == 1)
      {
         *retryCount = 0;
         returnVal = dataReadyState;
      }
      else
      {
         // no data ready
         returnVal = selectTimeoutState;
      }
   }

   return returnVal;
}


// Returns 0 if valid, 1 if corrupt
int crcCheck(u_char * pkt)
{
   unsigned short cksum = 0;
   if ((cksum = in_cksum((unsigned short *) pkt, HDR_LEN + MAX_PAYLOAD)) != 0)
   {
      return 1;
   }

   return 0;
}

int32_t safeSend(u_char * pkt, uint32_t len, Connection * connection)
{
   int send_len = 0;
   
   if ((send_len = sendtoErr(connection->sk_num, pkt, len, 0, 
      (struct sockaddr *) &(connection->remote), connection->len)) < 0) 
   {
      perror("in send_buf(), sendto() call");
      exit(-1);
   }

   return send_len;
}

int32_t safeRecv(int recv_sk_num, u_char * data_buf, int len, Connection * connection)
{
   uint32_t recv_len = 0;
   uint32_t remote_len = sizeof(struct sockaddr_in);

   if ((recv_len = recvfrom(recv_sk_num, data_buf, len, 0, (struct sockaddr *)&(connection->remote), &remote_len)) < 0)
   {
      perror("recv_buf, recvfrom");
      exit(-1);
   }

   connection->len = remote_len;
   
   return recv_len;
}

void printPkt(u_char * pkt, int bytes_read)
{
   uint32_t seq;
   uint16_t checksum;
   uint8_t flag;
   char data[MAX_PAYLOAD];

   memset(data, 0, MAX_PAYLOAD);
   memcpy(&seq, pkt, 4);
   memcpy(&checksum, pkt + 4, 2);
   memcpy(&flag, pkt + 4 + 2, 1);
   memcpy(data, pkt + 7, MAX_PAYLOAD);

   printf("***********************\n");
   printf("Packet Data...\n");
   printf("Bytes Read: %d\n", bytes_read);
   printf("Sequence num: %d\n", ntohl(seq));
   printf("Checksum: %.04x\n", checksum);
   printf("Flag: %d\n", flag);
   printf("Data: %s\n", data);
   printf("***********************\n");
}
  

int32_t udp_client_setup(char * hostname, uint16_t port_num, Connection * connection)
{
   struct hostent * hp = NULL;

   connection->sk_num = 0;
   connection->len = sizeof(struct sockaddr_in);

   if ((connection->sk_num = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
   {
      perror("udp_client_setup, socket");
      exit(-1);
   }

   connection->remote.sin_family = AF_INET;

   hp = gethostbyname(hostname);
   
   if (hp == NULL)
   {
      printf("Host not found: %s\n", hostname);
      return -1;
   }

   memcpy(&(connection->remote.sin_addr), hp->h_addr, hp->h_length);

   connection->remote.sin_port = htons(port_num);

   return 0;
}

int32_t udp_server(int portNumber)
{
   int sk = 0;
   struct sockaddr_in local;
   uint32_t len = sizeof(local);
   
   if ((sk = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
   {
      perror("socket");
      exit(-1);
   }

   local.sin_family = AF_INET;
   local.sin_addr.s_addr = INADDR_ANY;
   local.sin_port = htons(portNumber);

   if (bindMod(sk, (struct sockaddr *)&local, sizeof(local)) < 0)
   {
      perror("udp_server, bind");
      exit(-1);
   }

   getsockname(sk, (struct sockaddr *)&local, &len);
   printf("Using Port #: %d\n", ntohs(local.sin_port));
   
   return(sk);
}

int setupUdpClientToServer(struct sockaddr_in6 *server, char * hostName, int portNumber)
{
	// currently only setup for IPv4 
	int socketNum = 0;
	char ipString[INET6_ADDRSTRLEN];
	uint8_t * ipAddress = NULL;
	
	// create the socket
	if ((socketNum = socket(AF_INET6, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket() call error");
		exit(-1);
	}
  	 	
	if ((ipAddress = gethostbyname6(hostName, server)) == NULL)
	{
		exit(-1);
	}
	
	server->sin6_port = ntohs(portNumber);
	server->sin6_family = AF_INET6;	
	
	inet_ntop(AF_INET6, ipAddress, ipString, sizeof(ipString));
	printf("Server info - IP: %s Port: %d \n", ipString, portNumber);
		
	return socketNum;
}
