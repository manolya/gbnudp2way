#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>

#define DATALEN 16
#define N 1024
#define TIMEOUT 2000
#define MAX_ATTEMPTS 4
#define MAX_CWND 4
#define POLL_SIZE 3


/*States*/
enum {CLOSED = 0, SYN_SENT=1, SYN_RCVD=2, CONN_EST=3 , FIN_SENT=4, FIN_RCVD=5, FIN_WAIT=6,

     };

/* PACKET TYPES*/
#define SYN 0
#define SYNACK 1
#define DATA 2
#define DATAACK 3
#define FIN 4
#define FINACK 5
#define RST 6 /* TO REJECT NEW CONNECTION AFTER SIGNALLED TO CLOSE*/

 unsigned long int    noBlock; 


typedef struct gbnpck {
    uint8_t type;
    uint8_t seqnum;
    uint8_t payload[DATALEN];

} gbnpck;

typedef struct connect_state {
    int state;
    uint8_t seqnum;
    struct sockaddr address;
    socklen_t sck_len;
    uint8_t cwnd;    
    int fin;
    int fin_ack;

}connect_state;

connect_state current_state;

struct sockaddr_in serverSock;
struct sockaddr_in clientSock;
socklen_t server_socklen;
socklen_t client_socklen;

int clientfd;
char inputBuff[N];
char outputBuff[N];
ssize_t bytesRead;
int sentBytes = 0;
//closing?
int isRst = 0;
int countingUnacks;
int nonblockfd = 0;

struct pollfd fds[POLL_SIZE];
int numfds = POLL_SIZE;

int ansFlag = -1;

void answerBack(){

	printf("\n starting the ack packets");
	int result = 0;
	gbnpck *ackPkt = malloc(sizeof(*ackPkt));
	memset(ackPkt->payload, '\0', sizeof(ackPkt->payload));
	ackPkt->type = DATAACK;
	ackPkt->seqnum = current_state.seqnum + 1;  
	
	gbnpck *finAckPkt = malloc(sizeof(*finAckPkt));
	memset(finAckPkt->payload, '\0', sizeof(finAckPkt->payload));
	finAckPkt->type = FINACK; 
	finAckPkt->seqnum = current_state.seqnum + 1;

	int isSent = 0;
	switch(ansFlag){

		 case DATAACK:
			 if(sendto(clientfd, ackPkt,sizeof(*ackPkt), 0, (struct sockaddr*)&serverSock, server_socklen ) < 0 ){
				 printf("\n Client: failed to send ack");
				 current_state.state = CLOSED;
			 }
			 else{
				 current_state.seqnum = ackPkt->seqnum;
				 printf("\n Client: sent an ack");
			}
			break;
		 case FINACK:
				if(sendto(clientfd, finAckPkt,sizeof(*finAckPkt), 0, (struct sockaddr*)&serverSock, server_socklen ) < 0 ){
				printf("\n Client: failed to send ack");
				current_state.state = CLOSED;
			}
			else{
				current_state.seqnum = finAckPkt->seqnum;
				current_state.state = CLOSED;
				printf("\nClient: sent a fin  ack");
				isRst = 1;
			}
			break;
		default:
			break;
	}
	
	free(ackPkt);
	free(finAckPkt);


}

int SendDataPkt(){
	printf("\n starting to send data packet");
	gbnpck *dataPkt = (gbnpck*)malloc(sizeof(*dataPkt));
	dataPkt->type = DATA;
	gbnpck *ackPkt = malloc(sizeof(*ackPkt));   
	memset(ackPkt->payload, '\0', sizeof(ackPkt->payload));
	
	size_t offset = 0;
	int attempts = 0;
	int result = -1;
	countingUnacks = 0;
	while(sentBytes > 0){
		if(current_state.state == CONN_EST){
				alarm((uint8_t)(TIMEOUT / 2000));
				for(int i = 0; i < current_state.cwnd; i++)
				{
					size_t remain = sentBytes - DATALEN*i;
					if(remain > 0)
					{
						dataPkt->seqnum= ((uint8_t)current_state.seqnum)+i;
						memset(dataPkt->payload, '\0', sizeof(dataPkt->payload));
						size_t maxDataSize = DATALEN;
						size_t dataSize = remain < maxDataSize ? remain : maxDataSize;
						sentBytes -= maxDataSize;
						memcpy(dataPkt->payload, outputBuff + offset, dataSize);
						
						printf("\n outputBuff: %s", outputBuff);
						printf("\npayload: %s", dataPkt->payload);
						offset += dataSize;
						
						if((result = sendto(clientfd, dataPkt, sizeof(*dataPkt), 0, (struct sockaddr *)&serverSock, server_socklen)) < 0){
							printf("\nClient error: unable to send DATA packet.\n");
							current_state.state = CLOSED;
							break;
						}
						else{
							printf("sent data: %d. \n",(int)dataSize);
							printf("type: %d\tseqnum: %d\n", dataPkt->type, dataPkt->seqnum);
							countingUnacks++;					
						}
					}
					else{
						sentBytes = 0;
						break;
					}
				}
				alarm(0);

			}
			else if(current_state.state == CLOSED){

				printf("\n the connection is closed.");
				return -1;
			}		
		}
		
	return result;
}
void gbnRecv()
{
	gbnpck *synAckPkt = (gbnpck*)malloc(sizeof(*synAckPkt));      
	memset(synAckPkt->payload, '\0', sizeof(synAckPkt->payload));
	
	gbnpck *dataAckPkt =  (gbnpck*)malloc(sizeof(*dataAckPkt));
	memset(dataAckPkt->payload, '\0', sizeof(dataAckPkt->payload));
	
	gbnpck *dataPkt = (gbnpck*)malloc(sizeof(*dataPkt));
	memset(dataPkt->payload,  '\0', sizeof(dataPkt->payload));

	
	int result = -1;
	int dataSize =0;
	int isNewDat =0;


	if(current_state.state == SYN_SENT)
	{
		if((result= recvfrom(clientfd, synAckPkt, sizeof(*synAckPkt), 0,(struct sockaddr*)&serverSock, &server_socklen)) >= 0 )
		{
			if(synAckPkt->type == SYNACK){ 
				printf("\nSYNACK received");		
				
				current_state.state = CONN_EST;
				current_state.seqnum = synAckPkt->seqnum;
			}
		}	

		bytesRead = result;
		return;
	}/*
	else if(current_state.state == FIN_SENT){
		
		if((result=recvfrom(clientfd, dataAckPkt, sizeof(*dataAckPkt), 0, (struct sockaddr*)&serverSock, &server_socklen))>=0){

			if(dataPkt->type == FINACK){

				printf("\nFINACK received");
				current_state.state = FIN_RCVD;
				return;
			}	
		}
		else{
			if(errno != EINTR)
			{
				printf("\n FIN timed out.");
				current_state.state = CONN_EST;
				return;
			}
			else{

				printf("\n FIN TIMEOUT");
				current_state.state = CONN_EST;
				return;
			}

		}
	

	}*/
	else if(current_state.state == CONN_EST)
	{

		if((result = recvfrom(clientfd, dataPkt, sizeof(*dataPkt), 0,(struct sockaddr*)&serverSock, &server_socklen))>= 0)
		{
			switch(dataPkt->type){
				case FIN:
					printf("\n Valid FIN Pkt");
					current_state.state = FIN_RCVD;
					current_state.seqnum = dataPkt->seqnum + (uint8_t)1;
					ansFlag = FINACK;
					break;
				case DATA:
					printf("\n Valid Data Pckt");  
					ansFlag = DATAACK; 
					if(dataPkt->seqnum == (uint8_t)current_state.seqnum){ 
						printf("SUCCESS data in order");
						memcpy(inputBuff, dataPkt->payload, sizeof(dataPkt->payload)); 
						current_state.seqnum = dataPkt->seqnum;
						printf("\n payload: %s, seqnum: %d", inputBuff,current_state.seqnum); 
						isNewDat = 1;
					}
					else{

						printf("\n wrong data sequence, current seqnum=%d\tdata seqnum=%d", current_state.seqnum, dataPkt->seqnum);
					
					}
					break;	
				case DATAACK:
					if(countingUnacks > 0){
						printf("packet, with seq no: %d arrived", dataPkt->seqnum);
						
						int seqnumdiff = (int)dataPkt->seqnum - (int)current_state.seqnum;
						if (seqnumdiff < 0 ) {

							seqnumdiff += 255;

						}	
						countingUnacks -=seqnumdiff;
							
						current_state.seqnum = dataPkt->seqnum;
						if(current_state.cwnd < MAX_CWND){
							current_state.cwnd++;
							printf("cwnd changed %d", current_state.cwnd);
						}
							
					}
					else
					{
						printf("\nwas not expecting ACK");
					}
					break;
				default:
					printf("\n unrecognized incoming packet");
					break;
			}
		}
		else if(errno != EINTR){
			printf("no timeout: %s", strerror(errno));
			current_state.state = CLOSED;
		}
		else {
			printf("timeout!!\n");
			if(current_state.cwnd > 1) {	
				printf("window size: %d\n", current_state.cwnd);
				current_state.cwnd /= 2;
				printf("window size changed");
			}	
		}

	}
	bytesRead = result;
	

}

void starttheChat()
{
	int nullCtr = 0;

	int ret;
	while(1){

		ret = poll(fds, numfds, -1);
		if(isRst == 1){
			printf("\n client user singalled for close connection");
			break;

		}
		if(ret == 0){
			printf("\n Timeout from polling");
			break;
		}
		else if(ret < 0){
	
			printf("\nClient: Error poll return. \n error: %s", strerror(errno));

			break; 
		}
		else if(ret >0){

			if(fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				printf("\nError - poll indicated stdin error\n");
				break;
			}
			else if(fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				printf("\nError - poll indicated socket error\n");
				break;
			}
			else if(fds[0].revents & POLLIN){//console read
				
				memset(outputBuff, 0,N);

				bytesRead = read(fds[0].fd, &outputBuff, (ssize_t)(sizeof(outputBuff)-1));
				if(bytesRead == 1)
				{
					memset(outputBuff, 0,N);


					nullCtr++;
						
					if(nullCtr == 3)
					{
						printf("\n client leaving the chat..");
						gbnpck *finPkt = malloc(sizeof(*finPkt));
						memset(finPkt->payload, '\0', sizeof(finPkt->payload));
						
						finPkt->type = FIN;
						current_state.state = FIN_SENT;
						int res = sendto(clientfd, finPkt, sizeof(*finPkt), 0, (struct sockaddr *)&serverSock, server_socklen);
						if(res > 0)
							break;
						
				
					}
				}	
				else if(bytesRead >1){	

					outputBuff[(ssize_t)(sizeof(outputBuff)-1)] = '\0';
					printf("\nstate=%d\tbytes=%d\t%s",current_state.state, bytesRead,outputBuff);	
					sentBytes = bytesRead;			
					SendDataPkt(); 
				}						
				else
					printf("\nClient: error sending text over socket");
				
			}
			else if(fds[1].revents & POLLIN){

				printf("\nread from socket");
				ansFlag = -1;
				
				gbnRecv();
				answerBack();
				if(current_state.state == FIN_RCVD)
				{
					break;
				}					
				if(ansFlag == DATAACK){  
					inputBuff[N-1] = '\0';
				}				
				else if(ansFlag == FINACK && isRst){
					break;	
				}

			}

			

		}		
	}


}

void startConnection(){

	//syn packets
	current_state.state = SYN_SENT;
	printf("\nconnection started: gbnConnect - > %d..\n", clientfd);
	gbnpck *synPkt = (gbnpck*)malloc(sizeof(*synPkt));
	synPkt->type =SYN;
	synPkt->seqnum = current_state.seqnum;
	char pl[] = "SYN";
	memcpy(synPkt->payload, pl, 4);
	 //syn ack
	int result;
	if(( result = sendto(clientfd, synPkt, sizeof(synPkt), 0, (struct sockaddr *)&serverSock, server_socklen)) >=0){
		/**/
		printf("\n %s", strerror(errno));		
		printf("\n %d, %d, %d", clientfd, serverSock.sin_addr.s_addr, synPkt->seqnum);
		printf("\n Starting the chat:");
		free(synPkt);
	}
	else
	{
		free(synPkt);
		return;
	}

	//printf("\n result: %d, error? %s", result, strerror(errno));
	//printf("\n SYN start");	

	



}

int main(int argc, char *argv[]){

	memset(&serverSock, 0, sizeof(struct sockaddr_in));
        memset(&clientSock, 0, sizeof(struct sockaddr_in));
        current_state = *(connect_state*)malloc(sizeof(connect_state));
        srand(time(0));
        current_state.seqnum = (uint8_t)rand();
        current_state.cwnd = 1;


        if((clientfd= socket(AF_INET, SOCK_DGRAM, 0)) < 0){
                printf("\n Server:socket start error.\n");
                exit(-1);

        }
	if((nonblockfd = socket(AF_INET, SOCK_DGRAM, 0)) <0) {
		
		printf("\n Server:socket start error(nonblockingfd).\n"); 
		exit(-1);                                         
		
	}

        serverSock.sin_family=AF_INET;
	serverSock.sin_addr.s_addr = inet_addr(argv[1]);
        serverSock.sin_port = htons(atoi(argv[2]));
        
	server_socklen = sizeof(struct sockaddr_in);
        	
	clientSock.sin_family = AF_INET;
	clientSock.sin_addr.s_addr = htonl(INADDR_ANY);
	clientSock.sin_port = htons(atoi(argv[2]));
	client_socklen = sizeof(struct sockaddr_in);
	
	fds[0].fd = 0;
	fds[1].fd = clientfd;
	fds[0].events = POLLIN;
	fds[1].events = POLLIN;
	
	fds[2].fd = nonblockfd;
	fds[2].events = POLLOUT;



	//client connect attempt
	startConnection();	
		
	printf("\nstarting the chat");
	starttheChat();

	close(clientfd);

}
