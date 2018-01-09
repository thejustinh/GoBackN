/* Server side - UDP Code				    */
/* By Hugh Smith	4/1/2017	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "networks.h"
#include "libcpe464/networks/checksum.h"
#include "cpe464.h"

#define MAXBUF 80
#define HDR_LEN 7
#define MAX_PAYLOAD 1400

void printClientIP(struct sockaddr_in6 * client);
int checkArgs(int argc, char *argv[]);
void sendFile(int socketNum);
void fillPkt(u_char *pkt, uint32_t seq, uint8_t flag, char *data);
int crcCheck(u_char * pkt);

void processServer(int socketNum);
void processClient(int socketNum, u_char * buf, Connection * client);
int setupResponse(Connection *client, u_char *pkt, int16_t * windowSize, int16_t * buffSize);

int sendData(Connection * client, int fd, int32_t * seq_num, 
   int buf_size, int window_size, int *windowCount, struct packets *myWindow);
int recvAck(Connection * client, struct packets * myWindow, int windowSize, int32_t *srej);

void printWindow(struct packets *myWindow, int windowSize);
void saveToWindow(struct packets *myWindow, int windowSize, u_char * packet);
int32_t resendRR(Connection * client, struct packets *myWindow, int windowSize);
int resendBuff(Connection * client, struct packets *myWindow, int windowSize);
int windowClosed(Connection * client, struct packets * myWindow, int windowSize, int * windowCount);
void delFromWindow(struct packets * myWindow, int windowSize, int seq_num);
int notExpected(struct packets *myWindow, int windowSize, int32_t seq_num);
int itemsInWindow(struct packets * myWindow, int windowSize);

int main ( int argc, char *argv[]  )
{ 
	int socketNum = 0;				
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);
	
   sendtoErr_init(atof(argv[1]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);
	
	socketNum = udp_server(portNumber);

   processServer(socketNum);

	close(socketNum);

   return 0;
}

void processServer(int socketNum)
{
   pid_t pid = 0;
   u_char buf[HDR_LEN + MAX_PAYLOAD];
   int status = 0;
   uint16_t windowSize = 0;
   uint16_t bufSize = 0;
   int dataLen = 0;
   uint32_t recv_len;
   Connection client;

   while (1)
   {
      // block waiting for a new client
      if (select_call(socketNum, LONG_TIME, 0, TIMER_SET) == 1)
      { 
         recv_len = safeRecv(socketNum, buf, HDR_LEN + MAX_PAYLOAD, &client);

         if (crcCheck(buf) == 1) // corrupt packet 
         {
            printf("corrupt packet!... dropping\n");
            continue;
         }
      
         if (recv_len != 0) // if we read something
         {
            if ((pid = fork()) < 0)
            {
               perror("fork");
               exit(-1);
            }
            if (pid == 0)
            {
               processClient(socketNum, buf, &client);
               close(client.sk_num);
               exit(0);
            }
         }

         while (waitpid(-1, &status, WNOHANG) > 0) {}
      }
   }
}

void processClient(int socketNum, u_char * buf, Connection * client)
{
   int state = FILENAME;
   char file[FILE_LEN];
   static int16_t windowSize = 0;
   static int16_t buffSize = 0;
   static int windowCount = 0;
   static int32_t seq_num = START_SEQ_NUM + 1;
   int fd;
   int i;
   int num;
   static int32_t srej;
   static int32_t count = 0;
   int32_t zero = (int32_t)0;
   struct packets * myWindow; // change window size to be dynamic

   memset(file, 0, FILE_LEN);
   memcpy(file, buf + HDR_LEN + 4, FILE_LEN);   
   
   fd = open(file, O_RDONLY);

   while(1)
   {
      switch (state)
      {
         case FILENAME:
            state = setupResponse(client, buf, &windowSize, &buffSize);
            myWindow = malloc((int)windowSize * sizeof(struct packets));
            for (i = 0; i < windowSize; i++) {
               myWindow[i].seq_num = zero;
               memset(myWindow[i].packet, 0, HDR_LEN + MAX_PAYLOAD);
            }
            break;
         case SEND_DATA: // Open window
            state = sendData(client, fd, &seq_num, buffSize, windowSize, &windowCount, myWindow);
            break;
         case RECV_ACK:
            state = recvAck(client, myWindow, windowSize, &srej);
            break;
         case WINDOW_CLOSED:
            state = windowClosed(client, myWindow, windowSize, &windowCount);
            break;
         case DONE:
            close(fd);
            exit(0);
            break;
         default:
            state = DONE;
            break;  
      }
   }
}

int setupResponse(Connection *client, u_char *pkt, int16_t *windowSize,  int16_t * buffSize)
{
   uint8_t flag;
   char file[FILE_LEN + HDR_LEN];
   u_char send[HDR_LEN + MAX_PAYLOAD];
   int returnVal = DONE;
   int i;
   int32_t zero = (int32_t)0;
 
   // Save filename 
   memset(file, 0, FILE_LEN);
   memcpy(file, pkt + HDR_LEN + 4, FILE_LEN);

   memcpy(windowSize, pkt + HDR_LEN, 2);
   memcpy(buffSize, pkt + HDR_LEN + 2, 2);
 
   if ((client->sk_num = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
   {
      perror("filename, open client socket");
      exit(-1);
   }
   

   *windowSize = (int16_t)ntohs(*windowSize) ;
   *buffSize = (int16_t)ntohs(*buffSize);
 
   if( access( file, F_OK ) != -1 ) { 
      flag = 2; // file exists 
      returnVal = SEND_DATA;
   } else {
      flag = 8; // file doesn't exist
   }

   fillPkt(send, 1, flag, file);
   
   safeSend(send, HDR_LEN + MAX_PAYLOAD, client);

   return returnVal;
}

/***** 
 * This sendData() method used to read file contents into a buffer to send to the client.
 * Some of the code below was adapted from Professor Smith's solution for Stop
 * and Wait Protocol
 ****/
int sendData(Connection * client, int fd, int32_t * seq_num,
   int buf_size, int window_size, int *windowCount, struct packets *myWindow)
{
   int returnVal = SEND_DATA;
   int clientAddrLen = sizeof(client);
   int len_read = 0;
   int items = itemsInWindow(myWindow, window_size);
   u_char data[buf_size + 1];
   u_char pkt[HDR_LEN + MAX_PAYLOAD];
   
   if (select_call(client->sk_num, 0, 0, TIMER_SET) == 1) {
      (*windowCount)--;
      return RECV_ACK;
   }

   memset(data, 0, buf_size + 1);
   memset(pkt, 0, HDR_LEN + MAX_PAYLOAD);

   // *****
   // if window is closed wait for ack before continuing!!!
   // *****

   if (*windowCount == window_size) {
      (*windowCount)--; // once window is open, reduce the count and send more data
      return WINDOW_CLOSED;
   }

   if (items == window_size) {
      (*windowCount) = 0;
      return resendBuff(client, myWindow, window_size);
   }

   // *****
   // Window is currently open
   // *****
   
   len_read = read(fd, data, (size_t)buf_size);

   switch(len_read)
   {
      case -1: // error with read() system call
         perror("sendData: read error");
         returnVal = DONE;
         break;
      case 0: // no bytes read in system call (end of file)
         fillPkt(pkt, *seq_num, EOF_FLAG, data);
         returnVal = WINDOW_CLOSED;                
         break;
      default: // something read
         fillPkt(pkt, *seq_num, DATA_FLAG, data);
         returnVal = SEND_DATA;
         (*seq_num)++;
         break;
   }

   saveToWindow(myWindow, (int)window_size, pkt);
   safeSend(pkt, HDR_LEN + MAX_PAYLOAD, client);
    
   // *****
   // Continually check for RRs and SREJs
   // *****
   if (select_call(client->sk_num, 0, 0, TIMER_SET) == 1) {
      (*windowCount)--;
      return RECV_ACK;
   }

   (*windowCount)++;
   return returnVal;
}

int recvAck(Connection * client, struct packets * myWindow, int windowSize, int32_t * srrej)
{
   int32_t recv_len = 0;
   int recvFlag = 0;
   int min;
   int i;
   int32_t seq_num;
   int32_t srej;
   int32_t rr;
   u_char ack[HDR_LEN + MAX_PAYLOAD];

   recv_len = safeRecv(client->sk_num, ack, HDR_LEN + MAX_PAYLOAD, client);

   if (crcCheck(ack) == 1) {
      return WINDOW_CLOSED; // Wait on ACK
   }

   recvFlag = ack[6];

   if (recvFlag == RR) {
      memcpy(&rr, ack + HDR_LEN, 4);
      rr = ntohl(rr);
      delFromWindow(myWindow, windowSize, rr - 1);
   } else if (recvFlag == SREJ) {
      memcpy(&srej, ack + HDR_LEN, 4); // seq num we want to resend
      srej = ntohl(srej);
      delFromWindow(myWindow, windowSize, srej - 1);
      return WINDOW_CLOSED; // resend buffer and close window
   } else if (recvFlag == EOF_ACK) {
      close(client->sk_num);
      return DONE;
   } else {
      return DONE;
   }

   return SEND_DATA;
}
 
int windowClosed(Connection * client, struct packets * myWindow, int windowSize, int * windowCount)
{
   int dataLen = 0;
   int flag = 0;
   static int resend = 0;
   int32_t seq_num;
   u_char dataBuf[HDR_LEN + MAX_PAYLOAD];

   memset(dataBuf, 0, HDR_LEN + MAX_PAYLOAD);

   if (itemsInWindow(myWindow, windowSize) == 0)
      return SEND_DATA;

   resend++;

   if (resend > 10) {
      printf("Data resent 10 times. other side is down\n");
      return DONE;
   }
   
   if (select_call(client->sk_num, 1, 0, TIMER_SET) == 0)
   {
      (*windowCount) = 0;
      resendBuff(client, myWindow, windowSize);
      return WINDOW_CLOSED;
   }

   resend = 0;
   return RECV_ACK;
}


/*****
 * Function to Resend the packets in the buffer
 ****/
int resendBuff(Connection * client, struct packets *myWindow, int windowSize)
{
   int i = 0;
   int numPackets = itemsInWindow(myWindow, windowSize);
   int32_t zero = (int32_t)0;
   int32_t min = 0;
   struct packets *tmpWindow = malloc(windowSize * sizeof(struct packets));

   if (numPackets == 0) // if we do not have anything in out window
      return SEND_DATA;

   // If temp window is emtpy (aka we have resnt buffer before) 
   for (i = 0; i < windowSize; i++) {
      tmpWindow[i].seq_num = (int32_t)0;
      memset(tmpWindow[i].packet, 0, HDR_LEN + MAX_PAYLOAD);
      tmpWindow[i].seq_num = myWindow[i].seq_num;
      memcpy(tmpWindow[i].packet, myWindow[i].packet, HDR_LEN + MAX_PAYLOAD);
   } 


   // Resend Buffer
   for (i = 0; i < numPackets; i++) {
      // Returns the index of the lowest unacknowledged sequencen umber sent
      min = resendRR(client, tmpWindow, windowSize);
      tmpWindow[min].seq_num = (int32_t)0;
      memset(tmpWindow[min].packet, 0, HDR_LEN + MAX_PAYLOAD);
   
      if (select_call(client->sk_num, 0, 0, TIMER_SET) == 1) {
         return RECV_ACK;
      }
   }

   return WINDOW_CLOSED;
}


int32_t resendRR(Connection * client, struct packets *myWindow, int windowSize)
{
   int i = 0;
   int32_t minRR = 0; // index of the lowest unacknowledged packet
   int32_t zero = (int32_t) 0;
   u_char send[HDR_LEN + MAX_PAYLOAD];
   
   // This loop gets the first seq_num from the queue that is not 0
   for(i = 0; i < windowSize; i++) 
   {
      if (myWindow[i].seq_num != zero)
         minRR = i;
   }

   if (minRR == 0 && myWindow[minRR].seq_num == zero) {
      return 0;
   }

   for(i = 0; i < windowSize; i++)
   {
      if (myWindow[i].seq_num < myWindow[minRR].seq_num && myWindow[i].seq_num != 0)
         minRR = i;
   }
   
   if (myWindow[minRR].seq_num != 0) {
      memset(send, 0, HDR_LEN + MAX_PAYLOAD);
      memcpy(send, myWindow[minRR].packet, HDR_LEN + MAX_PAYLOAD);
      safeSend(send, HDR_LEN + MAX_PAYLOAD, client);
   } 

   return minRR;
}

/*****
 * Function to check if the received sequence number is greater than any of
 * the unacknowledged sequence numbers.
 ****/
int notExpected(struct packets *myWindow, int windowSize, int32_t seq_num)
{
   int i;

   for (i = 0; i < windowSize; i++)
   {
      if (myWindow[i].seq_num != 0 && myWindow[i].seq_num < seq_num)
         return 1;
   }
   
   return 0;
}

void printClientIP(struct sockaddr_in6 * client)
{
	char ipString[INET6_ADDRSTRLEN];

	inet_ntop(AF_INET6, &client->sin6_addr, ipString, sizeof(ipString));
	printf("Client info - IP: %s Port: %d ", ipString, ntohs(client->sin6_port));
	
}

void fillPkt(u_char *pkt, uint32_t seq, uint8_t flag, char *data)
{
   unsigned short cksum = 0;
   uint32_t seq_num = htonl(seq);

   // clearing pkt
   memset(pkt, 0, HDR_LEN + MAX_PAYLOAD);

   // store sequence number in network order
   memcpy(pkt, &seq_num, 4);

   // store fake (0-value) checksum 
   memcpy(pkt + 4, &cksum, 2);

   // store flag
   pkt[6] = flag;

   // store data
   memcpy(pkt + 7, data, MAX_PAYLOAD);

   // calculate and store checksum
   cksum = in_cksum((unsigned short *)pkt, HDR_LEN + MAX_PAYLOAD);
   memcpy(pkt + 4, &cksum, 2);
}

void printWindow(struct packets *myWindow, int windowSize)
{
   int i = 0;
   int32_t seq;
   int32_t zero = (int32_t) 0;
   printf("***************\nWindow:\n");
   for (i = 0; i < windowSize; i++)
   {
      printf("%d: ", i);
      if (myWindow[i].seq_num != zero)
         printf("seq num #%d || DATA: %s\n", myWindow[i].seq_num, myWindow[i].packet + HDR_LEN);
      else
         printf("\n");
   }
   printf("***************\n\n");
}

void saveToWindow(struct packets *myWindow, int windowSize, u_char * packet)
{
   int32_t seq;
   int32_t zero = (int32_t)0;
   int index = 0, i = 0;
   
   memcpy(&seq, packet, 4);
   seq = ntohl(seq);

   for(i = 0; i < windowSize; i++)
   {
      if (myWindow[i].seq_num == zero)
      {
         memcpy(&(myWindow[i].seq_num), &seq, 4);
         memcpy((myWindow[i].packet), packet, HDR_LEN + MAX_PAYLOAD);
         return;
      } // or save to window if seq num is old 
   }
}

int itemsInWindow(struct packets * myWindow, int windowSize)
{
   int count = 0;
   int i;
   int32_t zero = (int32_t)0;

   for (i = 0; i < windowSize; i++) 
   {
      if (myWindow[i].seq_num != zero) 
         count++;
   }

   return count;
}

/*****
 * Funciton to remove packet from a window. (Packet was Acknowledged)
 ****/
void delFromWindow(struct packets * myWindow, int windowSize, int seq_num)
{
   int i = 0;

   for(i = 0; i < windowSize; i++)
   {
      if (myWindow[i].seq_num <= seq_num) 
      {
         myWindow[i].seq_num = (int32_t)0;
         memset(myWindow[i].packet, 0, HDR_LEN + MAX_PAYLOAD);
      }
   }
}

int checkArgs(int argc, char *argv[])
{
	// Checks args and returns port number
	int portNumber = 0;

	if (argc > 3 || argc == 1)
	{
		fprintf(stderr, "Usage %s err-percent [optional port number]\n", argv[0]);
		exit(-1);
	}
	
   if (atoi(argv[1]) < 0 || atoi(argv[1]) >= 1)
	{
		printf("Error rate needs to be between 0 and less than 1 and is %s\n", argv[1]);
      exit(-1);
	}

   if (argc == 3)
   {
      portNumber = atoi(argv[2]);
   }
	
	return portNumber;
}


