// Client side - UDP Code				    
// By Hugh Smith	4/1/2017		

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
#include "libcpe464/networks/checksum.h"
#include "cpe464.h"

#define MAXBUF 80
#define xstr(a) str(a)
#define str(a) #a

#define HDR_LEN 7
#define MAX_PAYLOAD 1400
#define TIMER_SET 1

void talkToServer(int socketNum, struct sockaddr_in6 server);
int getData(char * buffer);
int checkArgs(int argc, char * argv[]);
void fileTransfer(int socketNum, struct sockaddr_in6 server, char * file);

void processClient(Connection * server, char * outputFile, char * srcFile, int16_t *windowSize, int16_t *bufSize);
void fillPkt(u_char *pkt, uint32_t seq, uint8_t flag, char *data);
int fileCheck(Connection * server, char * file, int16_t *windowSize, int16_t *bufSize);
int recvData(Connection * server, char * outputFile, int32_t * my_seq);


int main (int argc, char *argv[])
 {
	int32_t socketNum = 0;				
	int portNumber = 0;
   static int16_t windowSize;
   static int16_t bufSize;
	portNumber = checkArgs(argc, argv);
   Connection server;  

   sendtoErr_init(atof(argv[5]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);

   if( (socketNum = udp_client_setup(argv[6], portNumber, &server)) < 0)
   {
      printf("Could not connect to server\n");
      exit(-1);
   }
   
   windowSize = atoi(argv[3]);
   bufSize = atoi(argv[4]);
   processClient(&server, argv[1], argv[2], &windowSize, &bufSize);

	close(socketNum);
   
   return 0;
}

void processClient(Connection * server, char * outputFile, char * srcFile, int16_t *windowSize, int16_t *bufSize)
{ 
   int state = FILENAME;
   int32_t outputFD = 0;
   static int32_t my_seq = START_SEQ_NUM + 1;

   while (state != DONE)
   {
      switch (state)
      {
         case FILENAME:
            state = fileCheck(server, srcFile, windowSize, bufSize);
            break;
         case FILE_STATUS:
            state = createFile(&outputFD, outputFile);
            break;
         case RECV_DATA:
            state = recvData(server, outputFile, &my_seq);
            break;
         case DONE:
            break;
         default:
            state = DONE;
            break;
      }
   }
}

int recvData(Connection * server, char * outputFile, int32_t * my_seq)
{
   int32_t seq_num = 0;
   int32_t data_len = 0;
   uint8_t flag = 0;
   u_char dataBuf[HDR_LEN + MAX_PAYLOAD];
   u_char packet[HDR_LEN + MAX_PAYLOAD];
   int serverAddrLen = sizeof(server);
   FILE *fptr = fopen(outputFile, "a");
   static int32_t expected_seq_num = START_SEQ_NUM + 1;   

   if (select_call(server->sk_num, LONG_TIME, 0, TIMER_SET) == 0)
   {
      printf("Timeout after 10 seconds, server must be gone.\n");
      fclose(fptr);
      return DONE;
   }
   
   memset(dataBuf, 0, HDR_LEN + MAX_PAYLOAD);
   memset(packet, 0, HDR_LEN + MAX_PAYLOAD);

   safeRecv(server->sk_num, dataBuf, HDR_LEN + MAX_PAYLOAD, server);
 
   memcpy(&seq_num, dataBuf, 4);
   flag = dataBuf[6];
   seq_num = ntohl(seq_num);

   // recvData again if there is a crc error
   if (crcCheck(dataBuf) == 1) {
      fclose(fptr);
      return RECV_DATA;
   }

   if (flag == EOF_FLAG) {
      // Send ACK
      fillPkt(packet, *my_seq, EOF_ACK, &expected_seq_num);
      safeSend(packet, HDR_LEN + MAX_PAYLOAD, server);
      fclose(fptr);
      return DONE;
   }

   // if packet is what we are expecting
   if (seq_num == expected_seq_num) {
      expected_seq_num++;
      expected_seq_num = htonl(expected_seq_num);
      fillPkt(packet, *my_seq, RR, &expected_seq_num);
      fwrite(dataBuf + HDR_LEN, 1, strlen(dataBuf + HDR_LEN), fptr);
      fclose(fptr);
   } else { // not what we are expecting
      expected_seq_num = htonl(expected_seq_num);
      fillPkt(packet, *my_seq, SREJ, &expected_seq_num);
      fclose(fptr);
   } 

   expected_seq_num = ntohl(expected_seq_num);   

   (*my_seq)++;
   safeSend(packet, HDR_LEN + MAX_PAYLOAD, server);

   return RECV_DATA;
} 

// returns state
int fileCheck(Connection * server, char * file, int16_t * windowSize, int16_t * buffSize)
{
   u_char pkt[HDR_LEN + MAX_PAYLOAD];
   u_char recv[HDR_LEN + MAX_PAYLOAD];
   u_char stats[103];
   int serverAddrLen = sizeof(server);
   static int retryCount = 0; 
   int returnVal = FILENAME;
   int16_t ws;
   int16_t bs;
   unsigned short cksum = 0;
   u_char tmp[1];

   // Send first packet. This implies that the window is closed and we select
   // for 1-second waiting on RRs.
   fillPkt(pkt, 1, 1, tmp);

   ws = htons((int16_t)*windowSize);
   bs = htons((int16_t)*buffSize);

   memset(pkt + HDR_LEN, 0, MAX_PAYLOAD);
   memcpy(pkt + HDR_LEN, &ws, 2);
   memcpy(pkt + HDR_LEN + 2, &bs, 2);
   memcpy(pkt + HDR_LEN + 2 + 2, file, strlen(file));

   memcpy(&ws, pkt + HDR_LEN, 2);
   memcpy(&bs, pkt + HDR_LEN + 2, 2);

   memset(pkt + 4, 0, 2);
   cksum = in_cksum((unsigned short *)pkt, HDR_LEN + MAX_PAYLOAD);
   memcpy(pkt + 4, &cksum, 2);
 
   safeSend(pkt, HDR_LEN + MAX_PAYLOAD, server);
 
   if ((returnVal = processSelect(server, &retryCount, FILENAME, FILE_STATUS, DONE)) == FILE_STATUS)
   {  
      // Socket is ready to recv data
      safeRecv(server->sk_num, recv, HDR_LEN + MAX_PAYLOAD, server);
       
      // Corrupt Data
      if(crcCheck(recv) == 1)
         return returnVal;

      if (recv[6] == 2) {
         returnVal = FILE_STATUS; // file is ok so create output file and recv data
      } else  {
         returnVal = DONE;
      }
   } 
   
   return returnVal;
}

int createFile(int * outputFD, char * outputFile) 
{
   int returnVal = DONE;
   if ((*outputFD = open(outputFile, O_CREAT | O_TRUNC | O_WRONLY, 0600)) < 0)
   {
      perror("createFile: File open error");
      returnVal = DONE;
   } 
   else 
   {
      returnVal = RECV_DATA; // receive data once set up arugments is complete
   }

   return returnVal;
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
   memcpy(pkt + HDR_LEN, data, MAX_PAYLOAD);

   // calculate and store checksum
   cksum = in_cksum((unsigned short *)pkt, HDR_LEN + MAX_PAYLOAD);
   memcpy(pkt + 4, &cksum, 2);
}

int checkArgs(int argc, char * argv[])
{
   int portNumber = 0;

	/* check command line arguments  */
	if (argc != 8)
	{
		printf("usage: local-TO-file remote-FROM-file window-size");
      printf(" buffer-size error-percent remote-machine remote-port\n");
		exit(1);
	}
   
   if (strlen(argv[1]) > 100 || strlen(argv[2]) > 100) {
      printf("File name is too long. Please enter something <= 100 chars\n");	
   }

   if (atoi(argv[5]) < 0 || atoi(argv[5]) >= 1)
   {
      printf("Error rate needs to be between 0 and less than 1 and is %s\n", argv[5]);
      exit(-1);
   }

	// Checks args and returns port number
	
	portNumber = atoi(argv[7]);		

	return portNumber;
}

