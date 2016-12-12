// Bernie Birnbaum and Shawyoun Saidon
// Comp 112 Final Project
// Tufts University

// This whole section with includes, defines, and struct definitions would
// ideally be in a seperate header file
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
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "demo_ssl_utils.h"

using namespace std;

// Single points of reference for hard-coded parameters
#define MAXDATASIZE 400 // Only limits messages sent between users
#define MAXCLIENTS 200 // Draft with this many would run out of players however
#define IDLENGTH 20 // Includes null character
#define TEAMSIZE 12
#define ROUNDTIME 10 // Max of 255
#define PARTICIPATING_MASK 256
const float minTimeout = 50.0;

// Message types (in order in which they were incorperated into the code)
// Comments indicat use of msgID field, if any
#define HELLO 1
#define HELLO_ACK 2 // 1 if draft in progress, 0 otherwise
#define LIST_REQUEST 3
#define CLIENT_LIST 4
#define CHAT 5 // # chat sent by client
#define EXIT 6
#define ERROR 7
#define CANNOT_DELIVER 8
#define PLAYER_REQUEST 9
#define PLAYER_RESPONSE 10
#define DRAFT_REQUEST 11
#define PING 12 // # ping sent to specific client by server
#define PING_RESPONSE 13 // # of pint client is responding to
#define START_DRAFT 14
#define DRAFT_STATUS 15
#define DRAFT_STARTING 16 // Roundlength & participation mask
#define DRAFT_ROUND_START 17  // # of round that is starting
#define DRAFT_ROUND_RESULT 18 // # of round that is ending
#define DRAFT_PASS 19
#define DRAFT_END 20 // # of draft that just finished
#define BAD_EXIT 25 /* Shawyoun */

// Header struct, agreed upon by client and server
struct header {
    unsigned short type; // One of the options from above;
    char sourceID[IDLENGTH]; 
    char destID[IDLENGTH];
    unsigned int dataLength;
    unsigned int msgID; // Used to store info for some message types
}__attribute__((packed, aligned(1)));

#define HEADERSIZE sizeof(header)

// struct for storing info about clients
struct clientInfo {
    int sock;
    char ID[IDLENGTH];
    bool active;
    int mode;

    // In case a header isn't read all at once
    int headerToRead;
    char partialHeader[HEADERSIZE];

    // Storage for data read from client
    int dataToRead;
    int totalDataExpected;
    char partialData[MAXDATASIZE];
    char destID[IDLENGTH];
    int msgID;

    // Storage for client's hashed password
    int pwordToRead;
    int totalPwordExpected;
    char pword[65];
    bool validated;

    // Info about this client's pings
    int pingSent;
    int pingRcvd;
    int pings;
    struct timespec lastPingSent;
    float estRTT;
    float devRTT;
    float timeout;

    // Is this client ready for the draft to start?
    bool readyToDraft;

	SSL * ssl; /* Shawyoun forked */
}__attribute__((packed, aligned(1)));

// Struct for each team participating in draft
struct team
{
	char owner[20];
	int playersDrafted;
	bool responseRecieved;
	timespec adjustedTimeReceived;
	playerInfo players[TEAMSIZE];
};

// Info about the draft
struct draftInfo {
	int index;
	int currentRound;
	timespec roundEndTime;
	vector<team> teams;
	vector<int> order;
};

// Stroring, tracking clients
struct clientInfo clients[MAXCLIENTS];
int clientCounter = 0;
int numClients = 0; int numActiveClients = 0;

// maximum time server should wait for any client to respond
float maxDelay;

// conditions for sending pings
bool timedOut = false;
bool curRoundPingsSent = false;
bool newEntry = false;

// Tracking the status of the draft (admittedly could have been part of draftInfo struct)
int draftNum = 0;
bool startDraft = false;
bool draftStarted;
bool startNewRound = false;
struct draftInfo theDraft;

// Info on all players
vector<playerInfo> playerData;

// For estRTT, devRTT, timeout calculations
const float alpha = 0.875;
const float beta = 0.25;

// Boilerplate functions
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

/* Methods are broken down into sections */
// 1. All purpose method for new read from a client
void readFromClient(int sockfd);
void readFromClientForProxy(); /* Shawyoun */

// 2. Methods for reading specifc parts of messages
void readHeader(struct clientInfo *curClient, int sockfd);
void readHeaderForProxy(struct clientInfo *curClient); /* Shawyoun */
void readData(struct clientInfo *curClient);
void readDataForProxy(struct clientInfo *curClient); /* Shawyoun */
void readPword(struct clientInfo *curClient);
void readPwordForProxy(struct clientInfo *curClient); /* Shawyoun */

// 3. Methods for handling particular types of messages
void handleHello(struct clientInfo *curClient);
void handleHelloForProxy(struct clientInfo *curClient); /* Shawyoun */
void handleListRequest(struct clientInfo *curClient);
void handleChat(struct clientInfo *sender);
void handleExit(struct clientInfo *curClient); /* Shawyoun forked */
void handleExitForClient(struct clientInfo * curClient);
void handleExitForProxy(struct clientInfo * curClient);
void handleClientPresent(struct clientInfo *curClient, char *ID);
void handleCannotDeliver(struct clientInfo *curClient);
void handleError(struct clientInfo *curClient); /* Shawyoun forked */
void handleErrorForClient(struct clientInfo *curClient);
void handleErrorForProxy(struct clientInfo *curClient);
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

char currentClientID[IDLENGTH]; /* Shawyoun */
int currentHeaderToRead; /* Shawyoun */
int proxyFrontEndSock; /* Shawyoun */ 
int proxyMode = 0; /* Shawyoun */
int secureMode = 0; /* Shawyoun */
SSL_CTX * ctx; /* Shawyoun forked */

int custom_read(int fd, void * buffer, int nbytes, SSL* secure_fd); /* Shawyoun fork */
int custom_write(int fd, void * buffer, int nbytes, SSL* secure_fd); /* Shawyoun fork */

/********************************************************************
 *							Section 0: main							*
 ********************************************************************/
int main(int argc, char *argv[]) {
	srand(unsigned (time(0)));
	memset(clients,0,sizeof(clients));
	// TODO: Give more options than this hard coded file
	playerData = readCSV("nba1516.csv");

    memset(&clients, 0, sizeof(clients));
    int sockfd, newsockfd, portno, pid, i;
    
    socklen_t clilen;

    struct sockaddr_in serv_addr, cli_addr;

	// TODO: Flags for particular error message types
    if (argc < 2) {
		fprintf(stderr,"ERROR, no port provided\n");
		exit(1);
    } else if(argc > 2){ /* Shawyoun forked */
		if(strcmp(argv[2], "-p") == 0){
			proxyMode = 1;
			fprintf(stderr, "Starting as a server behind a proxy. Awaiting connection from proxy frontend.\n");
		}
		else if(strcmp(argv[2], "-s") == 0){
			secureMode = 1;
			fprintf(stderr, "Starting as a secure server\n");
			ctx = newSSLContext(4);
			loadPrivateAndPublicKeys(ctx, "newreq.pem", "newreq.pem");
		}
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
   		timespec curTime;
   		clock_gettime(CLOCK_MONOTONIC,&curTime);

   		// Determine timeout for select
		timespec timeToEndRound;
    	if(draftStarted && !startNewRound) {
    		if(timespecLessthan(&theDraft.roundEndTime,&curTime)) {
    			// Time has elapsed!
    			fprintf(stderr, "About to endDraftRound\n");
    			endDraftRound();
    			continue; // Probably unecessary 
    		} else {
				timespecSubtract(&theDraft.roundEndTime,&curTime,&timeToEndRound);
				fprintf(stderr, "Time til next round: %d.%ds\n", timeToEndRound.tv_sec,timeToEndRound.tv_nsec);
			}
    	} else {
    		// Next round will never be if the draft is not going on!
    		timeToEndRound.tv_sec = 99999;
    		timeToEndRound.tv_nsec = 0;
    	}

		timedOut = true;

		// If the draft is going on, want to end as close to end of round as possible
		// Otherwise wait a healthy amount more than necessary to accomodate maxDelay
		int timeoutSecs = min(max(((int)(maxDelay + 1500) / 1000), 5), (timeToEndRound.tv_sec + 1) );
		//fprintf(stderr, "timeoutSecs: %d\n", timeoutSecs);

		struct timeval selectTimeout = {timeoutSecs,(timeToEndRound.tv_nsec / 1000)};

		// Cases where we want to send pings sooner rather than later
		if(startNewRound && !curRoundPingsSent) selectTimeout = {0,1000}; // Just making sure nobody quit
		if(newEntry && !draftStarted) selectTimeout = {1,0}; // Idea is to get a ping in quickly; reality is if RTT is greater it causes issues

		read_fd_set = active_fd_set;

		/* Block until input arrives on one or more active sockets */
		if(select(FD_SETSIZE, &read_fd_set, NULL, NULL, &selectTimeout) < 0) {
		    error("ERROR on select");
		}

		/* Service all the sockets with input pending */
		for(i = 0; i < FD_SETSIZE; i++) { //FD_SETSIZE == 1024
	    	if(FD_ISSET(i, &read_fd_set)) {
	    		timedOut = false;
			int refusedCxn = 0; /* Shawyoun forked */
				if(i == sockfd) {
			    	/* Connection request on original socket */	
				    	newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
					
			    	if(newsockfd < 0)
						error("ERROR on accept");

				if(proxyMode){ /* Shawyoun forked */
					if(proxyFrontEndSock){ /* "Reject" cxn if we've already made one */
						close(newsockfd);
						refusedCxn = 1;
					}
					else{
						proxyFrontEndSock = newsockfd;
						fprintf(stdout, "Connected to proxy!\n");
					}
				} else {

		    		/* make and insert new clientInfo record */
		    		struct clientInfo newClient;
			    	memset(&newClient, 0, sizeof(newClient));
			    	newClient.sock = newsockfd;
			    	newClient.headerToRead = HEADERSIZE;
		    		newClient.active = true;
			    	newClient.validated = false;
			    	newClient.timeout = 5000;
			    	newClient.readyToDraft = false;
				if(secureMode){
					newClient.ssl = SSL_new(ctx);
					SSL_set_fd(newClient.ssl, newClient.sock);
					if(SSL_accept(newClient.ssl) != 1)
						ERR_print_errors_fp(stderr);
					else
						fprintf(stderr, "Handshake successful!\n");
				}
				    bool inserted = false;
				    while(!inserted) {
						if(clients[clientCounter].sock == NULL) {
					    	clients[clientCounter] = newClient;
					    	inserted = true;
						}
					 	clientCounter++;
					 	numClients++;
					 	numActiveClients++;
						clientCounter = clientCounter % MAXCLIENTS;
				    }
				}
				if(!refusedCxn){ /* Shawyoun forked */
			    		fprintf(stderr, "New connection with newsockfd: %d\n", newsockfd);
					startDraft = false; // Don't want to ambush new client with start of a draft
			    		// fprintf(stderr, "Server: connect from host %s, port %hu. \n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

				    	FD_SET(newsockfd, &active_fd_set);
				}
		 		} else {
			    /* Data arriving on an already-connected socket */
				if(proxyMode) /* Shawyoun forked */
			    		readFromClientForProxy();
				else
					readFromClient(i);
				}
	    	} 
		}

		// If it's been quiet for long enough or we need to get the pings in now, lets ping!
		if((timedOut && !draftStarted) || (startNewRound && !curRoundPingsSent)) {
			for(int i = 0; i < MAXCLIENTS; i++) {
				if(clients[i].active && clients[i].validated) {
					// Don't ping if we're still waiting on a response...unless it's really hopeless
					if((clients[i].pingSent == clients[i].pingRcvd) || ((curTime.tv_sec - clients[i].lastPingSent.tv_sec) > 30)) {
						sendPing(&clients[i]);
					}
				}
			}
			if(startNewRound) {
				curRoundPingsSent = true;
				timedOut = false;
			}
			// Want to keep up the short timeouts until we get the newbie at least one ping
			if(newEntry) newEntry = false;
		}

		// Making sure there's a chance for client input to come in before starting anything
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

    /* get client address based on sockfd */
    for(i = 0; i < MAXCLIENTS; i++) {
		if(clients[i].sock == sockfd) {
			if(clients[i].active) {
			    curClient = &clients[i];
			    break;
			} else {
				// This is a new user
				struct clientInfo newClient; /* TODO Shawyoun: might need more forking here */
				memset(&newClient, 0, sizeof(newClient));
				newClient.sock = sockfd;
			    newClient.headerToRead = HEADERSIZE;
		     	newClient.active = true;
		     	newClient.validated = false;
			if(secureMode){
					newClient.ssl = SSL_new(ctx);
					SSL_set_fd(newClient.ssl, newClient.sock);
					if(SSL_accept(newClient.ssl) != 1)
						ERR_print_errors_fp(stderr);
					else
						fprintf(stderr, "Handshake successful!\n");
				}
				bool inserted = false;
		     	while(!inserted) {
					if(clients[clientCounter].sock == 0) {
				  		clients[clientCounter] = newClient;
				    	inserted = true;
				 	}
					clientCounter++;
					numActiveClients++;
					clientCounter = clientCounter % MAXCLIENTS;
		     	}
		     	break;
			}
		}
    }

    /* send to appropriate read method; headerToRead should always be positive */
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
    nbytes = custom_read (curClient->sock, &header_buffer[HEADERSIZE-curClient->headerToRead],curClient->headerToRead, curClient->ssl);

    //fprintf(stderr, "readHeader client: %s, nbytes: %d, expected readsize: %d\n",curClient->ID, nbytes,curClient->headerToRead); 

    if (nbytes <= 0) {
		/* Read error or EOF: Socket closed */
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

		/* identify potential bad input; holdover from Assignment 2 */
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

			// Make sure readPword is triggered
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
		    /* Client formally exiting, make sure handleExit removes them completely */
			curClient->active = false;
			numActiveClients--;
			curClient->validated = false;
		    handleExit(curClient);

		} else if(newHeader.type == PLAYER_REQUEST) {
			handlePlayerRequest(curClient);

		} else if(newHeader.type == DRAFT_REQUEST) {
			// Need to read in player that is being drafted
			memset(curClient->partialData, 0, MAXDATASIZE);
			curClient->mode = DRAFT_REQUEST;
			curClient->dataToRead = newHeader.dataLength;
			curClient->totalDataExpected = newHeader.dataLength;

		}else if(newHeader.type == PING_RESPONSE) {
			// Client is sending a timestamp of its own, though the server doesn't use it
			memset(curClient->partialData,0,MAXDATASIZE);
			curClient->mode = PING_RESPONSE;
			curClient->msgID = newHeader.msgID;
			curClient->dataToRead = newHeader.dataLength;
			curClient->totalDataExpected = newHeader.dataLength;

		} else if(newHeader.type == START_DRAFT) {
			// Sorry about the confusing name; client is toggling their readiness
			handleStartDraft(curClient);

		} else if(newHeader.type == DRAFT_PASS) {
			// Need to read in player being passed on
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
    nbytes = custom_read (curClient->sock, &data_buffer[curClient->totalDataExpected-curClient->dataToRead],curClient->dataToRead, curClient->ssl);

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

		// Send to the appropraite handler
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
	nbytes = custom_read (curClient->sock, &data_buffer[curClient->totalPwordExpected - curClient->pwordToRead], curClient->pwordToRead, curClient->ssl);

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

		// Ready to deal with new client now that we have their password
		handleHello(curClient);
	}
}


/********************************************************************
 *			Section 3: Handlers for specific message types			*
 ********************************************************************/
void handleHello(struct clientInfo *curClient) {

    bool returning = false;

	if(curClient->validated == false) {
		returning = true; // New clients are automatically validated before getting here
		
		for(int i = 0; i < MAXCLIENTS; i++) {
			// There are two clients with the same ID, let's get them both
			if((strcmp(clients[i].ID, curClient->ID) == 0) && !(clients[i].sock == curClient->sock)) {
				if(strcmp(clients[i].pword, curClient->pword) == 0) {
					//fprintf(stderr, "Password matches password of existing client, removing placeholder\n");
					curClient->validated = true;
					curClient->readyToDraft = clients[i].readyToDraft; // If the client was previously ready to draft, they (hopefully) still are
					handleError(&clients[i]); // Remove the old client without removing all trace of the ID which the new client also has
				} else {
					//fprintf(stderr, "Password does not match existing client! You're fired!\n");
					curClient->active = false;
					numActiveClients--;
					//fprintf(stderr, "735: numActiveClients: %d\n", numActiveClients);
					handleError(curClient);
					return;
				}
			}
		}
	}

	char helloMessage[50];
	memset(helloMessage,0,50);

	// I later learned easier ways to to this but hey
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
	
    //fprintf (stderr, "HELLO_ACK responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", ntohs(responseHeader.type), responseHeader.sourceID, responseHeader.destID, ntohl(responseHeader.dataLength), ntohl(responseHeader.msgID));

    /* send HELLO_ACK */
    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    fprintf(stderr, "handleHello: Writing to %s with sock %d\n", curClient->ID,curClient->sock);
    do {
		bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);
	
	total = strlen(helloMessage) + 1;
    sent = 0;
    while(sent < total) {
        bytes = custom_write(curClient->sock, helloMessage+sent, total-sent, curClient->ssl);
        if(bytes < 0) error("ERROR writing to socket");
        if(bytes == 0) break;
        sent+= bytes;
        //fprintf(stdout,"Sent %d bytes of the helloMessage\n");
    }

    handleListRequest(curClient);

    // curClient->timeout should be the default
    if(maxDelay < curClient->timeout) maxDelay = curClient->timeout;

	// If the client had previously participated in the draft
	// Note: All clients are set to not ready at the end of the draft so a 
	// client will only return to a draft they were previously involved in
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
			bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
			if(bytes < 0) error("ERROR writing to socket");
			if(bytes == 0) break;
			sent+=bytes;
	    } while (sent < total);

	    total = strlen(stringBuffer) + 1;	
		sent = 0;
		while(sent < total) {
	        bytes = custom_write(curClient->sock, stringBuffer+sent, total-sent, curClient->ssl);
	        if(bytes < 0) error("ERROR writing to socket");
	        if(bytes == 0) break;
	        sent+= bytes;
	    }
    }

    // Even if the client isn't participating, they need to get the information about the ongoing draft
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
	    fprintf(stderr, "handleHello (draftStarted, client not ready): Writing to %s with sock %d\n", curClient->ID,curClient->sock);
	    do {
			bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
			if(bytes < 0) error("ERROR writing to socket");
			if(bytes == 0) break;
			sent+=bytes;
	    } while (sent < total);

	    total = strlen(stringBuffer) + 1;	
		sent = 0;
		while(sent < total) {
	        bytes = custom_write(curClient->sock, stringBuffer+sent, total-sent, curClient->ssl);
	        if(bytes < 0) error("ERROR writing to socket");
	        if(bytes == 0) break;
	        sent+= bytes;
	    }    	
    }

    // Tell everyone else a client has logged in
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
				bytes = custom_write(clients[i].sock, (char *)&responseHeader+sent, total-sent, clients[i].ssl);
				if(bytes < 0) error("ERROR writing to socket");
				if(bytes == 0) break;
				sent+=bytes;
		    } while (sent < total);

		    total = strlen(stringBuffer) + 1;	
			sent = 0;
			while(sent < total) {
	        	bytes = custom_write(clients[i].sock, stringBuffer+sent, total-sent, clients[i].ssl);
		        if(bytes < 0) error("ERROR writing to socket");
		        if(bytes == 0) break;
	    	    sent+= bytes;
		    }

		    handleListRequest(&clients[i]);
		}
    }

    // Shorten the select timeout to get the new client a ping ASAP
    newEntry = true;
}

// Relic of Assignment 2 without character limit
void handleListRequest(struct clientInfo *curClient) {
	fprintf(stderr, "Reached beginning of handleListRequest\n");
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
			strcpy(&IDBuffer[bufferIndex], clients[i].ID);
			bufferIndex = bufferIndex + IDLength;

			if(!(clients[i].active)) {
				IDBuffer[bufferIndex-1] = '*';
				IDBuffer[bufferIndex] = ' ';
				bufferIndex++;
			}
		}
    }

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
		bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);

    /* send IDBuffer */
    total = bufferIndex; sent = 0;
    do {
	bytes = custom_write(curClient->sock, (char *)&IDBuffer+sent, total-sent, curClient->ssl);
	if(bytes < 0) error("ERROR writing to socket");
	if(bytes == 0) break;
	sent+=bytes;
    } while (sent < total);
}

// Relic from Assignment 2
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
		bytes = custom_write(receiver->sock, (char *)&responseHeader+sent, total-sent, receiver->ssl);
		if(bytes < 0) error("ERROR writing to socket");
		//fprintf(stderr,"in handleChat chat header bytes: %d\n", bytes);
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);

    /* send partialData aka the message */
    total = sender->totalDataExpected; sent = 0;
    do {
		bytes = custom_write(receiver->sock, (char *)&sender->partialData[0]+sent, total-sent, receiver->ssl);
		//custom_write(1, (char *)&sender->partialData[0]+sent, bytes);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);
	memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
}

void handleExitForClient(struct clientInfo *curClient) {
	bool logout; bool actualExit = true;
    string s = curClient->ID;

	if(curClient->active && curClient->validated) {
		// Client is simply logging out
		int sock = curClient->sock;
		close(sock);
		FD_CLR(sock, &read_fd_set);
		FD_CLR(sock, &active_fd_set);
		curClient->active = false;
		curClient->sock = -1;
		numActiveClients--;
		logout = true;
	} else {
		// Client either quit permanently or is being booted
		int sock = curClient->sock;
		close(sock);
		FD_CLR(sock, &read_fd_set);
		FD_CLR(sock, &active_fd_set);
		if(sock == -1 || !curClient->validated) actualExit = false; // This is a case of duplicate being deleted when the user logs back in
	    for(int i = 0; i < MAXCLIENTS; i++) {
	    	//if(curClient->sock == 0) break; // Don't remember what this is about, but it's working so why mess?
			if((strcmp(curClient->ID,clients[i].ID) == 0) && (!(clients[i].active)) || !(clients[i].validated)) {
				// Client quit permanently
				if(actualExit && !curClient->validated && !draftStarted) {
					// Release client's players to everyone else
					for(int i = 0; i < playerData.size(); i++) {
						if(strcmp(curClient->ID,playerData[i].owner) == 0) {
							memset(playerData[i].owner,0,IDLENGTH);
							strcpy(playerData[i].owner,"Server");
						}
					}
				}
				if(actualExit && !curClient->validated && draftStarted) {
					// Remove client's players from the player pool
					for(int i = 0; i < playerData.size(); i++) {
						if(strcmp(curClient->ID,playerData[i].owner) == 0) {
							memset(playerData[i].owner,0,IDLENGTH);
							strcpy(playerData[i].owner,"QUITTER");
						}
					}
					// Also take their team away
					for(int i = 0; i < theDraft.teams.size(); i++) {
						if(strcmp(curClient->ID,theDraft.teams[i].owner) == 0) {
							memset(theDraft.teams[i].owner,0,IDLENGTH);
							strcpy(theDraft.teams[i].owner,"QUITTER");
						}
					}
				}
			    fprintf(stderr,"permanently removing client %s with sockfd %d\n",curClient->ID,curClient->sock);
			    memset(curClient, 0, sizeof(clientInfo));
			    numClients--;
			    logout = false;
			    break;
			}
	    }
	}

	// Give everyone else the bad news, also see if the draft is ready now that a client has left
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
	startDraft = true; bool anyActive = false;

    for(int i = 0; i < MAXCLIENTS; i++) {
    	if(clients[i].active) anyActive = true; // Only want to start draft if there is someone there to participate!
		if((clients[i].timeout > maxDelay) && clients[i].active) {
			maxDelay = clients[i].timeout;
		}
		if(clients[i].active && !clients[i].readyToDraft) startDraft = false; // If anyone is still not ready, let's not draft
    	if(clients[i].active && actualExit) { // Only send a message if someone actually left
	    	strcpy(responseHeader.destID, clients[i].ID);
		    responseHeader.type = htons(CHAT);

	        int bytes, sent, total;
	    	total = HEADERSIZE; sent = 0;	
		    fprintf(stderr, "handleExit (CHAT to active clients): Writing to %s with sock %d\n", clients[i].ID,clients[i].sock);
		    do {
				bytes = custom_write(clients[i].sock, (char *)&responseHeader+sent, total-sent, clients[i].ssl);
				if(bytes < 0) error("ERROR writing to socket");
				if(bytes == 0) break;
				sent+=bytes;
		    } while (sent < total);

		    total = strlen(stringBuffer) + 1;	
			sent = 0;
			while(sent < total) {
	        	bytes = custom_write(clients[i].sock, stringBuffer+sent, total-sent, clients[i].ssl);
		        if(bytes < 0) error("ERROR writing to socket");
		        if(bytes == 0) break;
	    	    sent+= bytes;
		    }

		    handleListRequest(&clients[i]);
		}
	}
	if(draftStarted) startDraft = false; // No need to start if it's already underway
	startDraft = startDraft && anyActive; // If everything checks out, draft will start at next timeout
}


// Unecessary holdover from Assignment 2
void handleClientPresent(struct clientInfo *curClient, char *ID) {
	curClient->active = false;
	numActiveClients--;
	//fprintf(stderr, "1151: numActiveClients: %d\n", numActiveClients);
    handleError(curClient);
}

// Relic of Assignment 2
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
    do {
	bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
	if(bytes < 0) error("ERROR writing to socket");
	if(bytes == 0) break;
	sent+=bytes;
    } while (sent < total);

	memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
}

// Relic from Assignment 2
void handleErrorForClient(struct clientInfo *curClient) {
	if(curClient->sock != -1) {
	    struct header responseHeader;
    	responseHeader.type = htons(ERROR);
	    strcpy(responseHeader.sourceID, "Server");
    	memcpy(responseHeader.destID, curClient->ID, IDLENGTH);
	    responseHeader.dataLength = htonl(0);
    	responseHeader.msgID = htonl(curClient->msgID);
	
	    // fprintf (stderr, "ERROR responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", responseHeader.type, responseHeader.sourceID, responseHeader.destID, responseHeader.dataLength, responseHeader.msgID);

    	/* send ERROR */
	    int bytes, sent, total;
    	total = HEADERSIZE; sent = 0;
    	fprintf(stderr, "handleError: Writing to %s with sock %d\n", curClient->ID,curClient->sock);
	    do {
			bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
			if(bytes < 0) error("ERROR writing to socket");
			if(bytes == 0) break;
			sent+=bytes;
	    } while (sent < total);
    	fprintf(stderr, "ERROR: booting client '%s' with sockfd %d\n",curClient->ID, curClient->sock);
    }

    handleExit(curClient);
}

/* Send client all player data */
void handlePlayerRequest(struct clientInfo *curClient) {

	string augCSV = vectorToAugmentedCSV(playerData);

	// There is certainly a better way to do this
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
    do {
		bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);

    /* send augCSVBuffer */
    total = augCSV.size(); sent = 0;
    do {
		bytes = custom_write(curClient->sock, (char *)&augCSVBuffer+sent, total-sent, curClient->ssl);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);
	memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
}

/* client has requested to draft a particular player */
void handleDraftRequest(clientInfo *curClient) {
	//fprintf(stderr, "Recieved draft request from %s for %s\n", curClient->ID, curClient->partialData);
	if(draftStarted) {
		if(strcmp(playerData[theDraft.order[theDraft.index]].PLAYER_NAME,curClient->partialData) == 0) {
			for(int i = 0; i < theDraft.teams.size(); i++) {
				// Find this client's team; if not found nothing really happens
				if(strcmp(theDraft.teams[i].owner,curClient->ID) == 0) {
					// Make the modified timestamp
					timespec adjustedTimeReceived;
					clock_gettime(CLOCK_MONOTONIC,&theDraft.teams[i].adjustedTimeReceived);
					
					int handicap = maxDelay - curClient->estRTT;
					//fprintf(stderr, "Adding %d seconds and %d useconds to adjustedTimeReceived\n", handicap / 1000, (handicap % 1000) * 1000000);
					theDraft.teams[i].adjustedTimeReceived.tv_sec += handicap / 1000;
					theDraft.teams[i].adjustedTimeReceived.tv_nsec += ((handicap % 1000) * 1000000);

					//fprintf(stderr, "%s's adjustedTimeReceived.tv_sec and tv_nsec: %d, %d\n", theDraft.teams[i].owner,theDraft.teams[i].adjustedTimeReceived.tv_sec, theDraft.teams[i].adjustedTimeReceived.tv_nsec);
					theDraft.teams[i].responseRecieved = true;
					if(roundIsOver()) endDraftRound();
				}
			}
		} else {
			//fprintf(stderr, "Draft request for invalid player recieved from %s\n", curClient->ID);
		}
	} else {
		// Pre-draft drafting is a bit more chill
		for(int i = 0; i < playerData.size(); i++) {
			if(strcmp(playerData[i].PLAYER_NAME, curClient->partialData) == 0) {
				if(strcmp(playerData[i].owner,"Server") == 0) {
					strcpy(playerData[i].owner,curClient->ID);
				}

				fprintf(stderr, "The owner of %s is now %s\n",playerData[i].PLAYER_NAME,playerData[i].owner);
			}
		}

		// Tell everyone the news (by sending them a ton of data...DoS attack potential here!)
		for(int i = 0; i < MAXCLIENTS; i++) {
			if(clients[i].active) {
				handlePlayerRequest(&clients[i]);		
			}
		}
	}
}

/* client doesn't want this player, but wants to keep things moving */
void handleDraftPass(clientInfo *curClient) {
	if(strcmp(playerData[theDraft.order[theDraft.index]].PLAYER_NAME,curClient->partialData) == 0) {
		for(int i = 0; i < theDraft.teams.size(); i++) {
			if(strcmp(theDraft.teams[i].owner,curClient->ID) == 0) {
				theDraft.teams[i].responseRecieved = true;
				if(roundIsOver()) endDraftRound();
			}
		}
	}
}

/* client changed their mind about if they're ready to draft */
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

		// This is info we should probably send to everyone
		s += "There are "; s += to_string(readyClients); s += " clients ready and ";
		s += to_string(totalClients - readyClients); s += " that are not ready.\n";
	} else {
		// Not sure what the client is hoping to accomplish here
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
		bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);
	
	total = strlen(stringBuffer) + 1;
    sent = 0;
    while(sent < total) {
        bytes = custom_write(curClient->sock, stringBuffer+sent, total-sent, curClient->ssl);
        if(bytes < 0) error("ERROR writing to socket");
        if(bytes == 0) break;
        sent+= bytes;
    }

    if((totalClients == readyClients) && !draftStarted) {
    	startDraft = true;
    }
}

/* A client pinged us back! */
void handlePingResponse(clientInfo *curClient) {
	fprintf(stderr, "Ping recieved from %s: %d, curClient->pingSent: %d\n", curClient->ID, curClient->msgID, curClient->pingSent);
	curClient->pingRcvd = curClient->msgID;

	if(curClient->pingRcvd != curClient->pingSent) {
		return; // If pings are out of sync God help us...or just ignore it
	}

	struct timespec sentTime, curTime;
	clock_gettime(CLOCK_MONOTONIC,&curTime);
	memcpy((char *)&sentTime,curClient->partialData,sizeof(sentTime)); // Time ping was sent

	int delayms = ((curTime.tv_sec * 1000) + (curTime.tv_nsec / 1000000)) - ((curClient->lastPingSent.tv_sec * 1000) + (curClient->lastPingSent.tv_nsec / 1000000));
	fprintf(stderr, "Delay between ping response sent and ping response recieved: %d\n", delayms);


	if(delayms < curClient->timeout) {
		if(curClient->estRTT == 0) { // First ping!
			curClient->estRTT = (float)delayms;
		} else {
			curClient->estRTT = (alpha * curClient->estRTT) + (delayms * (1.0 - alpha));
		}

		curClient->devRTT = (beta * fabs(delayms - curClient->estRTT)) + ((1.0 - beta) * curClient->devRTT);
		curClient->timeout = max((curClient->estRTT + (4 * curClient->devRTT)), minTimeout);

		curClient->pings++;
		fprintf(stderr, "Client %s has estRTT: %f, devRTT: %f, and timeout: %f\n", curClient->ID,curClient->estRTT,curClient->devRTT,curClient->timeout);

		// Motif for updating maxDelay...if it's possible to recduce it we want to!
		maxDelay = 0;

		for(int i = 0; i < MAXCLIENTS; i++) {
			if((clients[i].timeout > maxDelay) && clients[i].active) {
				maxDelay = clients[i].timeout;
			}
		}
	} else {
		fprintf(stderr, "Ping took too long; ping: %d, timeout: %f \n", delayms, curClient->timeout);
		curClient->timeout += (0.02 * curClient->timeout); // be a little more lenient!
		curClient->devRTT += (0.02 * curClient->estRTT);
		fprintf(stderr, "Adjusting timeout to %f\n", curClient->timeout);

		maxDelay = 0;

		for(int i = 0; i < MAXCLIENTS; i++) {
			if((clients[i].timeout > maxDelay) && clients[i].active) {
				maxDelay = clients[i].timeout;
			}
		}
	}
}


/********************************************************************
 *			Section 4: Method for sending a ping 					*
 ********************************************************************/

/* This one's pretty self explanatory */
void sendPing(clientInfo *curClient) {

	struct header responseHeader;
	memset(&responseHeader,0,sizeof(responseHeader));
    responseHeader.type = htons(PING);
    strcpy(responseHeader.sourceID, "Server");
    strcpy(responseHeader.destID, curClient->ID);
    responseHeader.dataLength = htonl(0);
    responseHeader.msgID = htonl(++curClient->pingSent);

    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    fprintf(stderr, "sendPing: Writing to %s with sock %d, ping #: %d\n", curClient->ID,curClient->sock,ntohl(responseHeader.msgID));
    do {
	    clock_gettime(CLOCK_MONOTONIC,&curClient->lastPingSent);
		bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);
}


/********************************************************************
 *			Section 5: Methods for managing the draft				*
 ********************************************************************/

 /* Start the draft */
void sendStartDraft() {
	memset(&theDraft,0,sizeof(theDraft));
	startDraft = false;

	// Get the message out
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
    	if(clients[i].active) {
		    strcpy(responseHeader.destID, clients[i].ID);
    		
		    int bytes, sent, total;
		    total = HEADERSIZE; sent = 0;
		    fprintf(stderr, "sendStartDraft: Writing to %s with sock %d\n", clients[i].ID, clients[i].sock);
		    do {
				bytes = custom_write(clients[i].sock, (char *)&responseHeader+sent, total-sent, clients[i].ssl);
				if(bytes < 0) error("ERROR writing to socket");
				if(bytes == 0) break;
				sent+=bytes;
		    } while (sent < total);

		   	total = strlen(stringBuffer) + 1;	
		    sent = 0;
		    while(sent < total) {
		        bytes = custom_write(clients[i].sock, stringBuffer+sent, total-sent, clients[i].ssl);
		        if(bytes < 0) error("ERROR writing to socket");
		        if(bytes == 0) break;
		        sent+= bytes;
		    }

		    // Each active client gets a team
		    struct team newTeam;
		    memset(&newTeam,0,sizeof(newTeam));
		    strcpy(newTeam.owner, clients[i].ID);
		    theDraft.teams.push_back(newTeam);
    	}
    }

    // Clear any existing claims on players
    for(int i = 0; i < playerData.size(); i++) {
    	memset(playerData[i].owner,0,IDLENGTH);
    	strcpy(playerData[i].owner,"Server");
    	theDraft.order.push_back(i);
    }

    random_shuffle(theDraft.order.begin(), theDraft.order.end()); // Randomize the order
    draftNum++; // Only relevant at the end when results need to be written to a unique file

   	usleep(maxDelay * 1000); // Wait for everyone to catch up
   	draftStarted = true;
   	startNewRound = true;
   	curRoundPingsSent = false;
}

/* Start a new round of the draft */
void draftNewRound() {
	startNewRound = false;
    theDraft.currentRound++;
    theDraft.index = theDraft.currentRound - 1;
    theDraft.index = theDraft.index % playerData.size(); // loop around if it gets to that point

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

    for(int i = 0; i < MAXCLIENTS; i++) {
    	if(clients[i].active) {
		    strcpy(responseHeader.destID, clients[i].ID);
		    
			int bytes, sent, total;
		    total = HEADERSIZE; sent = 0;
		    fprintf(stderr, "draftNewRound: Writing to %s with sock %d\n", clients[i].ID, clients[i].sock);
		    do {
				bytes = custom_write(clients[i].sock, (char *)&responseHeader+sent, total-sent, clients[i].ssl);
				if(bytes < 0) error("ERROR writing to socket");
				if(bytes == 0) break;
				sent+=bytes;
		    } while (sent < total);

		   	total = strlen(curPlayer) + 1;	
		    sent = 0;
		    while(sent < total) {
		        bytes = custom_write(clients[i].sock, curPlayer+sent, total-sent, clients[i].ssl);
		        if(bytes < 0) error("ERROR writing to socket");
		        if(bytes == 0) break;
		        sent+= bytes;
		    }
		}
    }

	// Clean slate for the new round        
    for(int i = 0; i < theDraft.teams.size(); i++) {
    		theDraft.teams[i].adjustedTimeReceived.tv_sec = 0;
	   		theDraft.teams[i].adjustedTimeReceived.tv_nsec = 0;
	   		theDraft.teams[i].responseRecieved = false;
    }

    // roundTotaltime is ROUNDTIME plus enough to account for everyone's latency 
    timespec roundTotalTime; roundTotalTime.tv_sec = ((int)maxDelay / 1000) + ROUNDTIME; 
    roundTotalTime.tv_nsec = ((int)maxDelay % 1000) * 1000000;
    //fprintf(stderr, "roundTotalTime.tv_sec: %d, tv_nsec: %d\n", roundTotalTime.tv_sec, roundTotalTime.tv_nsec);

    timespec curTime;
    clock_gettime(CLOCK_MONOTONIC,&curTime);

    // Round should end after roundTotalTime has ellapsed
    timespec roundEndTime;
    timespecAdd(&roundTotalTime,&curTime,&roundEndTime); 
    
    theDraft.roundEndTime = roundEndTime;
}

/* The round is over */
void endDraftRound() {

	char winner[IDLENGTH];
	memset(winner,0,IDLENGTH);

	timespec quickest;
	quickest.tv_sec = theDraft.roundEndTime.tv_sec;
	quickest.tv_nsec = theDraft.roundEndTime.tv_nsec;

	// Find the team that had the quickest effective time
	for(int i = 0; i < theDraft.teams.size(); i++) {
		if(theDraft.teams[i].adjustedTimeReceived.tv_sec != 0) {
			//fprintf(stderr, "Team %s had adjustedTimeReceived.tv_sec: %d and tv_nsec: %d\n", theDraft.teams[i].owner, theDraft.teams[i].adjustedTimeReceived.tv_sec, theDraft.teams[i].adjustedTimeReceived.tv_nsec);
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
			}
		}
	} else {
		fprintf(stderr, "No one claimed %s in round %d of the draft\n", playerData[theDraft.order[theDraft.index]].PLAYER_NAME, theDraft.currentRound);
	}

    // Tell everyone who won!
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
				bytes = custom_write(clients[i].sock, (char *)&responseHeader+sent, total-sent, clients[i].ssl);
				if(bytes < 0) error("ERROR writing to socket");
				if(bytes == 0) break;
				sent+=bytes;
		    } while (sent < total);
	
			total = IDLENGTH;
		    sent = 0;
		    while(sent < total) {
		        bytes = custom_write(clients[i].sock, winner+sent, total-sent, clients[i].ssl);
		        if(bytes < 0) error("ERROR writing to socket");
		        if(bytes == 0) break;
		        sent+= bytes;
		    }			
		}
	}
	
	bool readyToEnd = true;
	for(int i = 0; i < theDraft.teams.size(); i++) {
		if(theDraft.teams[i].playersDrafted != TEAMSIZE) {
			// Found a team that isn't full, but is the owner active?
			bool foundAndActive = false;
			for(int j = 0; j < MAXCLIENTS; j++) {
				if((strcmp(clients[j].ID,theDraft.teams[i].owner) == 0)  && clients[j].active) {
					// Sure is!	
					foundAndActive = true;
				}
			}
			if(foundAndActive) {
				// So we're not done yet
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
		curRoundPingsSent = false;
	}
}

/* Let's end this thing */
void endDraft() {
	draftStarted = false;

	// Get the word out
    struct header responseHeader;
    responseHeader.type = htons(DRAFT_END);
    strcpy(responseHeader.sourceID, "Server");
    responseHeader.dataLength = htonl(0);
    responseHeader.msgID = htonl(draftNum); // Client will be able to write to a unique file

	for(int i = 0; i < MAXCLIENTS; i++) {
		clients[i].readyToDraft = false;
		if(clients[i].active) {

		    memcpy(responseHeader.destID, clients[i].ID, IDLENGTH);

		    int bytes, sent, total;
		    total = HEADERSIZE; sent = 0;
		    fprintf(stderr, "endDraft: Writing to %s with sock %d\n", clients[i].ID, clients[i].sock);
		    do {
				bytes = custom_write(clients[i].sock, (char *)&responseHeader+sent, total-sent, clients[i].ssl);
				if(bytes < 0) error("ERROR writing to socket");
				if(bytes == 0) break;
				sent+=bytes;
		    } while (sent < total);			
		}
	}

	// Free up the players
	for(int i = 0; i < playerData.size(); i++) {
		memset(playerData[i].owner,0,IDLENGTH);
		strcpy(playerData[i].owner,"Server");
	}
}


/********************************************************************
 *		Section 6: Helper for deciding if draft round is over		*
 ********************************************************************/

/* true if conditions are met for ending round, false otherwise */
bool roundIsOver() {
	bool allResponsesRecieved = true;
	for(int i = 0; i < theDraft.teams.size(); i++) {
		if(!theDraft.teams[i].responseRecieved) 
			for(int j = 0; j < MAXCLIENTS; j++) {
				if((strcmp(clients[j].ID,theDraft.teams[i].owner) == 0) && clients[j].active) {
					allResponsesRecieved = false;
				}
			}
	}
	return allResponsesRecieved;
}


/********************************************************************
 *		Section 7: Helpers for timespec comparison and arithmetic	*
 ********************************************************************/

/* Sum of a and b placed in c */
void timespecAdd(timespec *a, timespec *b, timespec *c) {
	time_t secs = a->tv_sec + b->tv_sec + ((a->tv_nsec + b->tv_nsec) / 1000000000);
	long nsecs = (a->tv_nsec + b->tv_nsec) % 1000000000;

	c->tv_sec = secs;
	c->tv_nsec = nsecs;
}

/* Difference of a and b placed in c */
/* Assumes a > b; not sure what happens with negative time o.O */
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

/* true if a is earlier than b, false otherwise */
bool timespecLessthan(timespec *a, timespec *b) {
	if(a->tv_sec < b->tv_sec) return true;
	if(a->tv_sec > b->tv_sec) return false;
	if(a->tv_nsec < b->tv_nsec) return true;
	return false;
}

/********************************************************************
 *		 Section 8: Helper for encrypting user passwords			*
 ********************************************************************/

/* Thanks to the internet; do not recall the source, but this is not our code */
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

void readFromClientForProxy () {
    int i;
    struct clientInfo *curClient;
    //fprintf(stderr, "sockfd: %d\n", sockfd);
	bool foundMatchingClientID = 0; /* Shawyoun */
    /* get client address based on sockfd. Shawyoun: now get it based on currentClientID */
	fprintf(stderr, "Reached readFromClient. currentClientID is %s\n", currentClientID);
	fprintf(stderr, "Value of foundMatchingClientID before loop: %i\n", foundMatchingClientID);
    for(i = 0; i < MAXCLIENTS; i++) {
		if(strcmp(currentClientID, "") != 0 && strcmp(clients[i].ID, currentClientID) == 0 && clients[i].sock != -1) { /* reimagined */
			/*if(clients[i].active) {  Shawyoun */
			    curClient = &clients[i];
				foundMatchingClientID = true;
				fprintf(stderr, "Found matching client id: %s. Client entry i = %i\n", clients[i].ID, i);
			    break;
			/*} else { Shawyoun */
				/* Shawyoun: not sure if this is necessary anymore. We're always looking on the same sock.
				
					struct clientInfo newClient;
				    memset(&newClient, 0, sizeof(newClient));
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
		    //fprintf(stderr, "New client made with previously existing sockfd: %d\n", sockfd); */
			/* Shawyoun } */
		}
    }
	
	/* Shawyoun: If we don't find a matching ID (because we're in a "readHeader" state), 
	malloc curClient to create a "candidate" client. Once we've read the header, we can determine if it's actually
	a new client (and thus should be inserted into entries array). */
	fprintf(stderr, "Value of foundMatchingClientID: %i\n", foundMatchingClientID);
	if(!foundMatchingClientID){
		fprintf(stderr, "Did not find matching clientID\n");
		curClient = (struct clientInfo *)malloc(sizeof(struct clientInfo)); 
		memset(curClient, 0, sizeof(struct clientInfo));
		curClient->active = true;
		curClient->validated = false;
		curClient->sock = 1; /* reimagined */
		curClient->headerToRead = HEADERSIZE;
		curClient->timeout = 5000; /* Shawyoun merged */
		curClient->readyToDraft = false;
	}
    /* data takes precedence over headers since headerToRead should always 
       be > 0 */
    if(curClient->pwordToRead > 0) {
	fprintf(stderr, "curClient->pwordToRead: %i\n", curClient->pwordToRead);
    	readPwordForProxy(curClient);
    } else if(curClient->dataToRead > 0) {
	fprintf(stderr, "curClient->dataToRead: %i\n", curClient->dataToRead);
		readDataForProxy(curClient);
    } else if(curClient->headerToRead > 0) {
	fprintf(stderr, "About to enter readHeaderForBackEnd\n");
		readHeaderForProxy(curClient);
    } else {
		error("ERROR: headerToRead and dataToRead not > 0");
    }
}

/* Shawyoun: keeping this one as is for now */
void readHeaderForProxy(struct clientInfo *curClient) {
    fprintf(stderr, "Reached readHeader method\n");
    char header_buffer[HEADERSIZE];
    int nbytes;
    /* retrieve what has already been read of the header */
    memcpy(header_buffer, curClient->partialHeader, HEADERSIZE);
    /* try to read the rest of the header */
    nbytes = custom_read (proxyFrontEndSock, &header_buffer[HEADERSIZE-curClient->headerToRead],curClient->headerToRead, curClient->ssl);

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
		
		/* Shawyoun: check if header's clientID matches an existing ID. If not, it's new and we need to add entry 
		If it matches one, we can repurpose the curClient pointer for the matching entry. 
		Either way, we also need to update the currentClientID later */
		fprintf(stderr, "Reached beginning of new logic in readHeader\n");
		bool foundMatchingClientID = 0;
		int i;
		for(i = 0; i < MAXCLIENTS; i++) {
			if(strcmp(newHeader.sourceID, "") != 0 && strcmp(clients[i].ID, newHeader.sourceID) == 0 && (clients[i].sock != -1)) { /* reimagined */
				foundMatchingClientID = true;
				curClient = &clients[i];
				fprintf(stderr, "Found matching ID within readHeader method. Client entry i = %i\n", i);
				break;
			}
		}
	
		if(!foundMatchingClientID){
			fprintf(stderr, "Did not find matching ID within readHeader method\n");
			bool inserted = false;
			while(!inserted) {
				if(strcmp(clients[clientCounter].ID, "") == 0) { 
					fprintf(stderr, "Found an empty client entry to stick this new one in. Client entry clientCounter = %i\n", clientCounter);
					clients[clientCounter] = *curClient;
					curClient = &clients[clientCounter];
					fprintf(stderr, "The empty client entry has curClient->ID: %s\n", curClient->ID);
					inserted = 1;
				}
				clientCounter++;
				numClients++;
				clientCounter = clientCounter % MAXCLIENTS;
			}		
		}
		memcpy(currentClientID, newHeader.sourceID, IDLENGTH);
		/* End Shawyoun */
		
		newHeader.type = ntohs(newHeader.type);
		newHeader.dataLength = ntohl(newHeader.dataLength);
		newHeader.msgID = ntohl(newHeader.msgID);
		curClient->headerToRead = HEADERSIZE;

		fprintf (stderr, "Read-in header: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", newHeader.type, newHeader.sourceID, newHeader.destID, newHeader.dataLength, newHeader.msgID);
		
		/* TODO Shawyoun: perhaps curClient->ID needs to be changed to currentClientID here */

		/* identify potential bad input */
		if((strcmp(newHeader.sourceID,curClient->ID) != 0) && (strcmp(curClient->ID,"") != 0) && curClient->active) {
	    	fprintf(stderr, "ERROR: wrong user ID in header: Expecting %s but got %s\n", curClient->ID, newHeader.sourceID);
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


		    /*Shawyoun handle HELLO-specific bad input
	    	if(strcmp(curClient->ID,"") != 0) {
		    	// If there is already and ID, this is not this client's first interaction with the server
		    	fprintf(stderr, "ERROR: Attempt to HELLO from previously seen client\n");
	    		handleError(curClient);
		    } */
	    
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
			memset(currentClientID, 0, IDLENGTH); /* Shawyoun */ /* TODO Shawyoun: any request type where header is all there is, I would think you can do this immeidately*/
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
			memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
		} else if (newHeader.type == BAD_EXIT){
			handleExit(curClient); /* Shawyoun exit without resetting variables */
			memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
		}
		else if(newHeader.type == PLAYER_REQUEST) {
			//string augCSV = vectorToAugmentedCSV(playerData);
			handlePlayerRequest(curClient);
			memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
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
			memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
		} else if (newHeader.type == DRAFT_PASS) {
			memset(curClient->partialData,0,MAXDATASIZE);
			curClient->mode = DRAFT_PASS;
			curClient->msgID = newHeader.msgID;
			curClient->dataToRead = newHeader.dataLength;
			curClient->totalDataExpected = newHeader.dataLength;
		} else {
	    	fprintf(stderr, "ERROR: bad header type\n");
		    handleError(curClient);
		    return;
		}
    }
}

/* Shawyoun: keeping this one as is for now */
void readDataForProxy(struct clientInfo *curClient) {
    /* same logic for reading as in readHeader */
    char data_buffer[curClient->totalDataExpected];
    int nbytes;

    memcpy(data_buffer, curClient->partialData, curClient->totalDataExpected);
    nbytes = custom_read (proxyFrontEndSock, &data_buffer[curClient->totalDataExpected-curClient->dataToRead],curClient->dataToRead, curClient->ssl);

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
			memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
		} else if(curClient->mode == DRAFT_REQUEST) {
			handleDraftRequest(curClient);
			memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
		} else if(curClient->mode == PING_RESPONSE) {
			handlePingResponse(curClient);
			memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
		} else if(curClient->mode == DRAFT_PASS) {
			handleDraftPass(curClient);
			memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
		}else {
			fprintf(stderr, "ERROR: Done reading data but client in invalid mode\n");
			handleError(curClient);
		}
    }
}

/* Shawyoun: keeping this one as is for now */

void readPwordForProxy(struct clientInfo *curClient) {
	char data_buffer[curClient->totalPwordExpected];
	int nbytes;

	memcpy(data_buffer, curClient->pword, curClient->totalPwordExpected);
	nbytes = custom_read (proxyFrontEndSock, &data_buffer[curClient->totalPwordExpected - curClient->pwordToRead], curClient->pwordToRead, curClient->ssl);

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
		fprintf(stderr, "Hashed pword: %s has length %d\n", hashed, hashed.length());

		memcpy(curClient->pword, hashBuffer, hashed.length());
		fprintf(stderr, "Password read in: curClient->password: %s\n", curClient->pword);

		curClient->pwordToRead = 0;

		handleHelloForProxy(curClient);
		memset(currentClientID, 0, IDLENGTH); /* TODO Shawyoun: the other place to put this may be at the end of the readData message. Maybe even readHeader, if it's a message that doesn't have data. What about the "write" methods? Alternatively, stick them at end of handle methods*/
	}
}

/* Shawyoun: did not merge the validation part yet. Only the hello_ack stuff */
void handleHelloForProxy(struct clientInfo *curClient) {
    /* Add client ID to clientInfo */
    // memcpy(curClient->ID, ID, IDLENGTH);
    bool returning = false;

	if(curClient->validated == false) {
		returning = true;
		
		for(int i = 0; i < MAXCLIENTS; i++) {
			if(strcmp(clients[i].ID, curClient->ID) == 0 && !(clients[i].sock == curClient->sock)) { /* Reimagined : this condition must be changed now */
				fprintf(stderr, "In handleHello, clients[%i].ID: %s with sock: %i and curClient->ID: %s with sock: %i\n", i,
					clients[i].ID,clients[i].sock,curClient->ID, curClient->sock);
				if(strcmp(clients[i].pword, curClient->pword) == 0) {
					fprintf(stderr, "Password matches password of existing client, removing duplicate\n");
					curClient->validated = true; /* reimagined */
					handleError(&clients[i]);
				} else {
					fprintf(stderr, "Password does not match existing client! You're fired!\n");
					curClient->validated = false; /* reimagined */
					curClient->active = false; /*reimagined */
					handleError(curClient);
					return;
				}
			}
		}
	}

	char helloMessage[50];
	memset(helloMessage,0,50);

	// I later learned easier ways to to this but hey
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
	
    //fprintf (stderr, "HELLO_ACK responseHeader: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", ntohs(responseHeader.type), responseHeader.sourceID, responseHeader.destID, ntohl(responseHeader.dataLength), ntohl(responseHeader.msgID));

    /* send HELLO_ACK */
    int bytes, sent, total;
    total = HEADERSIZE; sent = 0;
    fprintf(stderr, "handleHello: Writing to %s with sock %d\n", curClient->ID,curClient->sock);
    do {
		bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
		if(bytes < 0) error("ERROR writing to socket");
		if(bytes == 0) break;
		sent+=bytes;
    } while (sent < total);
	
	total = strlen(helloMessage) + 1;
    sent = 0;
    while(sent < total) {
        bytes = custom_write(curClient->sock, helloMessage+sent, total-sent, curClient->ssl);
        if(bytes < 0) error("ERROR writing to socket");
        if(bytes == 0) break;
        sent+= bytes;
        //fprintf(stdout,"Sent %d bytes of the helloMessage\n");
    }

    handleListRequest(curClient);

    // curClient->timeout should be the default
    if(maxDelay < curClient->timeout) maxDelay = curClient->timeout;

	// If the client had previously participated in the draft
	// Note: All clients are set to not ready at the end of the draft so a 
	// client will only return to a draft they were previously involved in
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
	    fprintf(stderr, "handleHello (draftStarted): Writing to %s with sock %d\n", curClient->ID,proxyFrontEndSock);
	    do {
			bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
			if(bytes < 0) error("ERROR writing to socket");
			if(bytes == 0) break;
			sent+=bytes;
	    } while (sent < total);

	    total = strlen(stringBuffer) + 1;	
		sent = 0;
		while(sent < total) {
	        bytes = custom_write(curClient->sock, stringBuffer+sent, total-sent, curClient->ssl);
	        if(bytes < 0) error("ERROR writing to socket");
	        if(bytes == 0) break;
	        sent+= bytes;
	    }
    }

    // Even if the client isn't participating, they need to get the information about the ongoing draft
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
	    fprintf(stderr, "handleHello (draftStarted, client not ready): Writing to %s with sock %d\n", curClient->ID,proxyFrontEndSock);
	    do {
			bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
			if(bytes < 0) error("ERROR writing to socket");
			if(bytes == 0) break;
			sent+=bytes;
	    } while (sent < total);

	    total = strlen(stringBuffer) + 1;	
		sent = 0;
		while(sent < total) {
	        bytes = custom_write(curClient->sock, stringBuffer+sent, total-sent, curClient->ssl);
	        if(bytes < 0) error("ERROR writing to socket");
	        if(bytes == 0) break;
	        sent+= bytes;
	    }    	
    }

    // Tell everyone else a client has logged in
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
		    fprintf(stderr, "handleHello (CHAT to active clients): Writing to %s with sock %d\n", clients[i].ID,proxyFrontEndSock);
		    do {
				bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
				if(bytes < 0) error("ERROR writing to socket");
				if(bytes == 0) break;
				sent+=bytes;
		    } while (sent < total);

		    total = strlen(stringBuffer) + 1;	
			sent = 0;
			while(sent < total) {
	        	bytes = custom_write(curClient->sock, stringBuffer+sent, total-sent, curClient->ssl);
		        if(bytes < 0) error("ERROR writing to socket");
		        if(bytes == 0) break;
	    	    sent+= bytes;
		    }

		    handleListRequest(&clients[i]);
		}
    }

    // Shorten the select timeout to get the new client a ping ASAP
    newEntry = true;
}

void handleExit(struct clientInfo * curClient){
	if(proxyMode)
		handleExitForProxy(curClient);
	else
		handleExitForClient(curClient);
}

/* Shawyoun: keeping this one as is for now */
void handleExitForProxy(struct clientInfo *curClient) {
	/* TODO Shawyoun: I think we need to clear our currentClientID */
	if(curClient->active && curClient->validated) {
		fprintf(stderr, "exit: active and validated\n");
		/* Shawyoun int sock = proxyFrontEndSock;
		close(sock);
		FD_CLR(sock, &read_fd_set);
		FD_CLR(sock, &active_fd_set); */
		curClient->active = false;
		curClient->sock = -1; /* Reimagined */
	} else {
		if(!curClient->active) fprintf(stderr, "exit: not active\n");
		if(!curClient->validated) fprintf(stderr, "exit: not validated\n");
		/* Shawyoun int sock = proxyFrontEndSock;
		close(sock);
		FD_CLR(sock, &read_fd_set);
		FD_CLR(sock, &active_fd_set); */
		int okToClear = 1; /* Reimagined */
	    for(int i = 0; i < MAXCLIENTS; i++) {
	    	/* reimagined if(strcmp(clients[i].ID, "") == 0) break;
			if(((strcmp(curClient->ID,clients[i].ID) == 0) && (!(clients[i].active)) || !(clients[i].validated)){
				if(!curClient->validated) {
					for(int i = 0; i < playerData.size(); i++) {
						if(strcmp(curClient->ID,playerData[i].owner) == 0) {
							memset(playerData[i].owner,0,IDLENGTH);
							strcpy(playerData[i].owner,"Server");
						}
					}
				}
			    fprintf(stderr,"permanently removing client %s with sockfd %d\n",curClient->ID,proxyFrontEndSock);
 			    memset(curClient, 0, sizeof(struct clientInfo));
			} reimagined */
		fprintf(stderr, "Just before long print statement\n");
		fprintf(stderr, "Comparing curClient %s with sock %i and active %i and validated %i\n against client[%i]: %s with sock %i and active %i and validated %i\n", curClient->ID, curClient->sock, curClient->active, curClient->validated, i, clients[i].ID, clients[i].sock, clients[i].active, clients[i].validated);
		if(strcmp(curClient->ID, clients[i].ID) == 0 && !(clients[i].active) && clients[i].validated && clients[i].sock == -1){
			okToClear = 0;
			fprintf(stderr, "It's NOT okay to clear\n");
			break;
		}
	    } /* reimagined */
		if(okToClear){
			for(int i = 0; i < playerData.size(); i++) {
						if(strcmp(curClient->ID,playerData[i].owner) == 0) {
							memset(playerData[i].owner,0,IDLENGTH);
							strcpy(playerData[i].owner,"Server");
						}
					}
		}
	    	numClients--;
		fprintf(stderr,"permanently removing client %s with sockfd %d\n",curClient->ID,proxyFrontEndSock);
		memset(curClient, 0, sizeof(struct clientInfo)); /* reimagined */
	}
	memset(currentClientID, 0, IDLENGTH); /* Shawyoun */
}

// Relic from Assignment 2
void handleErrorForProxy(struct clientInfo *curClient) {
	/* TODO Shawyoun: I think we have to reset the currentClientID when we get an error */
	if(curClient->sock != -1) { /* Reimagined */
		curClient->validated = false; /* Reimagined */
		curClient->active = false; /* Reimagined */
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
			bytes = custom_write(curClient->sock, (char *)&responseHeader+sent, total-sent, curClient->ssl);
			if(bytes < 0) error("ERROR writing to socket");
			if(bytes == 0) break;
			sent+=bytes;
	    } while (sent < total);
    	fprintf(stderr, "ERROR: booting client '%s' with sockfd %d\n",curClient->ID, proxyFrontEndSock);
    }

    handleExit(curClient);
}

void handleError(struct clientInfo* curClient){
	if(proxyMode)
		handleErrorForProxy(curClient);
	else
		handleErrorForClient(curClient);
}

int custom_read(int fd, void * buffer, int nbytes, SSL* secure_fd){
	if(proxyMode){
		return read(proxyFrontEndSock, buffer, nbytes);
	}else if(secureMode && fd){
		return SSL_read(secure_fd, buffer, nbytes);
	}else{
		return read(fd, buffer, nbytes);
	}
}

int custom_write(int fd, void * buffer, int nbytes, SSL* secure_fd){
	if(proxyMode)
		return write(proxyFrontEndSock, buffer, nbytes);
	else if(secureMode && fd){
		return SSL_write(secure_fd, buffer, nbytes);
	}else{
		return write(fd, buffer, nbytes);
	}
}

