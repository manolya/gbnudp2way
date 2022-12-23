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
#define TIMEOUT 100
#define MAX_ATTEMPTS 4
#define MAX_CWND 4
#define POLL_SIZE 2

/*States*/
enum {CLOSED = 0, SYN_SENT=1, SYN_RCVD=2, CONN_EST=3, FIN_SENT=4, FIN_RCVD=5, FIN_WAIT=6,

     };

/* PACKET TYPES*/
#define SYN 0
#define SYNACK 1
#define DATA 2
#define DATAACK 3
#define FIN 4
#define FINACK 5
#define RST 6 /* TO REJECT NEW CONNECTION AFTER SIGNALLED TO CLOSE*/

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

int listenfd;
char inputBuff[N];
char outputBuff[N];
ssize_t bytesRead;
int sendBytes = 0;
int isrst = 0;
int ansFlag = -1;
int countingUnacks = 0;
void answerBack(){

	printf("\n starting the ack packets");
	int result = 0;
	gbnpck *synAckPkt = malloc(sizeof(*synAckPkt));
	memset(synAckPkt->payload, '\0', sizeof(synAckPkt->payload));
	synAckPkt->type = SYNACK;
	synAckPkt->seqnum = current_state.seqnum + 1;	
	
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
		case SYNACK:
			printf("\nsending SYN ACK");
			
			if((result  = sendto(listenfd, synAckPkt, sizeof(*synAckPkt),0, (struct sockaddr *)&clientSock, client_socklen)) >= 0){
						
				printf("\nSUCCESS of connection est");
				current_state.state = CONN_EST;
				current_state.seqnum = synAckPkt->seqnum;
				ansFlag = SYNACK;
				isSent = 1;			
				
			}				
			else {
				if(errno != EINTR){
					printf("\n sending issue, %s", strerror(errno));
					current_state.state = CLOSED;
					isSent = 1;;
					break;
					
				}
				printf("\n problem: %s", strerror(errno));
			}	

			break;
		case DATAACK:


			if(sendto(listenfd, ackPkt,sizeof(*ackPkt), 0, (struct sockaddr*)&clientSock, client_socklen ) < 0 ){

				
				printf("\n Server: failed to send ack");
				current_state.state = CLOSED;
				
				

			}
			else{
				current_state.seqnum = ackPkt->seqnum;
				printf("\n Server: sent an ack");

			}
			break;

		case FINACK:
			if(sendto(listenfd, finAckPkt,sizeof(*finAckPkt), 0, (struct sockaddr*)&clientSock, client_socklen ) < 0 ){
				printf("\n Server: failed to send ack");
				current_state.state = CLOSED;
			}
			else{
				current_state.seqnum = finAckPkt->seqnum;
				current_state.state = CLOSED;
				printf("\n Server: sent an ack");
				isrst = 1;


			}
			break;

		default:
			break;
		
	}
	
	free(synAckPkt);
	free(ackPkt);
	free(finAckPkt);
	
}


int SendDataPkt(){

        printf("\n starting to send data packet");
        gbnpck *dataPkt = (gbnpck*)malloc(sizeof(*dataPkt));
        dataPkt->type = DATA;
	gbnpck *ackPkt = (gbnpck*)malloc(sizeof(*ackPkt));
	memset(ackPkt->payload, '\0', sizeof(ackPkt->payload));
		
        size_t offset = 0;
        countingUnacks = 0;
        int attempts = 0;
        int result = -1;
        while(sendBytes > 0){
                if(current_state.state == CONN_EST){

			alarm((uint8_t)(TIMEOUT / 2000));
			for(int i = 0; i < current_state.cwnd; i++){

				size_t remain = sendBytes - DATALEN*i;
				if(remain > 0){
					dataPkt->seqnum= ((uint8_t)current_state.seqnum)+i;
					memset(dataPkt->payload, '\0', sizeof(dataPkt->payload));
					size_t maxDataSize = DATALEN;
					size_t dataSize = remain < maxDataSize ? remain : maxDataSize;
					sendBytes -= maxDataSize;
					memcpy(dataPkt->payload, outputBuff + offset, dataSize);
					printf("\n outputBuff: %s", outputBuff);
					printf("\npayload: %s", dataPkt->payload);
					offset += dataSize;

					if((result = sendto(listenfd, dataPkt, sizeof(*dataPkt),0, (struct sockaddr *)&clientSock, client_socklen)) < 0){
						printf("\nClient error: unable to send DATA packet.\n");
						current_state.state = CLOSED;
						break;
					}
					else {
						printf("sent data: %d. \n",(int)dataSize);
						printf("type: %d\tseqnum: %d\n", dataPkt->type, dataPkt->seqnum);
						countingUnacks++;


					}


				}
				else {
					sendBytes = 0;
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



void gbnRecv() {

	gbnpck *dataPkt = malloc(sizeof(*dataPkt));
	memset(dataPkt->payload, '\0', sizeof(dataPkt->payload));

		
	
	int result  = -1;

	if(current_state.state == CLOSED)
	{
	
		gbnpck *synPkt = malloc(sizeof(*synPkt));
		memset(synPkt->payload, '\0', sizeof(synPkt->payload));

		
		if(recvfrom(listenfd, synPkt, sizeof(*synPkt), 0, (struct sockaddr*)&clientSock, &client_socklen) >=  0){
				
			printf("\n SYN ");      
			if(synPkt->type == SYN){ 
				printf("\n Valid SYN Pkt");       
				current_state.seqnum = synPkt->seqnum  + (uint8_t)+1;
				current_state.state = SYN_RCVD;
				
				printf("\n seqno = %d", (uint8_t)synPkt->seqnum);
				ansFlag = SYNACK;
				return;	
			}
		}
		else{
			printf("\n issue with Syn Pkt receipt: %s", strerror(errno));
			current_state.state = CLOSED;
		}	
		
	
		if(current_state.state == SYN_RCVD)
			bytesRead = 	SYN_RCVD;
		else{
			bytesRead = -1;
		}

		return;
	}
	else if (current_state.state == CONN_EST){
	size_t dataSize=0;
	int isNewDat = 0;
		if((result = recvfrom(listenfd, dataPkt, sizeof(*dataPkt), 0,(struct sockaddr*)&serverSock, &client_socklen))>= 0){
			switch(dataPkt->type){
				case FIN:
					printf("\n Valid Fin Pkt");
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
						printf("\n payload: %s, seqnum: %d", inputBuff, current_state.seqnum);
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
					else{
						printf("\nwas not expecting ACK");
					}	
					break;
				default:
					printf("\n unrecognized incoming packet");
			}

		}
		else if(errno != EINTR){
		printf("\n receive error: %s", strerror(errno));
		current_state.state = CLOSED;
	
		}
		else{
			printf("timeout!!\n");
			if(current_state.cwnd > 1) {
				printf("window size: %d\n", current_state.cwnd);
				current_state.cwnd /= 2;
				printf("window size changed");
			}
		}

	}

	free(dataPkt);
	bytesRead = result;
	
}

void starttheChat()
{

       	struct pollfd pollset[POLL_SIZE];
        pollset[0].fd = 0;
        pollset[0].events = POLLIN;
        pollset[1].fd = listenfd;
        pollset[1].events = POLLIN;

        int numfds = POLL_SIZE;
	int nullCtr = 0;
        
	while(1){

		int ret = poll(pollset, numfds, 30000);
                if(isrst == 1){

                        printf("\n Server user signalled for close connection.");
                        break;

                }
                if(ret == 0){

                        printf("\n Timeout from polling");
                        break;
                }
                else if(ret < 0){

                        printf("\n Server: Error poll return. \n error: %s", strerror(errno));
                        break;
                }
                else if(ret > 0){

                        if(pollset[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                                printf("\nError - poll indicated stdin error\n");
                                break;
                        }
			if(pollset[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                                printf("\nError - poll indicated socket error\n");
                                break;
                        }
			if(pollset[0].revents & POLLIN)
                        {

		       		memset(outputBuff, 0,N);
	       			bytesRead = read(pollset[0].fd, &outputBuff, (ssize_t)(sizeof(outputBuff)-1));
			       	if(bytesRead == 1){

					memset(outputBuff, 0,N);
					nullCtr++;
					if(nullCtr == 3){
						printf("\n server leaving the chat..");
						gbnpck *finPkt = malloc(sizeof(*finPkt));
						memset(finPkt->payload, '\0', sizeof(finPkt->payload));
						finPkt->type = FIN;
						current_state.state = FIN_SENT;
						int res = sendto(listenfd, finPkt, sizeof(*finPkt), 0, (struct sockaddr *)&clientSock, client_socklen);
						if(res > 0)
							break;	
					}

				}
				else if(bytesRead >1){
					 outputBuff[(ssize_t)(sizeof(outputBuff)-1)] = '\0';
					 printf("\nstate=%d\tbytes=%d\t%s",current_state.state, bytesRead,outputBuff);
					sendBytes = bytesRead;
					SendDataPkt();
				}
				else
					printf("\nServer: error sending text over socket");


				
                         }
			if(pollset[1].revents & POLLIN){

                                printf("\n read from socket");
				
				ansFlag = -1;
				//receive operations				
				gbnRecv();

				answerBack();
				if(current_state.state == FIN_RCVD)
					break;
				if(ansFlag == DATAACK){
					inputBuff[N-1] = '\0';
				}
				else if(ansFlag == FINACK && isrst)
				{
					break;
				}


                        }

                }


	}

}


int main(int argc, char *argv[]){

        memset(&serverSock, 0, sizeof(struct sockaddr_in));
 	current_state = *(connect_state*)malloc(sizeof(connect_state));
        srand(time(0));
        current_state.seqnum = (uint8_t)rand();
        current_state.cwnd = 1;
        
        if((listenfd= socket(AF_INET, SOCK_DGRAM, 0)) < 0){
                printf("\n Server:socket start error.\n");
                exit(-1);

        }

        serverSock.sin_family=AF_INET;
        serverSock.sin_addr.s_addr = htonl(INADDR_ANY);
        serverSock.sin_port = htons(atoi(argv[1]));
        server_socklen = sizeof(struct sockaddr_in);

	client_socklen = sizeof(clientSock);
        printf("\nadr:%d \t port:%d \n",serverSock.sin_addr.s_addr, ntohs(serverSock.sin_port));
        if( bind(listenfd, (struct sockaddr *)&serverSock, server_socklen ) < 0)
        {
                printf("\n Server:socket bind error");
                exit(-1);
        }


	starttheChat();	
	close(listenfd);
	return 0;

}
