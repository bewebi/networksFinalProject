// Bernie Birnbaum and Shawyoun Saidon
// Comp 112 Final Project
// Tufts University

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string>
#include <fstream> 
#include <vector>
#include <sstream>
#include <iostream>
#include <math.h>
#include <iomanip>
#include "player.h"
#include <openssl/sha.h>

using namespace std;

#define HEADERSIZE 50
#define MAXDATASIZE 400
#define MAXCLIENTS 200
#define IDLENGTH 20 // Includes null character
#define TEAMSIZE 12

#define HELLO 1
#define HELLO_ACK 2
#define LIST_REQUEST 3
#define CLIENT_LIST 4
#define CHAT 5
#define EXIT 6
#define ERROR 7
#define CANNOT_DELIVER 8
#define PLAYER_REQUEST 9
#define PLAYER_RESPONSE 10
#define DRAFT_REQUEST 11
#define PING 12
#define PING_RESPONSE 13
#define START_DRAFT 14
#define DRAFT_STATUS 15
#define DRAFT_STARTING 16
#define DRAFT_ROUND_START 17
#define DRAFT_ROUND_RESULT 18

struct header {
    unsigned short type;
    char sourceID[IDLENGTH];
    char destID[IDLENGTH];
    unsigned int dataLength;
    unsigned int msgID;
}__attribute__((packed, aligned(1)));

struct clientInfo {
    int sock;
    char ID[IDLENGTH];
    bool active;
    int mode;

    int headerToRead;
    char partialHeader[HEADERSIZE];

    int dataToRead;
    int totalDataExpected;
    char partialData[MAXDATASIZE];
    char destID[IDLENGTH];
    int msgID;

    int pwordToRead;
    int totalPwordExpected;
    char pword[65];
    bool validated;

    int pingID;
    int pings;
    struct timeval lastPingSent;
    float estRTT;
    float devRTT;
    float timeout;

    bool readyToDraft;
}__attribute__((packed, aligned(1)));

struct team
{
	char owner[20];
	int playersDrafted;
	timeval adjustedTimeReceived;
	playerInfo players[TEAMSIZE];
};

struct draftInfo {
	int index;
	int currentRound;
	timeval roundEndTime;
	vector<team> teams;
};

struct clientInfo clients[MAXCLIENTS];
int clientCounter = 0;
int numClients = 0;
int maxDelay = 0;
bool pingNow = false;
bool draftStarted;
struct draftInfo theDraft;
timeval roundEndTime;

//struct timeval selectTimeout = {1,0};

vector<playerInfo> playerData;

const float minTimeout = 50.0;
const float alpha = 0.875;
const float beta = 0.25;

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

void readFromClient(int sockfd);

void readHeader(struct clientInfo *curClient, int sockfd);
void readData(struct clientInfo *curClient);
void readPword(struct clientInfo *curClient);

void handleHello(struct clientInfo *curClient);
void handleListRequest(struct clientInfo *curClient);
void handleChat(struct clientInfo *sender);
void handleExit(struct clientInfo *curClient);
void handleClientPresent(struct clientInfo *curClient, char *ID);
void handleCannotDeliver(struct clientInfo *curClient);
void handleError(struct clientInfo *curClient);
void handlePlayerRequest(struct clientInfo *curClient);
void handleDraftRequest(struct clientInfo *curClient);
void handleStartDraft(struct clientInfo *curClient);
void handlePingResponse(struct clientInfo *curClient);

void sendPing(struct clientInfo *curClient);
void sendStartDraft();

void draftNewRound();
void endDraftRound();

string sha256(const string str);

fd_set active_fd_set, read_fd_set; // Declared here so all functions can use

int main(int argc, char *argv[])
{
	// TODO: Give more options than this hard coded file
	playerData = readCSV("nba1516.csv");
	//for(int i = 0; i < playerData.size(); i++) {
		//cout << playerToString(playerData[i]) << '\n';
	//}

    memset(&clients, 0, sizeof(clients));
    int sockfd, newsockfd, portno, pid, i;
    //int clientCounter = 0;
    
    socklen_t clilen;

    struct sockaddr_in serv_addr, cli_addr;
   
    if (argc < 2) {
		fprintf(stderr,"ERROR, no port provided\n");
		exit(1);
    }
    sockfd = socket(AF_INET, SOCK_STREAM, 0); 
    if (sockfd < 0)  
       error("ERROR opening socket"); 
    
    bzero((char *) &serv_addr, sizeof(serv_addr)); 
	portno = atoi(argv[1]); // Get port number from args
	serv_addr.sin_family = AF_INET; 
	serv_addr.sin_addr.s_addr = INADDR_ANY; 
	serv_addr.sin_port = htons(portno); 
    
    if (bind(sockfd, (struct sockaddr *) &serv_addr, 
        sizeof(serv_addr)) < 0)  
            error("ERROR on binding"); 

    listen(sockfd,5);
    FD_ZERO(&active_fd_set);
    FD_SET(sockfd, &active_fd_set);
 
    clilen = sizeof(cli_addr); 
    while(1) {
    	if(draftStarted) {
    		timeval curTime;
    		gettimeofday(&curTime,NULL);

    		fprintf(stderr, "curTime - roundEndTime: %d\n", curTime.tv_sec - theDraft.roundEndTime.tv_sec);
    		if(curTime.tv_sec > theDraft.roundEndTime.tv_sec) {
    			endDraftRound();
    			//break;
    		}
    	}

    	if(pingNow) {
			for(int i = 0; i < MAXCLIENTS; i++) {
				if(clients[i].active) {
					sendPing(&clients[i]);
				}
			}
		}

		//fprintf(stderr, "While again\n");
		pingNow = true;

		struct timeval selectTimeout = {5,0};
		read_fd_set = active_fd_set;
		/* Block until input arrives on one or more active sockets */
		if(select(FD_SETSIZE, &read_fd_set, NULL, NULL, &selectTimeout) < 0) {
		    error("ERROR on select");
		}

		/* Service all the sockets with input pending */
		for(i = 0; i < FD_SETSIZE; i++) { //FD_SETSIZE == 1024
	    	if(FD_ISSET(i, &read_fd_set)) {
	    		pingNow = false;
				if(i == sockfd) {
			    	/* Connection request on original socket */
				    newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
			    	if(newsockfd < 0)
						error("ERROR on accept");

		    		/* make and insert new clientInfo record */
		    		struct clientInfo newClient;
			    	memset(&newClient, 0, sizeof(newClient));
			    	newClient.sock = newsockfd;
			    	newClient.headerToRead = HEADERSIZE;
		    		newClient.active = true;
			    	newClient.validated = false;
			    	newClient.timeout = 5000;
			    	newClient.readyToDraft = false;

				    bool inserted = false;
				    while(!inserted) {
						if(clients[clientCounter].sock == NULL) {
					    	clients[clientCounter] = newClient;
					    	inserted = 1;
						}
					 	clientCounter++;
					 	numClients++;
						clientCounter = clientCounter % MAXCLIENTS;
				    }
		    		fprintf(stderr, "New connection with newsockfd: %d\n", newsockfd);

		    		// fprintf(stderr, "Server: connect from host %s, port %hu. \n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

			    	FD_SET(newsockfd, &active_fd_set);
		 		} else {
			    /* Data arriving on an already-connected socket */
			    	readFromClient(i);
				}
	    	}
		}
    }
    close(sockfd); 
    return 0; 
}  


void readFromClient (int sockfd) {
    int i;
    struct clientInfo *curClient;
    //fprintf(stderr, "sockfd: %d\n", sockfd);

    /* get client address based on sockfd */
    for(i = 0; i < MAXCLIENTS; i++) {
		if(clients[i].sock == sockfd) {
			if(clients[i].active) {
			    curClient = &clients[i];
			    break;
			} else {
				struct clientInfo newClient;
				    memset(&newClient, 0, sizeof(newClient));
				    newClient.sock = sockfd;
			     	newClient.headerToRead = HEADERSIZE;
		     		newClient.active = true;
		     		newClient.validated = false;
				    int inserted = 0;
		     		while(inserted == 0) {
				 		if(clients[clientCounter].sock == NULL) {
				    		clients[clientCounter] = newClient;
					    	inserted = 1;
					 	}
						clientCounter++;
						clientCounter = clientCounter % MAXCLIENTS;
		     		}
		    //fprintf(stderr, "New client made with previously existing sockfd: %d\n", sockfd);
			}
		}
    }

    /* data takes precedence over headers since headerToRead should always 
       be > 0 */
    if(curClient->pwordToRead > 0) {
    	readPword(curClient);
    } else if(curClient->dataToRead > 0) {
		readData(curClient);
    } else if(curClient->headerToRead > 0) {
		readHeader(curClient, sockfd);
    } else {
		error("ERROR: headerToRead and dataToRead not > 0");
    }
}

void readHeader(struct clientInfo *curClient, int sockfd) {
    char header_buffer[HEADERSIZE];
    int nbytes;
    /* retrieve what has already been read of the header */
    memcpy(header_buffer, curClient->partialHeader, HEADERSIZE);
    /* try to read the rest of the header */
    nbytes = read (curClient->sock, &header_buffer[HEADERSIZE-curClient->headerToRead],curClient->headerToRead);

    //fprintf(stderr, "readHeader client: %s, nbytes: %d, expected readsize: %d\n",curClient->ID, nbytes,curClient->headerToRead); 

    if (nbytes <= 0) {
		/* Read error or EOF: Socket closed */
    	// TODO: Pause mode
		handleExit(curClient);
    } else if (nbytes < curClient->headerToRead) {
		/* still more to read */
		curClient->headerToRead = curClient->headerToRead - nbytes;
		memcpy(curClient->partialHeader, header_buffer, HEADERSIZE);
    } else {
        /* Parse header. */
		struct header newHeader;
		memcpy((char *)&newHeader, &header_buffer[0], HEADERSIZE);
	
		newHeader.type = ntohs(newHeader.type);
		newHeader.dataLength = ntohl(newHeader.dataLength);
		newHeader.msgID = ntohl(newHeader.msgID);
		curClient->headerToRead = HEADERSIZE;

		fprintf (stderr, "Read-in header: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", newHeader.type, newHeader.sourceID, newHeader.destID, newHeader.dataLength, newHeader.msgID);

		/* identify potential bad input */
		if((strcmp(newHeader.sourceID,curClient->ID) != 0) && (strcmp(curClient->ID,"") != 0) && curClient->active) {
	    	fprintf(stderr, "ERROR: wrong user ID in header\n");
		    handleError(curClient);
		    return;
		}
		if(strlen(newHeader.sourceID) >= IDLENGTH) {
	    	fprintf(stderr, "ERROR: sourceID is too long\n");
		    handleError(curClient);
		    return;
		}
		if(strlen(newHeader.destID) >= IDLENGTH) {
		    fprintf(stderr, "ERROR: destID is too long\n");
		    handleError(curClient);
	    	return;
		}
		if(newHeader.dataLength > MAXDATASIZE) {
	    	fprintf(stderr, "ERROR: dataLength too large\n");
		    handleError(curClient);
		    return;
		}

		/* next step depends on header type */
		if(newHeader.type == HELLO) {
			//char pword[IDLENGTH];
			//memset(pword,0,IDLENGTH);

		    if(newHeader.dataLength > 20) {
				fprintf(stderr, "ERROR: HELLO has dataLength > 20 \n");
				handleError(curClient);
				return;
		    }


		    /* handle HELLO-specific bad input */
	    	if(strcmp(curClient->ID,"") != 0) {
		    	// If there is already and ID, this is not this client's first interaction with the server
		    	fprintf(stderr, "ERROR: Attempt to HELLO from previously seen client\n");
	    		handleError(curClient);
		    } 
	    
		    if(newHeader.msgID !=0) {
				fprintf(stderr, "ERROR: HELLO has msgID != 0 \n");
				handleError(curClient);
				return;
		    }
	    	if(strcmp(newHeader.destID, "Server") != 0) {
				fprintf(stderr, "ERROR: HELLO not addressed to Server\n");
				handleError(curClient);
				return;
		    }
		    if(strcmp(newHeader.sourceID, "Server") == 0) {
				fprintf(stderr, "ERROR: 'Server' is not a valid ID\n");
				handleError(curClient);
				return;
		    }

	    	// readPword(curClient, newHeader.sourceID);
		    /* look for CLIENT_ALREADY_PRESENT error */
		    int i; int duplicate = 0;
	    	for(i = 0; i < MAXCLIENTS; i++) {
				if(strcmp(clients[i].ID, newHeader.sourceID) == 0) {
					if(clients[i].active) {
						// Can't sign in as active user
						memcpy(curClient->ID, newHeader.sourceID, IDLENGTH);			
						handleClientPresent(curClient, newHeader.sourceID);
						return;
					}
					// Could be client returning
					duplicate = 1;
		    	}
			}
		
			if(duplicate == 0) {
		   		// There is no chance of collision with existing user
			   	curClient->validated = true;
			} else {
				// Need to check for correct password
				curClient->validated = false;
			}

		    curClient->totalPwordExpected = newHeader.dataLength;
		    curClient->pwordToRead = newHeader.dataLength;
		    memcpy(curClient->ID, newHeader.sourceID, IDLENGTH);

		} else if(newHeader.type == LIST_REQUEST) {
		    /* handle LIST_REQUEST-specific bad input */
	    	if(strcmp(curClient->ID,"") == 0) {
				fprintf(stderr,"ERROR: LIST_REQUEST from client without ID\n");
				handleError(curClient);
				return;
		    }
	    	if(newHeader.dataLength != 0) {
				fprintf(stderr, "ERROR: LIST_REQUEST has dataLength != 0\n");
				handleError(curClient);
				return;
	    	}
		    if(newHeader.msgID != 0) {
				fprintf(stderr, "ERROR: LIST_REQUEST has msgID != 0\n");
				handleError(curClient);
				return;
	    	}
		    if(strcmp(newHeader.destID, "Server") != 0) {
				fprintf(stderr, "ERROR: LIST_REQEUST not addressed to Server\n");
				handleError(curClient);
				return;
		    }
		    handleListRequest(curClient);
		} else if(newHeader.type == CHAT) {
		    /* handle CHAT-specific bad input (not related to CANNOT_DELIVER) */
	    	if(newHeader.msgID == 0) {
				fprintf(stderr,"ERROR: CHAT has msgID == 0\n");
				handleError(curClient);
				return;
	    	}
		    if(strcmp(curClient->ID,"") == 0) {
				fprintf(stderr,"ERROR: CHAT from client without ID\n");
				handleError(curClient);
				return;
	    	}
	    
		    /* store information about CHAT in clientInfo struct */
		    curClient->msgID = newHeader.msgID;
	    	memcpy(curClient->destID, newHeader.destID, 20);

		    curClient->mode = CHAT;
		    curClient->dataToRead = newHeader.dataLength;
	    	curClient->totalDataExpected = newHeader.dataLength;
		} else if(newHeader.type == EXIT) {
		    /* No need to check for errors, 
               we're kicking the client out anyway! */
			curClient->active = false;
			curClient->validated = false;
		    handleExit(curClient);
		} else if(newHeader.type == PLAYER_REQUEST) {
			//string augCSV = vectorToAugmentedCSV(playerData);
			handlePlayerRequest(curClient);
		} else if(newHeader.type == DRAFT_REQUEST) {
			memset(curClient->partialData, 0, MAXDATASIZE);
			curClient->mode = DRAFT_REQUEST;
			curClient->dataToRead = newHeader.dataLength;
			curClient->totalDataExpected = newHeader.dataLength;
		}else if(newHeader.type == PING_RESPONSE) {
			memset(curClient->partialData,0,MAXDATASIZE);
			curClient->mode = PING_RESPONSE;
			curClient->msgID = newHeader.msgID;
			curClient->dataToRead = newHeader.dataLength;
			curClient->totalDataExpected = newHeader.dataLength;
		} else if(newHeader.type == START_DRAFT) {
			handleStartDraft(curClient);
		} else {
	    	fprintf(stderr, "ERROR: bad header type\n");
		    handleError(curClient);
		    return;
		}
    }
}

void readData(struct clientInfo *curClient) {
    /* same logic for reading as in readHeader */
    char data_buffer[curClient->totalDataExpected];
    int nbytes;

    memcpy(data_buffer, curClient->partialData, curClient->totalDataExpected);
    nbytes = read (curClient->sock, &data_buffer[curClient->totalDataExpected-curClient->dataToRead],curClient->dataToRead);

    //fprintf(stderr, "nbytes: %d, expected readsize: %d\n",nbytes,curClient->dataToRead); 

    if (nbytes <= 0) {
      /* Read error or EOF */
		handleExit(curClient);
    } else if (nbytes < curClient->dataToRead) {
	/* still more data to read */
		curClient->dataToRead = curClient->dataToRead - nbytes;
		memcpy(curClient->partialData, data_buffer, curClient->totalDataExpected);
    } else {
	/* All data has been read */
		memcpy(curClient->partialData, data_buffer, curClient->totalDataExpected);	
		fprintf(stderr,"Message read in: curClient->partialData: %s\n",curClient->partialData);

		curClient->dataToRead = 0;

		if(curClient->mode == CHAT) {
			handleChat(curClient);
		} else if(curClient->mode == DRAFT_REQUEST) {
			handleDraftRequest(curClient);
		} else if(curClient->mode == PING_RESPONSE) {
			handlePingResponse(curClient);
		} else {
			fprintf(stderr, "ERROR: Done reading data but client in invalid mode\n");
			handleError(curClient);
		}
    }
}

void readPword(struct clientInfo *curClient) {
	char data_buffer[curClient->totalPwordExpected];
	int nbytes;

	memcpy(data_buffer, curClient->pword, curClient->totalPwordExpected);
	nbytes = read (curClient->sock, &data_buffer[curClient->totalPwordExpected - curClient->pwordToRead], curClient->pwordToRead);

	//fprintf(stderr, "nbytes: %d, expected readsize of pword: %d\n", nbytes, curClient->pwordToRead);

	if (nbytes <= 0) {
		/* Read error or EOF */
		handleExit(curClient);
	} else if (nbytes < curClient->pwordToRead) {
		/* still more pword to read */
		curClient->pwordToRead = curClient->pwordToRead - nbytes;
		memcpy(curClient->pword, data_buffer, curClient->totalPwordExpected);
	} else {
		/* entire password read */
		string hashed = sha256(data_buffer);
		char hashBuffer[65];
		for(int i = 0; i < hashed.length(); i++) {
			hashBuffer[i] = hashed[i];
		}
		//fprintf(stderr, "Hashed pword: %s has length %d\n", hashed, hashed.length());

		memcpy(curClient->pword, hashBuffer, hashed.length());
		fprintf(stderr, "Password read in: curClient->password: %s\n", curClient->pword);

		curClient->pwordToRead = 0;

		handleHello(curClient);
	}
}

void handleHello(struct clientInfo *curClient) {
    /* Add client ID to clientInfo */
    // memcpy(curClient->ID, ID, IDLENGTH);
    bool returning = false;

	if(curClient->validated == false) {
		returning = true;
		
		for(int i = 0; i < MAXCLIENTS; i++) {
			if((strcmp(clients[i].ID, curClient->ID) == 0) && !(clients[i].sock == curClient->sock)) {
				fprintf(stderr, "In handleHello, clients[i].ID: %s with sock: %i and curClient->ID: %s with sock: %i\n",
					clients[i].ID,clients[i].sock,curClient->ID,curClient->sock);
				if(strcmp(clients[i].pword, curClient->pword) == 0) {
					fprintf(stderr, "Password matches password of existing client, removing duplicate\n");
					handleError(&clients[i]);
				} else {
					fprintf(stderr, "Password does not match existing client! You're fired!\n");
					handleError(curClient);
					return;
				}
			}
		}
	}

	char helloMessage[50];
	memset(helloMessage,0,50);

	if(returning) {
		strcpy(helloMessage,"Welcome back ");
		memcpy(helloMessage + strlen(helloMessage), curClient->ID, strlen(curClient->ID));
	} else {
		strcpy(helloMessage,"Welcome for the first time ");
		memcpy(helloMessage + strlen(helloMessage), curClient->ID, strlen(curClient->ID));
	}

	helloMessage[strlen(helloMessage)] = '\0';

    /* build HELLO_ACK header */
    struct header responseHeader;
    responseHeader.type = htons(HELLO_ACK);
    strcpy(responseHeader.sourceID, "Server");
    memcpy(responseHeader.destID, curClient->ID, IDLENGTH);
    responseHeader.dataLength = htonl(strlen(helloMessage) + 1);
    responseHeader.msgID = htonl(0);
	
    //fprintf (stderr, "HELLO_ACK responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", responseHeader.type, responseHeader.sourceID, responseHeader.destID, responseHeader.dataLength, responseHeader.msgID);

    /* send HELLO_ACK */
    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    do {
		bytes = write(curClient->sock, (char *)&responseHeader+sent, total-sent);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);
	
	total = strlen(helloMessage) + 1;
    sent = 0;
    while(sent < total) {
        bytes = write(curClient->sock, helloMessage+sent, total-sent);
        if(bytes < 0) error("ERROR writing to socket");
        if(bytes == 0) break;
        sent+= bytes;
        //fprintf(stdout,"Sent %d bytes of the helloMessage\n");
    }

    handleListRequest(curClient);
    //curClient->estRTT = 100;
    //curClient->pings = 50;
    sendPing(curClient);
}

void handleListRequest(struct clientInfo *curClient) {
    /* construct IDs buffer */
    char IDBuffer[MAXCLIENTS * (IDLENGTH + 2)];
    memset(IDBuffer, 0, (MAXCLIENTS * (IDLENGTH + 2)));

    int i, bufferIndex, IDLength;
    bufferIndex = 0;

    /* IDs are added as long as they fit */
    for(i = 0; i < MAXCLIENTS; i++) {
		if(clients[i].sock != NULL) {
		    IDLength = strlen(clients[i].ID);
		    IDLength++;
		    //if((IDLength + bufferIndex) < MAXDATASIZE) {
			strcpy(&IDBuffer[bufferIndex], clients[i].ID);
			bufferIndex = bufferIndex + IDLength;

			if(!(clients[i].active)) {
				IDBuffer[bufferIndex-1] = '*';
				IDBuffer[bufferIndex] = ' ';
				bufferIndex++;
			}
	    //}
		}
    }
	
	//IDBuffer[bufferIndex] = '\0';
	//bufferIndex++;

    /* build CLIENT_LIST header */
    struct header responseHeader;
    responseHeader.type = htons(CLIENT_LIST);
    strcpy(responseHeader.sourceID, "Server");
    strcpy(responseHeader.destID, curClient->ID);
    responseHeader.dataLength = htonl(bufferIndex);
    responseHeader.msgID = htonl(0);
	
    //fprintf (stderr, "CLIENT_LIST responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", responseHeader.type, responseHeader.sourceID, responseHeader.destID, responseHeader.dataLength, responseHeader.msgID);

    /* send CLIENT_LIST header */
    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    do {
	bytes = write(curClient->sock, (char *)&responseHeader+sent, total-sent);
	if(bytes < 0) error("ERROR writing to socket");
	if(bytes == 0) break;
	sent+=bytes;
    } while (sent < total);

    /* send IDBuffer */
    total = bufferIndex; sent = 0;
    do {
	bytes = write(curClient->sock, (char *)&IDBuffer+sent, total-sent);
	if(bytes < 0) error("ERROR writing to socket");
	if(bytes == 0) break;
	sent+=bytes;
    } while (sent < total);
}

void handleChat(struct clientInfo *sender) {
    int i; int badRecipient = 1;
    struct clientInfo *receiver = 0;

    /* find the recipient and make sure they are valid */
    for(i = 0; i < MAXCLIENTS; i++) {
		if(strcmp(clients[i].ID, sender->destID) == 0) {
		    if((strcmp(sender->destID, sender->ID) != 0) && (strcmp(sender->destID,"") != 0)) {
				receiver = &clients[i];
				badRecipient = 0;
	    	}
		}
    }
    
    if(badRecipient == 1) {
		handleCannotDeliver(sender);
		return;
    }

    //sendPing(receiver);

    /* build CHAT header */
    struct header responseHeader;
    responseHeader.type = htons(CHAT);
    strcpy(responseHeader.sourceID, sender->ID);
    strcpy(responseHeader.destID, receiver->ID);
    responseHeader.dataLength = htonl(sender->totalDataExpected);
    responseHeader.msgID = htonl(sender->msgID);
	
    //fprintf (stderr, "CHAT responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", responseHeader.type, responseHeader.sourceID, responseHeader.destID, responseHeader.dataLength, responseHeader.msgID);

    /* send CHAT header */
    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    do {
		bytes = write(receiver->sock, (char *)&responseHeader+sent, total-sent);
		if(bytes < 0) error("ERROR writing to socket");
		//fprintf(stderr,"in handleChat chat header bytes: %d\n", bytes);
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);

    /* send partialData aka the message */
    total = sender->totalDataExpected; sent = 0;
    do {
		bytes = write(receiver->sock, (char *)&sender->partialData[0]+sent, total-sent);
		//write(1, (char *)&sender->partialData[0]+sent, bytes);
		if(bytes < 0) error("ERROR writing to socket");
		//fprintf(stderr,"in handleChat partialData bytes: %d\n", bytes);
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);
}

void handleExit(struct clientInfo *curClient) {

	if(curClient->active && curClient->validated) {
		fprintf(stderr, "exit: active and validated\n");
		int sock = curClient->sock;
		close(sock);
		FD_CLR(sock, &read_fd_set);
		FD_CLR(sock, &active_fd_set);
		curClient->active = false;
		curClient->sock = -1;
	} else {
		if(!curClient->active) fprintf(stderr, "exit: not active\n");
		if(!curClient->validated) fprintf(stderr, "exit: not validated\n");
		int sock = curClient->sock;
		close(sock);
		FD_CLR(sock, &read_fd_set);
		FD_CLR(sock, &active_fd_set);
	    for(int i = 0; i < MAXCLIENTS; i++) {
	    	if(curClient->sock == 0) break;
			if((strcmp(curClient->ID,clients[i].ID) == 0) && (!(clients[i].active)) || !(clients[i].validated)) {
				if(!curClient->validated) {
					for(int i = 0; i < playerData.size(); i++) {
						if(strcmp(curClient->ID,playerData[i].owner) == 0) {
							memset(playerData[i].owner,0,IDLENGTH);
							strcpy(playerData[i].owner,"Server");
						}
					}
				}
			    fprintf(stderr,"permanently removing client %s with sockfd %d\n",curClient->ID,curClient->sock);
			    memset(curClient, 0, sizeof(curClient));
			}
	    }
	    numClients--;
	}
}

void handleClientPresent(struct clientInfo *curClient, char *ID) {
    /* build CLIENT_ALREADY_PRESENT header */
	/*
    struct header responseHeader;
    responseHeader.type = htons(ERROR);
    strcpy(responseHeader.sourceID, "Server");
    memcpy(responseHeader.destID, ID, IDLENGTH);
    responseHeader.dataLength = htonl(0);
    responseHeader.msgID = htonl(0);
	*/
    //fprintf (stderr, "CLIENT_ALREADY_PRESENT responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", responseHeader.type, responseHeader.sourceID, responseHeader.destID, responseHeader.dataLength, responseHeader.msgID);

    /* send CLIENT_ALREADY_PRESENT */
    /*
    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    do {
		bytes = write(curClient->sock, (char *)&responseHeader+sent, total-sent);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);
	*/
    handleExit(curClient);
}

void handleCannotDeliver(struct clientInfo *curClient) {
    /* build CANNOT_DELIVER header */
    struct header responseHeader;
    responseHeader.type = htons(CANNOT_DELIVER);
    strcpy(responseHeader.sourceID, "Server");
    memcpy(responseHeader.destID, curClient->ID, IDLENGTH);
    responseHeader.dataLength = htonl(0);
    responseHeader.msgID = htonl(curClient->msgID);
	
    fprintf (stderr, "CANNOT_DELIVER responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", responseHeader.type, responseHeader.sourceID, responseHeader.destID, responseHeader.dataLength, responseHeader.msgID);

    /* send CANNOT_DELIVER */
    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    do {
	bytes = write(curClient->sock, (char *)&responseHeader+sent, total-sent);
	if(bytes < 0) error("ERROR writing to socket");
	if(bytes == 0) break;
	sent+=bytes;
    } while (sent < total);
}

void handleError(struct clientInfo *curClient) {
	if(curClient->sock != -1) {
	    struct header responseHeader;
    	responseHeader.type = htons(ERROR);
	    strcpy(responseHeader.sourceID, "Server");
    	memcpy(responseHeader.destID, curClient->ID, IDLENGTH);
	    responseHeader.dataLength = htonl(0);
    	responseHeader.msgID = htonl(curClient->msgID);
	
	    fprintf (stderr, "ERROR responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", responseHeader.type, responseHeader.sourceID, responseHeader.destID, responseHeader.dataLength, responseHeader.msgID);

    	/* send ERROR */
	    int bytes, sent, total;
    	total = HEADERSIZE; sent = 0;
	    do {
			bytes = write(curClient->sock, (char *)&responseHeader+sent, total-sent);
			if(bytes < 0) error("ERROR writing to socket");
			if(bytes == 0) break;
			sent+=bytes;
	    } while (sent < total);
    	fprintf(stderr, "ERROR: booting client '%s' with sockfd %d\n",curClient->ID, curClient->sock);
    }

    handleExit(curClient);
}

void handlePlayerRequest(struct clientInfo *curClient) {
	//sendPing(curClient);
	string augCSV = vectorToAugmentedCSV(playerData);
    /* construct IDs buffer */
    char augCSVBuffer[augCSV.size()];
    memset(augCSVBuffer, 0, augCSV.size());

    for(int i = 0; i < augCSV.size(); i++) {
		augCSVBuffer[i] = augCSV.at(i);
    }	

    /* build PLAYER_RESPONSE header */
    struct header responseHeader;
    responseHeader.type = htons(PLAYER_RESPONSE);
    strcpy(responseHeader.sourceID, "Server");
    strcpy(responseHeader.destID, curClient->ID);
    responseHeader.dataLength = htonl(augCSV.size());
    responseHeader.msgID = htonl(0);
	
    //fprintf (stderr, "PLAYER_RESPONSE responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", responseHeader.type, responseHeader.sourceID, responseHeader.destID, responseHeader.dataLength, responseHeader.msgID);

    /* send CLIENT_LIST header */
    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    do {
		bytes = write(curClient->sock, (char *)&responseHeader+sent, total-sent);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);

    /* send augCSVBuffer */
    total = augCSV.size(); sent = 0;
    do {
		bytes = write(curClient->sock, (char *)&augCSVBuffer+sent, total-sent);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);
}

void handleDraftRequest(clientInfo *curClient) {
	//string playerName = curClient->partialData;

	fprintf(stderr, "Recieved draft request from %s for %s\n", curClient->ID, curClient->partialData);
	//playerInfo playerToDraft;
	if(draftStarted) {
		// TODO: Something funky going on here :/
		for(int i = 0; i < theDraft.teams.size(); i++) {
			if(strcmp(theDraft.teams[i].owner,curClient->ID) == 0) {
				fprintf(stderr, "Request from team with owner: %s\n", theDraft.teams[i].owner);
				timeval adjustedTimeReceived;
				gettimeofday(&theDraft.teams[i].adjustedTimeReceived, NULL);

				theDraft.teams[i].adjustedTimeReceived.tv_sec += ((maxDelay - curClient->estRTT) / 1000);
				theDraft.teams[i].adjustedTimeReceived.tv_usec += (((int)(maxDelay - curClient->estRTT) % 1000) * 1000);

				//theDraft.teams[i].adjustedTimeReceived = adjustedTimeReceived;
				fprintf(stderr, "%s's adjustedTimeReceived.tv_sec: %d\n", theDraft.teams[i].owner,theDraft.teams[i].adjustedTimeReceived.tv_sec);
			}
		}
	} else {
//		fprintf(stderr, "Sleeping for %f milliseconds\n", (maxDelay - curClient->estRTT));
//		usleep((maxDelay - curClient->estRTT) * 1000);

		for(int i = 0; i < playerData.size(); i++) {
			if(strcmp(playerData[i].PLAYER_NAME, curClient->partialData) == 0) {
				if(strcmp(playerData[i].owner,"Server") == 0) {
					strcpy(playerData[i].owner,curClient->ID);
				}

				fprintf(stderr, "The owner of %s is now %s\n",playerData[i].PLAYER_NAME,playerData[i].owner);
			}
		}

		for(int i = 0; i < MAXCLIENTS; i++) {
			if(clients[i].active) {
				//sendPing(&clients[i]);
				handlePlayerRequest(&clients[i]);		
			}
		}
	}
}

void sendPing(clientInfo *curClient) {
	//struct  timeval startTime;
	//char timeBuffer[sizeof(startTime)];

	struct header responseHeader;
	memset(&responseHeader,0,sizeof(responseHeader));
    responseHeader.type = htons(PING);
    strcpy(responseHeader.sourceID, "Server");
    strcpy(responseHeader.destID, curClient->ID);
    responseHeader.dataLength = htonl(0);
    responseHeader.msgID = htonl(++curClient->pingID);

    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    do {
	    gettimeofday(&curClient->lastPingSent, NULL);
		bytes = write(curClient->sock, (char *)&responseHeader+sent, total-sent);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);

    //readHeader(curClient, curClient->sock);
    //readData(curClient);
    //memcpy(timeBuffer,&startTime,sizeof(startTime));
}

void handleStartDraft(clientInfo *curClient) {
	curClient->readyToDraft = !curClient->readyToDraft;
	string s;

	int readyClients = 0;
	int totalClients = 0;

	for(int i = 0; i < MAXCLIENTS; i++) {
		if(clients[i].active) {
			totalClients++;
			if(clients[i].readyToDraft) {
				readyClients++;
			}
		}
	}

	if(!draftStarted) {
		if(curClient->readyToDraft) {
			s = "You, "; s += curClient->ID; s += ", are ready to draft! ";
		} else {
			s = "You, "; s += curClient->ID; s += ", are not ready to draft. ";
		}

		s += "There are "; s += to_string(readyClients); s += " clients ready and ";
		s += to_string(totalClients - readyClients); s += " that are not ready.\n";
	} else {
		s = "The draft is already underway!\n";
	}

	char stringBuffer[s.length() + 1];
	strcpy(stringBuffer, s.c_str());
	stringBuffer[s.length()] = '\0';

	struct header responseHeader;
	memset(&responseHeader,0,sizeof(responseHeader));
    responseHeader.type = htons(DRAFT_STATUS);
    strcpy(responseHeader.sourceID, "Server");
    strcpy(responseHeader.destID, curClient->ID);
    responseHeader.dataLength = htonl(s.length() + 1);
    responseHeader.msgID = htonl(0);

    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    do {
		bytes = write(curClient->sock, (char *)&responseHeader+sent, total-sent);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);
	
	total = strlen(stringBuffer) + 1;
    sent = 0;
    while(sent < total) {
        bytes = write(curClient->sock, stringBuffer+sent, total-sent);
        if(bytes < 0) error("ERROR writing to socket");
        if(bytes == 0) break;
        sent+= bytes;
    }

    if(totalClients == readyClients) {
    	sendStartDraft();
    	draftStarted = true;
    }
}

void sendStartDraft() {
	memset(&theDraft,0,sizeof(theDraft));

	fprintf(stderr, "In sendStartDraft\n");
	struct header responseHeader;
	memset(&responseHeader,0,sizeof(responseHeader));
    responseHeader.type = htons(DRAFT_STARTING);
    strcpy(responseHeader.sourceID, "Server");
    responseHeader.msgID = htonl(0);

    string s;
    s = "The draft is now starting with "; s += to_string(clientCounter); s += " clients!";

   	char stringBuffer[s.length() + 1];
	strcpy(stringBuffer, s.c_str());
	stringBuffer[s.length()] = '\0';

    responseHeader.dataLength = htonl(s.length() + 1);

    for(int i = 0; i < MAXCLIENTS; i++) {
    	fprintf(stderr, "In for with i=%d\n", i);
    	if(clients[i].active) {
    		//fprintf(stderr, "In for with active client: %s\n", clients[i].ID);
		    strcpy(responseHeader.destID, clients[i].ID);
    		
		    int bytes, sent, total;
		    total = HEADERSIZE; sent = 0;
		    do {
				bytes = write(clients[i].sock, (char *)&responseHeader+sent, total-sent);
				if(bytes < 0) error("ERROR writing to socket");
				if(bytes == 0) break;
				sent+=bytes;
		    } while (sent < total);

		   	total = strlen(stringBuffer) + 1;	
		    sent = 0;
		    while(sent < total) {
		        bytes = write(clients[i].sock, stringBuffer+sent, total-sent);
		        if(bytes < 0) error("ERROR writing to socket");
		        if(bytes == 0) break;
		        sent+= bytes;
		    }

		    struct team newTeam;
		    memset(&newTeam,0,sizeof(newTeam));
		    strcpy(newTeam.owner, clients[i].ID);
		    theDraft.teams.push_back(newTeam);
    	}
    }

   	draftNewRound();
}

void handlePingResponse(clientInfo *curClient) {
	struct timeval sentTime, curTime;
	gettimeofday(&curTime, NULL);
	memcpy((char *)&sentTime,curClient->partialData,sizeof(sentTime));

	int delayms = ((curTime.tv_sec * 1000) + (curTime.tv_usec / 1000)) - ((curClient->lastPingSent.tv_sec * 1000) + (curClient->lastPingSent.tv_usec / 1000));

	//fprintf(stderr, "Delay between ping response sent and ping response recieved: %d\n", delayms);
	//fprintf(stderr, "Ping recieved: %d, curClient->pingID: %d\n", curClient->msgID, curClient->pingID);
	if(curClient->msgID != curClient->pingID) return;

	if(delayms < curClient->timeout) {
		if(curClient->estRTT == 0) {
			curClient->estRTT = delayms;
		} else {
			curClient->estRTT = (alpha * curClient->estRTT) + (delayms * (1.0 - alpha));
		}

		curClient->devRTT = (beta * fabs(delayms - curClient->estRTT)) + ((1.0 - beta) * curClient->devRTT);
		curClient->timeout = max((curClient->estRTT + (4 * curClient->devRTT)), minTimeout);

		fprintf(stderr, "Client %s has estRTT of %f, devRTT of %f, and timeout of %f\n", curClient->ID,curClient->estRTT,curClient->devRTT,curClient->timeout);
		if(curClient->timeout > maxDelay) {
			maxDelay = curClient->timeout;
		}
	}

	if(curClient->pings-- > 0) {
		sendPing(curClient);
	}
}

string sha256(const string str) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, str.c_str(), str.size());
    SHA256_Final(hash, &sha256);
    stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return ss.str();
}

void draftNewRound() {
    theDraft.currentRound++;
	char curPlayer[50];
	memset(curPlayer,0,50);
	memcpy(curPlayer,playerData[theDraft.index].PLAYER_NAME,50);

	fprintf(stderr, "curPlayer in draftNewRound: %s\n", curPlayer);
	struct header responseHeader;
	memset(&responseHeader,0,sizeof(responseHeader));
    responseHeader.type = htons(DRAFT_ROUND_START);
    strcpy(responseHeader.sourceID, "Server");
    responseHeader.dataLength = htonl(strlen(curPlayer) + 1);
    responseHeader.msgID = htonl(theDraft.currentRound);	

    for(int i = 0; i < MAXCLIENTS; i++) {
    	if(clients[i].active) {
		    strcpy(responseHeader.destID, clients[i].ID);

			int bytes, sent, total;
		    total = HEADERSIZE; sent = 0;
		    do {
				bytes = write(clients[i].sock, (char *)&responseHeader+sent, total-sent);
				if(bytes < 0) error("ERROR writing to socket");
				if(bytes == 0) break;
				sent+=bytes;
		    } while (sent < total);

		   	total = strlen(curPlayer) + 1;	
		    sent = 0;
		    while(sent < total) {
		        bytes = write(clients[i].sock, curPlayer+sent, total-sent);
		        if(bytes < 0) error("ERROR writing to socket");
		        if(bytes == 0) break;
		        sent+= bytes;
		    }
		}
    }
        
    for(int i = 0; i < theDraft.teams.size(); i++) {
    		theDraft.teams[i].adjustedTimeReceived.tv_sec = 0;
	   		theDraft.teams[i].adjustedTimeReceived.tv_usec = 0;
    }

    theDraft.index++;
    //timeval fiveSecs; fiveSecs.tv_sec = 5; fiveSecs.tv_usec = 0;
    timeval roundTotalTime; roundTotalTime.tv_sec = (maxDelay / 1000) + 10; roundTotalTime.tv_usec = (maxDelay % 1000) * 1000;
    fprintf(stderr, "roundTotalTime.tv_sec: %d\n", roundTotalTime.tv_sec);
    timeval curTime;

    gettimeofday(&curTime, NULL);

    timeval roundEndTime;
    timeradd(&roundTotalTime,&curTime,&roundEndTime);
    //roundEndTime.tv_sec = roundTotalTime.tv_sec + curTime.tv_sec + ((roundTotalTime.tv_usec + curTime.tv_sec) / 1000000);
    //roundEndTime.tv_usec = (roundTotalTime.tv_usec + curTime.tv_usec) % 1000000;
    theDraft.roundEndTime = roundEndTime;
}

void endDraftRound() {
	fprintf(stderr, "In endDraftRound\n");
	char winner[20];
	memset(winner,0,20);

	timeval quickest;
	gettimeofday(&quickest, NULL);
	fprintf(stderr, "quickest has tv_sec: %d\n", quickest.tv_sec);

	for(int i = 0; i < theDraft.teams.size(); i++) {
		fprintf(stderr, "Team %s had adjustedTimeReceived.tv_sec: %d\n", theDraft.teams[i].owner, theDraft.teams[i].adjustedTimeReceived.tv_sec);

		if(theDraft.teams[i].adjustedTimeReceived.tv_sec != 0) {
			fprintf(stderr, "First if statement (tv_sec != 0)\n");

			if(timercmp(&theDraft.teams[i].adjustedTimeReceived,&quickest, <)) {
				fprintf(stderr, "New fastest is: %s\n", theDraft.teams[i].owner);
				quickest = theDraft.teams[i].adjustedTimeReceived;
				strcpy(winner,theDraft.teams[i].owner);
			} 
		}
	}

	if(strcmp(winner, "") != 0) {
		strcpy(playerData[theDraft.index].owner,winner);
		fprintf(stderr, "%s won %s in round %d of the draft\n", winner, playerData[theDraft.index].PLAYER_NAME, theDraft.currentRound);
	}

	fprintf(stderr, "End of endDraftRound\n");

}