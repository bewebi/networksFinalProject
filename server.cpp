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
#include <time.h>
#include <algorithm>
#include <random>

using namespace std;

#define MAXDATASIZE 400
#define MAXCLIENTS 200
#define IDLENGTH 20 // Includes null character
#define TEAMSIZE 12
#define ROUNDTIME 10 // Max of 255
#define PARTICIPATING_MASK 256

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
#define DRAFT_PASS 19
#define DRAFT_END 20


struct header {
    unsigned short type;
    char sourceID[IDLENGTH];
    char destID[IDLENGTH];
    unsigned int dataLength;
    unsigned int msgID;
}__attribute__((packed, aligned(1)));

#define HEADERSIZE sizeof(header)

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
    struct timespec lastPingSent;
    float estRTT;
    float devRTT;
    float timeout;

    bool readyToDraft;
}__attribute__((packed, aligned(1)));

struct team
{
	char owner[20];
	int playersDrafted;
	bool responseRecieved;
	timespec adjustedTimeReceived;
	playerInfo players[TEAMSIZE];
};

struct draftInfo {
	int index;
	int currentRound;
	timespec roundEndTime;
	vector<team> teams;
	vector<int> order;
};

struct clientInfo clients[MAXCLIENTS];
int clientCounter = 0;
int numClients = 0; int numActiveClients = 0;
int maxDelay = 0;
int draftNum = 0;
bool timedOut = false;
bool draftStarted;
bool startDraft = false;
bool startNewRound = false;
struct draftInfo theDraft;

vector<playerInfo> playerData;

const float minTimeout = 50.0;
const float alpha = 0.875;
const float beta = 0.25;

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

int max(int a, int b) {
	if(a > b) return a;
	return b;
}

int min(int a, int b) {
	if(a < b) return a;
	return b;
}

// 1. All purpose method for new read from a client
void readFromClient(int sockfd);

// 2. Methods for reading specifc parts of messages
void readHeader(struct clientInfo *curClient, int sockfd);
void readData(struct clientInfo *curClient);
void readPword(struct clientInfo *curClient);

// 3. Methods for handling particular types of messages
void handleHello(struct clientInfo *curClient);
void handleListRequest(struct clientInfo *curClient);
void handleChat(struct clientInfo *sender);
void handleExit(struct clientInfo *curClient);
void handleClientPresent(struct clientInfo *curClient, char *ID);
void handleCannotDeliver(struct clientInfo *curClient);
void handleError(struct clientInfo *curClient);
void handlePlayerRequest(struct clientInfo *curClient);
void handleDraftRequest(struct clientInfo *curClient);
void handleDraftPass(struct clientInfo *curClient);
void handleStartDraft(struct clientInfo *curClient);
void handlePingResponse(struct clientInfo *curClient);

// 4. Method for sending pings to all clients
void sendPing(struct clientInfo *curClient);

// 5. Methods called by the server to state of the draft
void sendStartDraft();
void draftNewRound();
void endDraftRound();
void endDraft();

// 6. Helper function, returns true if conditions for ending the current draft round are met
bool roundIsOver();

// 7. Helper functions for dealing with timespecs
void timespecAdd(timespec *a, timespec *b, timespec *c);
void timespecSubtract(timespec *a, timespec *b, timespec *c);
bool timespecLessthan(timespec *a, timespec *b);

// 8. Function for hashing passwords
string sha256(const string str);

fd_set active_fd_set, read_fd_set; // Declared here so all functions can use

/********************************************************************
 *							Section 0: main							*
 ********************************************************************/
int main(int argc, char *argv[]) {
	srand(unsigned (time(0)));

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
    	fprintf(stderr, "Top of while\n");
   		timespec curTime;
   		clock_gettime(CLOCK_MONOTONIC,&curTime);

		timespec timeToEndRound;
    	if(draftStarted && !startNewRound) {
    		fprintf(stderr, "draftStarted && !startNewRound\n");
    		if(timespecLessthan(&theDraft.roundEndTime,&curTime)) {
    			fprintf(stderr, "About to endDraftRound\n");
    			endDraftRound();
    			continue;
    		} else {
    			fprintf(stderr, "About to timespecSubtract\n");
				timespecSubtract(&theDraft.roundEndTime,&curTime,&timeToEndRound);
				fprintf(stderr, "Time til next round: %d.%ds\n", timeToEndRound.tv_sec,timeToEndRound.tv_nsec);
			}
    	} else {
    		fprintf(stderr, "!draftStarted || startNewRound\n");
    		timeToEndRound.tv_sec = 99999;
    		timeToEndRound.tv_nsec = 0;
    	}

    	if(timedOut && !startNewRound) {
			for(int i = 0; i < MAXCLIENTS; i++) {
				if(clients[i].active && clients[i].validated) {
					if(!draftStarted || (clients[i].pings < 5) || ((curTime.tv_sec - clients[i].lastPingSent.tv_sec) > 30)) {
						sendPing(&clients[i]);
					}
				}
			}
		}

		timedOut = true;

		int timeoutSecs = min(max(((int)(maxDelay + 500) / 1000), 5), (timeToEndRound.tv_sec + 1) );
		fprintf(stderr, "timeoutSecs: %d\n", timeoutSecs);
		struct timeval selectTimeout = {timeoutSecs,(timeToEndRound.tv_nsec / 1000)};
		read_fd_set = active_fd_set;
		/* Block until input arrives on one or more active sockets */
		if(select(FD_SETSIZE, &read_fd_set, NULL, NULL, &selectTimeout) < 0) {
		    error("ERROR on select");
		}

		/* Service all the sockets with input pending */
		for(i = 0; i < FD_SETSIZE; i++) { //FD_SETSIZE == 1024
	    	if(FD_ISSET(i, &read_fd_set)) {
	    		timedOut = false;
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
			    	newClient.timeout = 50000;
			    	newClient.readyToDraft = false;

				    bool inserted = false;
				    while(!inserted) {
						if(clients[clientCounter].sock == NULL) {
					    	clients[clientCounter] = newClient;
					    	inserted = 1;
						}
					 	clientCounter++;
					 	numClients++;
					 	numActiveClients++;
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
//	    if(timedOut) fprintf(stderr, "Timed out select\n");
		if(startDraft && timedOut) sendStartDraft();
		if(startNewRound && timedOut) draftNewRound();
    }
    close(sockfd); 
    return 0; 
}  

/********************************************************************
 *				Section 1: Determine which read to use				*
 ********************************************************************/
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
						numActiveClients++;
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


/********************************************************************
 *			Section 2: Read specific parts of messages				*
 ********************************************************************/
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

		fprintf (stderr, "Header read in: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", newHeader.type, newHeader.sourceID, newHeader.destID, newHeader.dataLength, newHeader.msgID);

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

		    /* look for CLIENT_ALREADY_PRESENT error */
		    bool duplicate = false;
	    	for(int i = 0; i < MAXCLIENTS; i++) {
				if(strcmp(clients[i].ID, newHeader.sourceID) == 0) {
					if(clients[i].active) {
						// Can't sign in as active user
						memcpy(curClient->ID, newHeader.sourceID, IDLENGTH);			
						handleClientPresent(curClient, newHeader.sourceID);
						return;
					}
					// Could be client returning
					duplicate = true;
		    	}
			}
		
			if(!duplicate) {
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
			numActiveClients--;
			curClient->validated = false;
		    handleExit(curClient);

		} else if(newHeader.type == PLAYER_REQUEST) {
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

		} else if(newHeader.type == DRAFT_PASS) {
			memset(curClient->partialData, 0, MAXDATASIZE);
			curClient->mode = DRAFT_PASS;
			curClient->dataToRead = newHeader.dataLength;
			curClient->totalDataExpected = newHeader.dataLength;

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
		//fprintf(stderr,"Message read in: curClient->partialData: %s\n",curClient->partialData);

		curClient->dataToRead = 0;

		if(curClient->mode == CHAT) {
			handleChat(curClient);
		} else if(curClient->mode == DRAFT_REQUEST) {
			handleDraftRequest(curClient);
		} else if(curClient->mode == PING_RESPONSE) {
			handlePingResponse(curClient);
		} else if(curClient->mode == DRAFT_PASS) {
			handleDraftPass(curClient);
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
		//fprintf(stderr, "Password read in: curClient->password: %s\n", curClient->pword);

		curClient->pwordToRead = 0;

		handleHello(curClient);
	}
}


/********************************************************************
 *			Section 3: Handlers for specific message types			*
 ********************************************************************/
void handleHello(struct clientInfo *curClient) {
    /* Add client ID to clientInfo */
    // memcpy(curClient->ID, ID, IDLENGTH);
    bool returning = false;

	if(curClient->validated == false) {
		returning = true;
		
		for(int i = 0; i < MAXCLIENTS; i++) {
			if((strcmp(clients[i].ID, curClient->ID) == 0) && !(clients[i].sock == curClient->sock)) {
				//fprintf(stderr, "In handleHello, clients[i].ID: %s with sock: %i and curClient->ID: %s with sock: %i\n",
				//	clients[i].ID,clients[i].sock,curClient->ID,curClient->sock);
				if(strcmp(clients[i].pword, curClient->pword) == 0) {
					//fprintf(stderr, "Password matches password of existing client, removing duplicate\n");
					curClient->validated = true;
					curClient->readyToDraft = clients[i].readyToDraft;
					handleError(&clients[i]);
				} else {
					//fprintf(stderr, "Password does not match existing client! You're fired!\n");
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
    if(draftStarted && curClient->readyToDraft) {
    	responseHeader.msgID = htonl(1);
    } else {
	    responseHeader.msgID = htonl(0);
	}
	
    //fprintf (stderr, "HELLO_ACK responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", responseHeader.type, responseHeader.sourceID, responseHeader.destID, responseHeader.dataLength, responseHeader.msgID);

    /* send HELLO_ACK */
    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    fprintf(stderr, "handleHello: Writing to %s with sock %d\n", curClient->ID,curClient->sock);
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
    //sendPing(curClient);

    if(draftStarted && curClient->readyToDraft) {
    	struct header responseHeader;
		memset(&responseHeader,0,sizeof(responseHeader));
	    responseHeader.type = htons(DRAFT_STARTING);
	    strcpy(responseHeader.destID, curClient->ID);
	    strcpy(responseHeader.sourceID, "Server");
	    responseHeader.msgID = htonl(ROUNDTIME | PARTICIPATING_MASK);

	    string s;
    	s = "Welcome back to the draft! Round "; s += to_string(theDraft.index+1); s += " is underway.\n";

	   	char stringBuffer[s.length() + 1];
		strcpy(stringBuffer, s.c_str());
		stringBuffer[s.length()] = '\0';

	    responseHeader.dataLength = htonl(s.length() + 1);

        int bytes, sent, total;
	    total = HEADERSIZE; sent = 0;	
	    fprintf(stderr, "handleHello (draftStarted): Writing to %s with sock %d\n", curClient->ID,curClient->sock);
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
    }

    if(draftStarted && !curClient->readyToDraft) {
	   	struct header responseHeader;
		memset(&responseHeader,0,sizeof(responseHeader));
	    responseHeader.type = htons(DRAFT_STARTING);
	    strcpy(responseHeader.destID, curClient->ID);
	    strcpy(responseHeader.sourceID, "Server");
	    responseHeader.msgID = htonl(ROUNDTIME);

	    string s;
    	s = "Round "; s += to_string(theDraft.index+1); s += " of the draft is underway. Feel free to watch the results!\n";

	   	char stringBuffer[s.length() + 1];
		strcpy(stringBuffer, s.c_str());
		stringBuffer[s.length()] = '\0';

	    responseHeader.dataLength = htonl(s.length() + 1);

        int bytes, sent, total;
	    total = HEADERSIZE; sent = 0;	
	    fprintf(stderr, "handleHello (draftStarted, client not read): Writing to %s with sock %d\n", curClient->ID,curClient->sock);
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
    }

	memset(&responseHeader,0,sizeof(responseHeader));
    strcpy(responseHeader.sourceID, "Server");
    responseHeader.msgID = htonl(0);

    for(int i = 0; i < MAXCLIENTS; i++) {
    	if(clients[i].active && (clients[i].sock != curClient->sock)) {
	    	strcpy(responseHeader.destID, clients[i].ID);
		    responseHeader.type = htons(CHAT);
		    string s = curClient->ID;
		    if(returning) {
		    	s += " has logged back in";
		    } else {
		    	s += " joined for the first time";
		    }
		   	char stringBuffer[s.length() + 1];
			strcpy(stringBuffer, s.c_str());
			stringBuffer[s.length()] = '\0';

	    	responseHeader.dataLength = htonl(s.length() + 1);		    

	        int bytes, sent, total;
	    	total = HEADERSIZE; sent = 0;	
		    fprintf(stderr, "handleHello (CHAT to active clients): Writing to %s with sock %d\n", clients[i].ID,clients[i].sock);
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

		    handleListRequest(&clients[i]);
		}
    }
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
    fprintf(stderr, "handleListRequest: Writing to %s with sock %d\n", curClient->ID,curClient->sock);
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
		if((strcmp(clients[i].ID, sender->destID) == 0) && clients[i].active) {
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
    fprintf(stderr, "handleChat: Writing to %s with sock %d\n", receiver->ID,receiver->sock);
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
	bool logout; bool actualExit = true;
    string s = curClient->ID;
	if(curClient->active && curClient->validated) {
		//fprintf(stderr, "exit: active and validated\n");
		int sock = curClient->sock;
		close(sock);
		FD_CLR(sock, &read_fd_set);
		FD_CLR(sock, &active_fd_set);
		curClient->active = false;
		curClient->sock = -1;
		numActiveClients--;
		logout = true;
	} else {
		//if(!curClient->active) fprintf(stderr, "exit: not active\n");
		//if(!curClient->validated) fprintf(stderr, "exit: not validated\n");
		int sock = curClient->sock;
		close(sock);
		FD_CLR(sock, &read_fd_set);
		FD_CLR(sock, &active_fd_set);
		if(sock == -1) actualExit = false;
	    for(int i = 0; i < MAXCLIENTS; i++) {
	    	if(curClient->sock == 0) break;
			if((strcmp(curClient->ID,clients[i].ID) == 0) && (!(clients[i].active)) || !(clients[i].validated)) {
				if(!curClient->validated && !draftStarted) {
					for(int i = 0; i < playerData.size(); i++) {
						if(strcmp(curClient->ID,playerData[i].owner) == 0) {
							memset(playerData[i].owner,0,IDLENGTH);
							strcpy(playerData[i].owner,"Server");
						}
					}
				}
				if(!curClient->validated && draftStarted) {
					for(int i = 0; i < playerData.size(); i++) {
						if(strcmp(curClient->ID,playerData[i].owner) == 0) {
							memset(playerData[i].owner,0,IDLENGTH);
							strcpy(playerData[i].owner,"QUITTER");
						}
					}
					for(int i = 0; i < theDraft.teams.size(); i++) {
						if(strcmp(curClient->ID,theDraft.teams[i].owner) == 0) {
							memset(theDraft.teams[i].owner,0,IDLENGTH);
							strcpy(theDraft.teams[i].owner,"QUITTER");
						}
					}
				}
			    fprintf(stderr,"permanently removing client %s with sockfd %d\n",curClient->ID,curClient->sock);
			    memset(curClient, 0, sizeof(curClient));
			}
	    }
	    numClients--;
	    logout = false;
	}

	struct header responseHeader;
	memset(&responseHeader,0,sizeof(responseHeader));
    strcpy(responseHeader.sourceID, "Server");
    responseHeader.msgID = htonl(0);
	if(logout) {
	   	s += " has logged out";
	} else {
	   	s += " has quit permanently";
	}
	char stringBuffer[s.length() + 1];
	strcpy(stringBuffer, s.c_str());
	stringBuffer[s.length()] = '\0';

	responseHeader.dataLength = htonl(s.length() + 1);

	maxDelay = 0;
	bool startDraft = true;
	
    for(int i = 0; i < MAXCLIENTS; i++) {
		if((clients[i].timeout > maxDelay) && clients[i].active) maxDelay = clients[i].timeout;
		if(clients[i].active && !clients[i].readyToDraft) startDraft = false;
    	if(clients[i].active && actualExit) {
	    	strcpy(responseHeader.destID, clients[i].ID);
		    responseHeader.type = htons(CHAT);

	        int bytes, sent, total;
	    	total = HEADERSIZE; sent = 0;	
		    fprintf(stderr, "handleExit (CHAT to active clients): Writing to %s with sock %d\n", clients[i].ID,clients[i].sock);
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

		    handleListRequest(&clients[i]);
		}
	}

	if(startDraft && !draftStarted) sendStartDraft();
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
	
    //fprintf (stderr, "CANNOT_DELIVER responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", responseHeader.type, responseHeader.sourceID, responseHeader.destID, responseHeader.dataLength, responseHeader.msgID);

    /* send CANNOT_DELIVER */
    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    fprintf(stderr, "cannotDeliver: Writing to %s with sock %d\n", curClient->ID,curClient->sock);
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
    	fprintf(stderr, "handleError: Writing to %s with sock %d\n", curClient->ID,curClient->sock);
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

    /* send PLAYER_RESPONSE header */
    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    fprintf(stderr, "handlePlayerRequest: Writing to %s with sock %d\n", curClient->ID,curClient->sock);
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

	//fprintf(stderr, "Recieved draft request from %s for %s\n", curClient->ID, curClient->partialData);
	//playerInfo playerToDraft;
	if(draftStarted) {
		if(strcmp(playerData[theDraft.order[theDraft.index]].PLAYER_NAME,curClient->partialData) == 0) {
			for(int i = 0; i < theDraft.teams.size(); i++) {
				if(strcmp(theDraft.teams[i].owner,curClient->ID) == 0) {
					//fprintf(stderr, "Request from team with owner: %s\n", theDraft.teams[i].owner);
					timespec adjustedTimeReceived;
					clock_gettime(CLOCK_MONOTONIC,&theDraft.teams[i].adjustedTimeReceived);
					//fprintf(stderr, "maxDelay - curClient->estRTT: %f\n", maxDelay - curClient->estRTT);
					int handicap = maxDelay - curClient->estRTT;
					//fprintf(stderr, "Adding %d seconds and %d useconds to adjustedTimeReceived\n", handicap / 1000, (handicap % 1000) * 1000000);
					theDraft.teams[i].adjustedTimeReceived.tv_sec += handicap / 1000;
					theDraft.teams[i].adjustedTimeReceived.tv_nsec += ((handicap % 1000) * 1000000);

					//theDraft.teams[i].adjustedTimeReceived = adjustedTimeReceived;
					//fprintf(stderr, "%s's adjustedTimeReceived.tv_sec and tv_nsec: %d, %d\n", theDraft.teams[i].owner,theDraft.teams[i].adjustedTimeReceived.tv_sec, theDraft.teams[i].adjustedTimeReceived.tv_nsec);
					theDraft.teams[i].responseRecieved = true;
					if(roundIsOver()) endDraftRound();
				}
			}
		} else {
			//fprintf(stderr, "Draft request for previous round recieved from %s\n", curClient->ID);
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
				handlePlayerRequest(&clients[i]);		
			}
		}
	}
}

void handleDraftPass(clientInfo *curClient) {
	//fprintf(stderr, "In handleDraftPass\n");
	//fprintf(stderr, "Player to draft: %s, player recieved: %s\n", playerData[theDraft.index].PLAYER_NAME,curClient->partialData);
	if(strcmp(playerData[theDraft.order[theDraft.index]].PLAYER_NAME,curClient->partialData) == 0) {
		for(int i = 0; i < theDraft.teams.size(); i++) {
			if(strcmp(theDraft.teams[i].owner,curClient->ID) == 0) {
				theDraft.teams[i].responseRecieved = true;
				if(roundIsOver()) endDraftRound();
			}
		}
	}
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
    fprintf(stderr, "handleStartDraft: Writing to %s with sock %d\n", curClient->ID,curClient->sock);
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

    if((totalClients == readyClients) && !draftStarted) {
//    	usleep((int)maxDelay * 1000);
    	startDraft = true;
//    	sendStartDraft();
//    	draftStarted = true;
    }
}


void handlePingResponse(clientInfo *curClient) {
	struct timespec sentTime, curTime;
	clock_gettime(CLOCK_MONOTONIC,&curTime);
	memcpy((char *)&sentTime,curClient->partialData,sizeof(sentTime));

	int delayms = ((curTime.tv_sec * 1000) + (curTime.tv_nsec / 1000000)) - ((curClient->lastPingSent.tv_sec * 1000) + (curClient->lastPingSent.tv_nsec / 1000000));

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

		curClient->pings++;
		//fprintf(stderr, "Client %s has estRTT of %f, devRTT of %f, and timeout of %f\n", curClient->ID,curClient->estRTT,curClient->devRTT,curClient->timeout);
		maxDelay = 0;
		for(int i = 0; i < MAXCLIENTS; i++) {
			if((clients[i].timeout > maxDelay) && clients[i].active) maxDelay = clients[i].timeout;
		}
	}

	//if(curClient->pings-- > 0) {
	//	sendPing(curClient);
	//}
}


/********************************************************************
 *			Section 4: Method for sending a ping 					*
 ********************************************************************/
void sendPing(clientInfo *curClient) {
	//struct  timespec startTime;
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
    fprintf(stderr, "sendPing: Writing to %s with sock %d\n", curClient->ID,curClient->sock);
    do {
	    clock_gettime(CLOCK_MONOTONIC,&curClient->lastPingSent);
		bytes = write(curClient->sock, (char *)&responseHeader+sent, total-sent);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);

    //readHeader(curClient, curClient->sock);
    //readData(curClient);
    //memcpy(timeBuffer,&startTime,sizeof(startTime));
}


/********************************************************************
 *			Section 5: Methods for managing the draft				*
 ********************************************************************/
void sendStartDraft() {
	memset(&theDraft,0,sizeof(theDraft));
	startDraft = false;
	//fprintf(stderr, "In sendStartDraft\n");
	struct header responseHeader;
	memset(&responseHeader,0,sizeof(responseHeader));
    responseHeader.type = htons(DRAFT_STARTING);
    strcpy(responseHeader.sourceID, "Server");
    responseHeader.msgID = htonl(ROUNDTIME | PARTICIPATING_MASK);

    string s;
    s = "The draft is now starting with "; s += to_string(numActiveClients); s += " clients!";

   	char stringBuffer[s.length() + 1];
	strcpy(stringBuffer, s.c_str());
	stringBuffer[s.length()] = '\0';

    responseHeader.dataLength = htonl(s.length() + 1);

    for(int i = 0; i < MAXCLIENTS; i++) {
    	//fprintf(stderr, "In for with i=%d\n", i);
    	if(clients[i].active) {
    		//fprintf(stderr, "In for with active client: %s\n", clients[i].ID);
		    strcpy(responseHeader.destID, clients[i].ID);
    		
		    int bytes, sent, total;
		    total = HEADERSIZE; sent = 0;
		    fprintf(stderr, "sendStartDraft: Writing to %s with sock %d\n", clients[i].ID, clients[i].sock);
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

    for(int i = 0; i < playerData.size(); i++) {
    	memset(playerData[i].owner,0,IDLENGTH);
    	strcpy(playerData[i].owner,"Server");
    	theDraft.order.push_back(i);
    }

    random_shuffle(theDraft.order.begin(), theDraft.order.end());
    draftNum++;
   	//draftNewRound();
   	usleep(maxDelay * 1000);
   	draftStarted = true;
   	startNewRound = true;
}

void draftNewRound() {
	startNewRound = false;
    theDraft.currentRound++;
    theDraft.index = theDraft.currentRound - 1;
    theDraft.index = theDraft.index % playerData.size();

	char curPlayer[50];
	memset(curPlayer,0,50);
	memcpy(curPlayer,playerData[theDraft.order[theDraft.index]].PLAYER_NAME,50);

	//fprintf(stderr, "curPlayer in draftNewRound: %s\n", curPlayer);
	struct header responseHeader;
	memset(&responseHeader,0,sizeof(responseHeader));
    responseHeader.type = htons(DRAFT_ROUND_START);
    strcpy(responseHeader.sourceID, "Server");
    responseHeader.dataLength = htonl(strlen(curPlayer) + 1);
    responseHeader.msgID = htonl(theDraft.currentRound);	

    timespec writeTime;
    for(int i = 0; i < MAXCLIENTS; i++) {
    	if(clients[i].active) {
		    strcpy(responseHeader.destID, clients[i].ID);
		    clock_gettime(CLOCK_MONOTONIC, &writeTime);
		    //fprintf(stderr, "writeTime for %s: sec: %d, nsec: %d\n", clients[i].ID, writeTime.tv_sec, writeTime.tv_nsec);
			int bytes, sent, total;
		    total = HEADERSIZE; sent = 0;
		    fprintf(stderr, "draftNewRound: Writing to %s with sock %d\n", clients[i].ID, clients[i].sock);
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
	   		theDraft.teams[i].adjustedTimeReceived.tv_nsec = 0;
	   		theDraft.teams[i].responseRecieved = false;
    }

    //timespec fiveSecs; fiveSecs.tv_sec = 5; fiveSecs.tv_nsec = 0;
    timespec roundTotalTime; roundTotalTime.tv_sec = (maxDelay / 1000) + ROUNDTIME; roundTotalTime.tv_nsec = (maxDelay % 1000) * 1000000;
    //fprintf(stderr, "roundTotalTime.tv_sec: %d, tv_nsec: %d\n", roundTotalTime.tv_sec, roundTotalTime.tv_nsec);
    timespec curTime;
    clock_gettime(CLOCK_MONOTONIC,&curTime);
    //fprintf(stderr, "curTime.tv_sec: %d, tv_nsec: %d\n", curTime.tv_sec, curTime.tv_nsec);
    timespec roundEndTime;
    timespecAdd(&roundTotalTime,&curTime,&roundEndTime);
    //fprintf(stderr, "roundEndTime.tv_sec: %d, tv_nsec: %d\n", roundEndTime.tv_sec, roundEndTime.tv_nsec);
    //roundEndTime.tv_sec = roundTotalTime.tv_sec + curTime.tv_sec + ((roundTotalTime.tv_nsec + curTime.tv_nsec) / 1000000000);
    //roundEndTime.tv_nsec = (roundTotalTime.tv_nsec + curTime.tv_nsec) % 1000000000;
    theDraft.roundEndTime = roundEndTime;
}

void endDraftRound() {
	//fprintf(stderr, "In endDraftRound\n");
	char winner[IDLENGTH];
	memset(winner,0,IDLENGTH);

	timespec quickest;
	quickest.tv_sec = theDraft.roundEndTime.tv_sec;
	quickest.tv_nsec = theDraft.roundEndTime.tv_nsec;
	//fprintf(stderr, "quickest has tv_sec: %d and tv_nsec %d\n", quickest.tv_sec, quickest.tv_nsec);

	for(int i = 0; i < theDraft.teams.size(); i++) {
		//fprintf(stderr, "Team %s had adjustedTimeReceived.tv_sec: %d and tv_nsec: %d\n", theDraft.teams[i].owner, theDraft.teams[i].adjustedTimeReceived.tv_sec, theDraft.teams[i].adjustedTimeReceived.tv_nsec);
		if(theDraft.teams[i].adjustedTimeReceived.tv_sec != 0) {
			if(timespecLessthan(&theDraft.teams[i].adjustedTimeReceived,&quickest)) {
				if(theDraft.teams[i].playersDrafted < TEAMSIZE) {
					//fprintf(stderr, "New fastest is: %s\n", theDraft.teams[i].owner);
					quickest = theDraft.teams[i].adjustedTimeReceived;
					strcpy(winner,theDraft.teams[i].owner);
				}
			} 
		}
	}

	if(strcmp(winner, "") != 0) {
		strcpy(playerData[theDraft.order[theDraft.index]].owner,winner);
		for(int i = 0; i < theDraft.teams.size(); i++) {
			if(strcmp(theDraft.teams[i].owner,winner) == 0) {
				theDraft.teams[i].players[theDraft.teams[i].playersDrafted] = playerData[theDraft.order[theDraft.index]];
				theDraft.teams[i].playersDrafted++;
				fprintf(stderr, "%s won %s in round %d of the draft\n", winner, playerData[theDraft.order[theDraft.index]].PLAYER_NAME, theDraft.currentRound);
				//fprintf(stderr, "The owner of %s is now %s\n",playerData[theDraft.index].PLAYER_NAME,playerData[theDraft.index].owner);
				//fprintf(stderr, "Team %s: \n", winner);
				//for(int j = 0; j < theDraft.teams[i].playersDrafted; j++) {
				//	fprintf(stderr, "Player %d: %s\n", j+1, theDraft.teams[i].players[j].PLAYER_NAME);
				//}
			}
		}
	} else {
		fprintf(stderr, "No one claimed %s in round %d of the draft\n", playerData[theDraft.order[theDraft.index]].PLAYER_NAME, theDraft.currentRound);
	}

    struct header responseHeader;
    responseHeader.type = htons(DRAFT_ROUND_RESULT);
    strcpy(responseHeader.sourceID, "Server");
    responseHeader.dataLength = htonl(IDLENGTH);
    responseHeader.msgID = htonl(theDraft.index);

	for(int i = 0; i < MAXCLIENTS; i++) {
		if(clients[i].active) {
		    memcpy(responseHeader.destID, clients[i].ID, IDLENGTH);
		    int bytes, sent, total;
		    total = HEADERSIZE; sent = 0;
		    fprintf(stderr, "endDraftRound: Writing to %s with sock %d\n", clients[i].ID, clients[i].sock);
		    do {
				bytes = write(clients[i].sock, (char *)&responseHeader+sent, total-sent);
				if(bytes < 0) error("ERROR writing to socket");
				if(bytes == 0) break;
				sent+=bytes;
		    } while (sent < total);
	
			total = IDLENGTH;
		    sent = 0;
		    while(sent < total) {
		        bytes = write(clients[i].sock, winner+sent, total-sent);
		        if(bytes < 0) error("ERROR writing to socket");
		        if(bytes == 0) break;
		        sent+= bytes;
		    }			
		}
	}
	//fprintf(stderr, "End of endDraftRound\n");
	bool readyToEnd = true;
	for(int i = 0; i < theDraft.teams.size(); i++) {
		//fprintf(stderr, "Check end loop i: %d\n", i);
		if(theDraft.teams[i].playersDrafted != TEAMSIZE) {
			//fprintf(stderr, "Team %s is not full\n", theDraft.teams[i].owner);
			bool foundAndActive = false;
			for(int j = 0; j < MAXCLIENTS; j++) {
				if(strcmp(clients[j].ID,theDraft.teams[i].owner) == 0) {
					if(clients[j].active) {
						//fprintf(stderr, "%s is found and active\n", clients[j].ID);
						foundAndActive = true;
					}
				}
			}
			if(foundAndActive) {
				readyToEnd = false;
				break;
			}
		}
	}

	if(readyToEnd) {
		fprintf(stderr, "Going to end the draft now\n");
		endDraft();
	} else {
		usleep(maxDelay * 1000);
		startNewRound = true;
//		draftNewRound();
	}
}

void endDraft() {
	draftStarted = false;

    struct header responseHeader;
    responseHeader.type = htons(DRAFT_END);
    strcpy(responseHeader.sourceID, "Server");
    responseHeader.dataLength = htonl(0);
    responseHeader.msgID = htonl(draftNum);

	for(int i = 0; i < MAXCLIENTS; i++) {
		clients[i].readyToDraft = false;
		if(clients[i].active) {

		    memcpy(responseHeader.destID, clients[i].ID, IDLENGTH);

		    int bytes, sent, total;
		    total = HEADERSIZE; sent = 0;
		    fprintf(stderr, "endDraft: Writing to %s with sock %d\n", clients[i].ID, clients[i].sock);
		    do {
				bytes = write(clients[i].sock, (char *)&responseHeader+sent, total-sent);
				if(bytes < 0) error("ERROR writing to socket");
				if(bytes == 0) break;
				sent+=bytes;
		    } while (sent < total);			
		}
	}

	for(int i = 0; i < playerData.size(); i++) {
		memset(playerData[i].owner,0,IDLENGTH);
		strcpy(playerData[i].owner,"Server");
	}
}


/********************************************************************
 *		Section 6: Helper for deciding if draft round is over		*
 ********************************************************************/
bool roundIsOver() {
	//fprintf(stderr, "In roundIsOver\n");
	bool allResponsesRecieved = true;
	for(int i = 0; i < theDraft.teams.size(); i++) {
		if(!theDraft.teams[i].responseRecieved) 
			for(int j = 0; j < MAXCLIENTS; j++) {
				if(strcmp(clients[j].ID,theDraft.teams[i].owner) == 0) {
					if(clients[j].active) allResponsesRecieved = false;
				}
			}
	}
	return allResponsesRecieved;
}


/********************************************************************
 *		Section 7: Helpers for timespec comparison and arithmetic	*
 ********************************************************************/
void timespecAdd(timespec *a, timespec *b, timespec *c) {
	time_t secs = a->tv_sec + b->tv_sec + ((a->tv_nsec + b->tv_nsec) / 1000000000);
	long nsecs = (a->tv_nsec + b->tv_nsec) % 1000000000;

	c->tv_sec = secs;
	c->tv_nsec = nsecs;
}

// Assumes a > b
void timespecSubtract(timespec *a, timespec *b, timespec *c) {
	if(!timespecLessthan(b,a)) return;

	if (a->tv_nsec < b->tv_nsec) {
		c->tv_nsec = a->tv_nsec + 1000000000;
		c->tv_sec = a-> tv_sec - 1;
	} else {
		c->tv_nsec = a->tv_nsec;
		c->tv_sec = a->tv_sec;
	}

	c->tv_nsec = c->tv_nsec - b->tv_nsec;
	c->tv_sec = c->tv_sec - b->tv_sec;
}

bool timespecLessthan(timespec *a, timespec *b) {
	// Is a less than/earlier than b?
	if(a->tv_sec < b->tv_sec) return true;
	if(a->tv_sec > b->tv_sec) return false;
	if(a->tv_nsec < b->tv_nsec) return true;
	return false;
}

/********************************************************************
 *		 Section 8: Helper for encrypting user passwords			*
 ********************************************************************/
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