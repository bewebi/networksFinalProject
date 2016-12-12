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
#define MAXDATASIZE 150000
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
#define DRAFT_PASS 19
#define DRAFT_END 20 // # of draft that just finished
#define BAD_EXIT 25 /* Shawyoun */

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
    int mode; /* TODO Shawyoun: will there need to be a mode for backend? */
	int modeForWrite;

    int headerToRead;
	int headerToWrite; /* Shawyoun */
    char partialHeader[HEADERSIZE];
	char partialHeaderForWrite[HEADERSIZE]; /* Shawyoun */

    int dataToRead;
	int dataToWrite; /* Shawyoun */
    int totalDataExpected;
	int totalDataExpectedToWrite; /* Shawyoun */
    char partialData[MAXDATASIZE];
	char partialDataForWrite[MAXDATASIZE]; /* Shawyoun */
    char destID[IDLENGTH];
	char destIDForWrite[IDLENGTH]; /* Shawyoun */
    int msgID;
	int msgIDForWrite; /* Shawyoun */ 

    int pwordToRead;
	int pwordToWrite; /* Shawyoun: don't expect to use this */
    int totalPwordExpected;
	int totalPwordExpectedtoWrite; /* Shawyoun: don't expect to use this */
    char pword[65];

    bool validated;

    int pingID;
    int pings;
    struct timeval lastPingSent;
    float estRTT;
    float devRTT;
    float timeout;

    bool readyToDraft;
	/* Shawyoun */
    char fullHeaderToRead[HEADERSIZE];
    char fullPwordToRead[65];
    char fullDataToRead[MAXDATASIZE]; /* TODO: this needs to be used too. for now we've only incorporated fullPwordToRead because that's the equivalent of "data" for HELLO */
    char fullHeaderToWrite[HEADERSIZE];
    char fullPwordToWrite[65]; /* Shawyoun: don't think this will be used */
    char fullDataToWrite[MAXDATASIZE];
	/* End Shawyoun */

	bool validReceiver = 0; /* Shawyoun */
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

char currentClientIDFromBackEndServer[IDLENGTH]; /* Shawyoun */
/*TODO Shawyoun: you'll have to reset currentClientIDFromBackEndServer just like on backend */
int backEndServerSock; /* Shawyoun */

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

void readFromClient(int sockfd);

void readHeader(struct clientInfo *curClient, int sockfd);
void readData(struct clientInfo *curClient);
void readPword(struct clientInfo *curClient);

/* TODO Shawyoun: either write different methods or branch the logic within the method, so that it behaves differently if it's the backend "client" */
void handleHello(struct clientInfo *curClient);
void handleListRequest(struct clientInfo *curClient);
void handleChat(struct clientInfo *curClient);
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

void readFromClientForBackEnd(); /* Shawyoun */
void readHeaderForBackEnd(struct clientInfo *curClient); /* Shawyoun */
void readDataForBackEnd(struct clientInfo *curClient); /* Shawyoun */

void handleHelloACK(struct clientInfo* curClient); /* Shawyoun */
void handleClientList(struct clientInfo* curClient); /* Shawyoun */
void handlePlayerResponse(struct clientInfo* curClient); /* Shawyoun */
void handleChatForWrite(struct clientInfo * curClient); /* Shawyoun */
void handleCannotDeliver(struct clientInfo * curClient); /* Shawyoun */
void handleErrorForWrite(struct clientInfo * curClient); /* Shawyoun */
void handleBadExit(struct clientInfo *curClient); /* Shawyoun */
void handleDraftStatusForWrite(struct clientInfo *curClient); /* Shawyoun */
void handleDraftStartingForWrite(struct clientInfo *curClient); /* Shawyoun */
void handleDraftRoundStartForWrite(struct clientInfo * curClient); /* Shawyoun */
void handleDraftRoundResultForWrite(struct clientInfo * curClient); /* Shawyoun */
void handleDraftPass(struct clientInfo* curClient); /* Shawyoun */

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
	
    struct sockaddr_in frontend_addr, cli_addr; /* Shawyoun */
   
    /* Shawyoun: add more arguments to specify the backend server */
    if (argc < 4) {
		fprintf(stderr,"Usage: ./proxy_frontend <frontendport> backendhostname <backendport>");
		exit(1);
    }
	
	/* Shawyoun */
	char * backEndHost = argv[2];
    int backEndPort = atoi(argv[3]);

    struct hostent * backEndServer;
    struct sockaddr_in backend_addr;
	/* End Shawyoun */

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
	backEndServerSock = socket(AF_INET, SOCK_STREAM, 0); /* Shawyoun */
	
    if (sockfd < 0 || backEndServerSock < 0)  /* Shawyoun */
       error("ERROR opening socket"); 
	   
	/* Shawyoun */
	backEndServer = gethostbyname(backEndHost);
    if(backEndServer == NULL) error("ERROR, no such host");

    memset(&backend_addr,0,sizeof(backend_addr));
    backend_addr.sin_family = AF_INET;
    backend_addr.sin_port = htons(backEndPort);
    memcpy(&backend_addr.sin_addr.s_addr,backEndServer->h_addr,backEndServer->h_length);

    if(connect(backEndServerSock,(struct sockaddr *)&backend_addr,sizeof(backend_addr)) < 0) error("ERROR connecting");
    /* connected = true; Shawyoun */
    fprintf(stdout, "Connected to the backend server!\n");
    /* end Shawyoun */
	
    bzero((char *) &frontend_addr, sizeof(frontend_addr)); 
	portno = atoi(argv[1]); // Get port number from args
	frontend_addr.sin_family = AF_INET; 
	frontend_addr.sin_addr.s_addr = INADDR_ANY; 
	frontend_addr.sin_port = htons(portno); 
    
    if (bind(sockfd, (struct sockaddr *) &frontend_addr, 
        sizeof(frontend_addr)) < 0)  
            error("ERROR on binding"); 

    listen(sockfd,5);
    FD_ZERO(&active_fd_set);
    FD_SET(sockfd, &active_fd_set);
	FD_SET(backEndServerSock, &active_fd_set); /* Shawyoun */
 
    clilen = sizeof(cli_addr); 
	
	/* TODO Shawyoun: Connect to backend server before doing anything else. Make sure to add this file descriptor to the set */
	
    while(1) {

		struct timeval selectTimeout = {5,0};
		read_fd_set = active_fd_set;
		/* Block until input arrives on one or more active sockets */
		if(select(FD_SETSIZE, &read_fd_set, NULL, NULL, &selectTimeout) < 0) {
		    error("ERROR on select");
		}

		/* Service all the sockets with input pending */
		for(i = 0; i < FD_SETSIZE; i++) { //FD_SETSIZE == 1024
	    	if(FD_ISSET(i, &read_fd_set)) {
	    		/* pingNow = false; */
				if(i == sockfd) { 
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
				newClient.validReceiver = false;

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
		 		} else if(i == backEndServerSock){ /* Shawyoun */
					fprintf(stderr, "Data came in from backend\n");
					/* TODO Shawyoun: fill this out */
					readFromClientForBackEnd();
				}
				else {
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
				newClient.validReceiver = false;
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
			/* Shawyoun */
		fprintf(stderr, "Partial header: ");
		fprintf(stderr, (char*)&curClient->partialHeader);
		/*End Shawyoun */

    /* retrieve what has already been read of the header */
    memcpy(header_buffer, curClient->partialHeader, HEADERSIZE);
    /* try to read the rest of the header */
    nbytes = read (curClient->sock, &header_buffer[HEADERSIZE-curClient->headerToRead],curClient->headerToRead);

    //fprintf(stderr, "readHeader client: %s, nbytes: %d, expected readsize: %d\n",curClient->ID, nbytes,curClient->headerToRead); 

    if (nbytes <= 0) {
		/* Read error or EOF: Socket closed */
    	// TODO: Pause mode
		handleBadExit(curClient); /* Shawyoun */
    } else if (nbytes < curClient->headerToRead) {
		/* Shawyoun */
		fprintf(stderr, "Partial header: ");
		fprintf(stderr, curClient->partialHeader);
		/*End Shawyoun */
		/* still more to read */
		curClient->headerToRead = curClient->headerToRead - nbytes;
		memcpy(curClient->partialHeader, header_buffer, HEADERSIZE);
    } else {
        /* Parse header. */
		/* Shawyoun */
		fprintf(stderr, "Partial header: ");
		fprintf(stderr, (char*)&curClient->partialHeader);
		/*End Shawyoun */
		struct header newHeader;
		memcpy((char *)&newHeader, &header_buffer[0], HEADERSIZE);
		memcpy(&curClient->fullHeaderToRead, &header_buffer[0], HEADERSIZE); /* Shawyoun */
	
		newHeader.type = ntohs(newHeader.type);
		newHeader.dataLength = ntohl(newHeader.dataLength);
		newHeader.msgID = ntohl(newHeader.msgID);
		curClient->headerToRead = HEADERSIZE;

		fprintf (stderr, "Read-in header: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", newHeader.type, newHeader.sourceID, newHeader.destID, newHeader.dataLength, newHeader.msgID);

		/* next step depends on header type */
		if(newHeader.type == HELLO) {
	
			/* Shawyoun: handle client already present case */
			int i; int duplicate = 0;
		    	for(i = 0; i < MAXCLIENTS; i++) {
				if(strcmp(clients[i].ID, newHeader.sourceID) == 0) {
					if(clients[i].validReceiver && curClient->validReceiver == false) {
						handleErrorForWrite(curClient);
						return;
					}
		    		}
			}
			
			curClient->validReceiver = true;
		
		    curClient->totalPwordExpected = newHeader.dataLength;
		    curClient->pwordToRead = newHeader.dataLength;
		    memcpy(curClient->ID, newHeader.sourceID, IDLENGTH);

		} else if(newHeader.type == LIST_REQUEST) {
		    handleListRequest(curClient);
		} else if(newHeader.type == CHAT) {
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
		} else if(newHeader.type == DRAFT_PASS){
			memset(curClient->partialData, 0, MAXDATASIZE);
			curClient->mode = DRAFT_PASS;
			curClient->dataToRead = newHeader.dataLength;
			curClient->totalDataExpected = newHeader.dataLength;
		} else {
	    	fprintf(stderr, "ERROR: bad header type\n");
		    /* Shawyoun handleError(curClient); */
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
		handleBadExit(curClient); /* Shawyoun */
    } else if (nbytes < curClient->dataToRead) {
	/* still more data to read */
		curClient->dataToRead = curClient->dataToRead - nbytes;
		memcpy(curClient->partialData, data_buffer, curClient->totalDataExpected);
    } else {
	/* All data has been read */
		memcpy(curClient->partialData, data_buffer, curClient->totalDataExpected);
		memcpy(curClient->fullDataToRead, data_buffer, curClient->totalDataExpected); /* Shawyoun */	
		fprintf(stderr,"Message read in: curClient->partialData: %s\n",curClient->partialData);

		curClient->dataToRead = 0;

		if(curClient->mode == CHAT) {
			handleChat(curClient);
		} else if(curClient->mode == DRAFT_REQUEST) {
			handleDraftRequest(curClient);
		} else if(curClient->mode == PING_RESPONSE) {
			handlePingResponse(curClient);
		}else if(curClient->mode == DRAFT_PASS) {
			handleDraftPass(curClient);
		} else {
			fprintf(stderr, "ERROR: Done reading data but client in invalid mode\n");
			/* Shawyoun  handleError(curClient); */
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
		handleBadExit(curClient); /* Shawyoun */
	} else if (nbytes < curClient->pwordToRead) {
		/* still more pword to read */
		curClient->pwordToRead = curClient->pwordToRead - nbytes;
		memcpy(curClient->pword, data_buffer, curClient->totalPwordExpected);
	} else {
		/* TODO Shawyoun: remove hashing from frontend */
		/* entire password read */
		memcpy(curClient->pword, data_buffer, curClient->totalPwordExpected); /* Shawyoun */
		memcpy(curClient->fullPwordToRead, data_buffer, curClient->totalPwordExpected); /* Shawyoun */
		fprintf(stderr, "Password read in: curClient->password: %s\n", curClient->pword);

		curClient->pwordToRead = 0;

		handleHello(curClient);
	}
}

void handleHello(struct clientInfo *curClient) {
	/* Shawyoun */
	fprintf(stderr, "reached handleHello. Header: %s\n Password: %s\n PasswordLength: %i\n", curClient->fullHeaderToRead, curClient->fullPwordToRead, curClient->totalPwordExpected);
	write(backEndServerSock, (char*)&curClient->fullHeaderToRead, HEADERSIZE);
	bzero(curClient->fullHeaderToRead, HEADERSIZE);
	write(backEndServerSock, (char*)&curClient->fullPwordToRead, curClient->totalPwordExpected);
	bzero(curClient->fullPwordToRead, 65);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleHello");
}

void handleListRequest(struct clientInfo *curClient) {
    /* Shawyoun */
	fprintf(stderr, "reached handleListRequest. Header: %s\n Data: %s\n PasswordLength: %i\n", curClient->fullHeaderToRead, curClient->fullDataToRead, curClient->totalPwordExpected);
	write(backEndServerSock, (char*)&curClient->fullHeaderToRead, HEADERSIZE);
	bzero(curClient->fullHeaderToRead, HEADERSIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleListRequest");
}

void handleChat(struct clientInfo *curClient) {
  	/* Shawyoun */
	fprintf(stderr, "reached handleChat. Header: %s\n Data %s\n PasswordLength: %i\n", curClient->fullHeaderToRead, curClient->fullDataToRead, curClient->totalDataExpected);
	write(backEndServerSock, (char*)&curClient->fullHeaderToRead, HEADERSIZE);
	bzero(curClient->fullHeaderToRead, HEADERSIZE);
	write(backEndServerSock, (char*)&curClient->fullDataToRead, curClient->totalDataExpected);
	bzero(curClient->fullDataToRead, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleChat");
}

void handleExit(struct clientInfo *curClient) {

	/* Shawyoun */
	fprintf(stderr, "reached handleExit. Header: %s\n Data %s\n PasswordLength: %i\n", curClient->fullHeaderToRead, curClient->fullDataToRead, curClient->totalDataExpected);
	write(backEndServerSock, (char*)&curClient->fullHeaderToRead, HEADERSIZE);
	bzero(curClient->fullHeaderToRead, HEADERSIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleExitRequest");

	/* Shawyoun if(curClient->active && curClient->validated) {
		fprintf(stderr, "exit: active and validated\n");
		int sock = curClient->sock;
		close(sock);
		FD_CLR(sock, &read_fd_set);
		FD_CLR(sock, &active_fd_set);
		curClient->active = false;
		curClient->sock = -1;
	} else { *
		if(!curClient->active) fprintf(stderr, "exit: not active\n");
		if(!curClient->validated) fprintf(stderr, "exit: not validated\n"); */
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
	/* } Shawyoun */
}

void handleBadExit(struct clientInfo *curClient) {
	fprintf(stderr, "Entered handleBadExit\n");
	struct header exitHeader;
    memset(&exitHeader, 0, sizeof(exitHeader));
    exitHeader.type = htons(BAD_EXIT);
    memcpy(exitHeader.sourceID,curClient->ID,IDLENGTH);
    memcpy(exitHeader.destID,"Server",IDLENGTH);
    exitHeader.dataLength = htonl(0);
    exitHeader.msgID = htonl(0);

    int total = sizeof(exitHeader);
    int sent = 0;
    int bytes;
    do {
        bytes = write(backEndServerSock,(char *)&exitHeader+sent,total-sent);
        if(bytes < 0) error("ERROR writing message to socket");
        if(bytes == 0) break;
        sent+=bytes;
        //fprintf(stdout,"Sent %d bytes of the exit header\n",sent);
    } while (sent < total);

	/* Shawyoun if(curClient->active && curClient->validated) {
		fprintf(stderr, "exit: active and validated\n");
		int sock = curClient->sock;
		close(sock);
		FD_CLR(sock, &read_fd_set);
		FD_CLR(sock, &active_fd_set);
		curClient->active = false;
		curClient->sock = -1;
	} else { *
		if(!curClient->active) fprintf(stderr, "exit: not active\n");
		if(!curClient->validated) fprintf(stderr, "exit: not validated\n"); */
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
	/* } Shawyoun */
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
 	/* TODO Shawyoun: implement this */   
}

/* Shawyoun: we may not need this anymore */
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
	/* Shawyoun */
	fprintf(stderr, "reached handlePlayerRequest. Header: %s\n Data %s\n PasswordLength: %i\n", curClient->fullHeaderToRead, curClient->fullDataToRead, curClient->totalDataExpected);
	write(backEndServerSock, (char*)&curClient->fullHeaderToRead, HEADERSIZE);
	bzero(curClient->fullHeaderToRead, HEADERSIZE);
	write(backEndServerSock, (char*)&curClient->fullDataToRead, curClient->totalPwordExpected);
	bzero(curClient->fullDataToRead, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handlePlayerRequest");
}

void handleDraftRequest(clientInfo *curClient) {
	/* Shawyoun */
	fprintf(stderr, "reached handleDraftRequest. Header: %s\n Data: %s\n PasswordLength: %i\n", curClient->fullHeaderToRead, curClient->fullDataToRead, curClient->totalPwordExpected);
	write(backEndServerSock, (char*)&curClient->fullHeaderToRead, HEADERSIZE);
	bzero(curClient->fullHeaderToRead, HEADERSIZE);
	write(backEndServerSock, (char*)&curClient->fullDataToRead, curClient->totalDataExpected);
	bzero(curClient->fullDataToRead, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleDraftRequest");			
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
	/* Shawyoun */
	fprintf(stderr, "reached handleStartDraft. Header: %s\n Data: %s\n DataLength %i\n", curClient->fullHeaderToRead, curClient->fullDataToRead, curClient->dataToRead);
	write(backEndServerSock, (char*)&curClient->fullHeaderToRead, HEADERSIZE);
	bzero(curClient->fullHeaderToRead, HEADERSIZE);
	fprintf(stderr, "end of handleStartDraft");
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
	/* Shawyoun */
	fprintf(stderr, "reached handlePingResponse. Header: %s\n Data: %s\n PasswordLength: %i\n", curClient->fullHeaderToRead, curClient->fullDataToRead, curClient->totalDataExpected);
	write(backEndServerSock, (char*)&curClient->fullHeaderToRead, HEADERSIZE);
	bzero(curClient->fullHeaderToRead, HEADERSIZE);
	write(backEndServerSock, (char*)&curClient->fullDataToRead, curClient->totalDataExpected);
	bzero(curClient->fullDataToRead, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handlePingResponse");
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

void readFromClientForBackEnd () {
    int i;
    struct clientInfo *curClient;
    //fprintf(stderr, "sockfd: %d\n", sockfd);
	bool foundMatchingClientID = 0; /* Shawyoun */
    /* get client address based on sockfd. Shawyoun: now get it based on currentClientID */
    for(i = 0; i < MAXCLIENTS; i++) {
		if(strcmp(currentClientIDFromBackEndServer, "") != 0 && strcmp(clients[i].ID, currentClientIDFromBackEndServer) == 0) {
			if(clients[i].active && clients[i].validReceiver) {
			    curClient = &clients[i];
				foundMatchingClientID = true;
				fprintf(stderr, "Found matching client id: %s\n", clients[i].ID);
			    break;
			} else {

			}
		}
    }
	
	/* Shawyoun: If we don't find a matching ID (because we're in a "readHeader" state), 
	malloc curClient to create a "candidate" client. Once we've read the header, we can determine if it's actually
	a new client (and thus should be inserted into entries array). */
	if(!foundMatchingClientID){
		fprintf(stderr, "Did not find matching clientID\n");
		curClient = (struct clientInfo *)malloc(sizeof(struct clientInfo)); 
		memset(curClient, 0, sizeof(struct clientInfo));
		curClient->active = true;
		curClient->validated = false;
		curClient->headerToWrite = HEADERSIZE;
	}
    /* data takes precedence over headers since headerToRead should always 
       be > 0 */
	fprintf(stderr, "About to enter the dispatching if statement in readClientForBackEnd\n");
    if(curClient->pwordToWrite > 0) { /* Shawyoun: I don't expect this to happen */
	fprintf(stderr, "curClient->pwordToWrite: %i\n", curClient->pwordToWrite);
    	readPword(curClient); /* Shawyoun: we don't want this to actually happen */
    } else if(curClient->dataToWrite > 0) {
		fprintf(stderr, "curClient->dataToWrite: %i\n", curClient->dataToWrite);
		readDataForBackEnd(curClient);
    } else if(curClient->headerToWrite > 0) {
		fprintf(stderr, "About to enter readHeaderForBackEnd\n");
		readHeaderForBackEnd(curClient);
    } else {
		error("ERROR: headerToWrite and dataToWrite not > 0");
    }
}

void readHeaderForBackEnd(struct clientInfo *curClient) {
    fprintf(stderr, "Reached readHeaderForBackEnd method\n");
    char header_buffer[HEADERSIZE];
    int nbytes;
    /* retrieve what has already been read of the header */
    memcpy(header_buffer, curClient->partialHeaderForWrite, HEADERSIZE);
    /* try to read the rest of the header */
    nbytes = read (backEndServerSock, &header_buffer[HEADERSIZE-curClient->headerToWrite],curClient->headerToWrite);

    //fprintf(stderr, "readHeader client: %s, nbytes: %d, expected readsize: %d\n",curClient->ID, nbytes,curClient->headerToRead); 

    if (nbytes <= 0) {
		/* Read error or EOF: Socket closed */
    	// TODO: Pause mode
		handleBadExit(curClient); /* Shawyoun */
    } else if (nbytes < curClient->headerToWrite) {
		/* still more to read */
		curClient->headerToWrite = curClient->headerToWrite - nbytes;
		memcpy(curClient->partialHeaderForWrite, header_buffer, HEADERSIZE);
    } else {
        /* Parse header. */
		struct header newHeader;
		memcpy((char *)&newHeader, &header_buffer[0], HEADERSIZE);
		memcpy(&curClient->fullHeaderToWrite, &header_buffer[0], HEADERSIZE);
		
		/* Shawyoun: check if header's clientID matches an existing ID. If not, I think there's a problem (most of the time )
		We also need to update the currentClientIDFromBackEndServer later */
		fprintf(stderr, "Reached beginning of new logic in readHeader\n");
		bool foundMatchingClientID = 0;
		int i;
		for(i = 0; i < MAXCLIENTS; i++) {
			if(strcmp(newHeader.destID, "") != 0 && strcmp(clients[i].ID, newHeader.destID) == 0 && clients[i].validReceiver == true) {
				foundMatchingClientID = true;
				curClient = &clients[i];
				fprintf(stderr, "Found matching ID within readHeader method\n");
				break;
			}
		}
	
		if(!foundMatchingClientID){
			fprintf(stderr, "Did not find matching ID within readHeader method. The server is referring to a client I don't recognize\n");		
		}
		/* TODO: we may have to move this to the end of the readHeaderForBackEnd method (for each "message type branch") so that we only populate it when the whole header is found to be valid */
		memcpy(currentClientIDFromBackEndServer, newHeader.destID, IDLENGTH);
		/* End Shawyoun */
		
		newHeader.type = ntohs(newHeader.type);
		newHeader.dataLength = ntohl(newHeader.dataLength);
		newHeader.msgID = ntohl(newHeader.msgID);
		curClient->headerToWrite = HEADERSIZE;

		fprintf (stderr, "Read-in header: type: %hu, sourceID: %s, destID: %s, dataLength: %u, msgID: %u\n", newHeader.type, newHeader.sourceID, newHeader.destID, newHeader.dataLength, newHeader.msgID);

		/*TODO Shawyoun: Let's check for valid Header types */
		/* next step depends on header type */
		if(newHeader.type == HELLO_ACK) {
			/*TODO Shawyoun: I think HELLO_ACK does have data now */
			/*TODO Shawyoun: using the header directly may not work because of endianess changes that we made */
			/*TODO Shawyoun: you may have to store fullHeaderToRead again like before. perhaps a "dataToWrite" and "totalDataExpectedToWrite" */
			write(curClient->sock, header_buffer, HEADERSIZE);
			curClient->modeForWrite = HELLO_ACK;
		    	curClient->dataToWrite = newHeader.dataLength;
	    		curClient->totalDataExpectedToWrite = newHeader.dataLength;
		} else if (newHeader.type == CLIENT_LIST){
			/*TODO Shawyoun : fill this out */
			write(curClient->sock, header_buffer, HEADERSIZE);
			curClient->modeForWrite = CLIENT_LIST;
		    	curClient->dataToWrite = newHeader.dataLength;
	    		curClient->totalDataExpectedToWrite = newHeader.dataLength;
		} else if (newHeader.type == PLAYER_RESPONSE){
			write(curClient->sock, header_buffer, HEADERSIZE);
			curClient->modeForWrite = PLAYER_RESPONSE;
			curClient->dataToWrite = newHeader.dataLength;
			curClient->totalDataExpectedToWrite = newHeader.dataLength;	
		}else if(newHeader.type == CHAT) {
		    
		/* We've already done the mapping to client here. curClient is the receiver. No need to look */
		write(curClient->sock, header_buffer, HEADERSIZE);
	    
		    /* store information about CHAT in clientInfo struct */
		    curClient->msgIDForWrite = newHeader.msgID;
	    	memcpy(curClient->destID, newHeader.destID, 20);

		    curClient->modeForWrite = CHAT;
		    curClient->dataToWrite = newHeader.dataLength;
	    	curClient->totalDataExpectedToWrite = newHeader.dataLength;
		} else if (newHeader.type == CANNOT_DELIVER){
			write(curClient->sock, header_buffer, HEADERSIZE);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH); /* Shawyoun: because this is the first message type that comes from server and has no data after it */
		} else if (newHeader.type == ERROR){
			write(curClient->sock, header_buffer, HEADERSIZE);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH);
		} else if (newHeader.type == PING){
			write(curClient->sock, header_buffer, HEADERSIZE);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH);
		}
		else if(newHeader.type == DRAFT_STATUS){
			write(curClient->sock, header_buffer, HEADERSIZE);
			curClient->modeForWrite = DRAFT_STATUS;
		    	curClient->dataToWrite = newHeader.dataLength;
	    		curClient->totalDataExpectedToWrite = newHeader.dataLength;
		}
		else if(newHeader.type == DRAFT_STARTING){
			write(curClient->sock, header_buffer, HEADERSIZE);
			curClient->modeForWrite = DRAFT_STARTING;
		    	curClient->dataToWrite = newHeader.dataLength;
	    		curClient->totalDataExpectedToWrite = newHeader.dataLength;
		}
		else if(newHeader.type == DRAFT_ROUND_START){
			write(curClient->sock, header_buffer, HEADERSIZE);
			curClient->modeForWrite = DRAFT_ROUND_START;
		    	curClient->dataToWrite = newHeader.dataLength;
	    		curClient->totalDataExpectedToWrite = newHeader.dataLength;
		}
		else if(newHeader.type == DRAFT_ROUND_RESULT){
			write(curClient->sock, header_buffer, HEADERSIZE);
			curClient->modeForWrite = DRAFT_ROUND_RESULT;
		    	curClient->dataToWrite = newHeader.dataLength;
	    		curClient->totalDataExpectedToWrite = newHeader.dataLength;
		}else if (newHeader.type == DRAFT_END){
			write(curClient->sock, header_buffer, HEADERSIZE);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH);
		}else {
			fprintf(stderr, "ERROR: bad header type\n");
		    /* Shawyoun handleError(curClient); */
		    return;
		}
    }
}

void readDataForBackEnd(struct clientInfo *curClient) {
    /* same logic for reading as in readHeader */
    char data_buffer[curClient->totalDataExpectedToWrite];
    int nbytes;

    memcpy(data_buffer, curClient->partialDataForWrite, curClient->totalDataExpectedToWrite);
    nbytes = read (backEndServerSock, &data_buffer[curClient->totalDataExpectedToWrite-curClient->dataToWrite],curClient->dataToWrite);

    //fprintf(stderr, "nbytes: %d, expected readsize: %d\n",nbytes,curClient->dataToRead); 

    if (nbytes <= 0) {
      /* Read error or EOF */
		handleBadExit(curClient); /* Shawyoun */
    } else if (nbytes < curClient->dataToWrite) {
	/* still more data to read */
		curClient->dataToWrite = curClient->dataToWrite - nbytes;
		memcpy(curClient->partialDataForWrite, data_buffer, curClient->totalDataExpectedToWrite);
    } else {
	/* All data has been read */
		memcpy(curClient->partialDataForWrite, data_buffer, curClient->totalDataExpectedToWrite);
		memcpy(curClient->fullDataToWrite, data_buffer, curClient->totalDataExpectedToWrite); /* Shawyoun */
		fprintf(stderr,"Message read in: curClient->partialData: %s\n",curClient->partialDataForWrite);
		/* fprintf(stderr,"Message read in: curClient->fullDataToWrite: %s\n",curClient->fullDataToWrite); Shawyoun: overwhelming terminal */

		curClient->dataToWrite = 0;
		/* TODO Shawyoun: do we need to reset the totalDataExpected too? Bernie hadn't done it in his code either */

		if(curClient->modeForWrite == HELLO_ACK){
			handleHelloACK(curClient);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH); /* Shawyoun */
		}else if(curClient->modeForWrite == CLIENT_LIST){
			handleClientList(curClient);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH); /* Shawyoun */
		}else if(curClient->modeForWrite == PLAYER_RESPONSE){
			handlePlayerResponse(curClient);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH);
		}else if(curClient->modeForWrite == CHAT) {
			handleChatForWrite(curClient);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH);
		} else if(curClient->modeForWrite == DRAFT_STATUS) {
			handleDraftStatusForWrite(curClient);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH);
		}else if(curClient->modeForWrite == DRAFT_STARTING){
			handleDraftStartingForWrite(curClient);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH);
		}else if(curClient->modeForWrite == DRAFT_ROUND_START){
			handleDraftRoundStartForWrite(curClient);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH);
		}else if(curClient->modeForWrite == DRAFT_ROUND_RESULT){
			handleDraftRoundResultForWrite(curClient);
			memset(currentClientIDFromBackEndServer, 0, IDLENGTH);
		}else {
			fprintf(stderr, "ERROR: Done reading data but client in invalid mode\n");
			/* Shawyoun handleError(curClient); */
		}
    }
}

void handleHelloACK(struct clientInfo* curClient){
	fprintf(stderr, "reached handleHelloACK, expecting %i bytes of data after header\n", curClient->totalDataExpectedToWrite);
	fprintf(stderr, "curClient->sock: %i\n", curClient->sock);
	// write(curClient->sock, (char*)&curClient->fullHeaderToWrite, HEADERSIZE);
	/* Shawyoun */
	bzero(curClient->fullHeaderToWrite, HEADERSIZE);
	write(curClient->sock, (char*)&curClient->fullDataToWrite, curClient->totalDataExpectedToWrite);
	bzero(curClient->fullDataToWrite, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleHelloACK");
}

void handleClientList(struct clientInfo* curClient){
	fprintf(stderr, "reached handleClientList, expecting %i bytes of data after header\n", curClient->totalDataExpectedToWrite);
	fprintf(stderr, "curClient->sock: %i\n", curClient->sock);
	// write(curClient->sock, (char*)&curClient->fullHeaderToWrite, HEADERSIZE);
	/* Shawyoun */
	bzero(curClient->fullHeaderToWrite, HEADERSIZE);
	write(curClient->sock, (char*)&curClient->fullDataToWrite, curClient->totalDataExpectedToWrite);
	bzero(curClient->fullDataToWrite, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleClientList");
}

void handlePlayerResponse(struct clientInfo* curClient){
	fprintf(stderr, "reached handlePlayerResponse, expecting %i bytes of data after header\n", curClient->totalDataExpectedToWrite);
	fprintf(stderr, "curClient->sock: %i\n", curClient->sock);
	// write(curClient->sock, (char*)&curClient->fullHeaderToWrite, HEADERSIZE);
	/* Shawyoun */
	bzero(curClient->fullHeaderToWrite, HEADERSIZE);
	write(curClient->sock, (char*)&curClient->fullDataToWrite, curClient->totalDataExpectedToWrite);
	bzero(curClient->fullDataToWrite, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handlePlayerResponse");	
}

void handleChatForWrite(struct clientInfo* curClient){
	fprintf(stderr, "reached handleChatForWrite, expecting %i bytes of data after header\n", curClient->totalDataExpectedToWrite); /*TODO Shawyoun: Should these "to write" variables be tied to the recipient instead? */

	fprintf(stderr, "curClient->sock: %i\n", curClient->sock);
	// write(receiver->sock, (char*)&sender->fullHeaderToWrite, HEADERSIZE);
	/* Shawyoun */
	bzero(curClient->fullHeaderToWrite, HEADERSIZE);
	write(curClient->sock, (char*)&curClient->fullDataToWrite, curClient->totalDataExpectedToWrite);
	bzero(curClient->fullDataToWrite, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleChatForWrite");
}

void handleErrorForWrite(struct clientInfo * curClient){
	/* Shawyoun: wipe the client entry out just like when an exit is incoming */
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

void handleDraftStatusForWrite(struct clientInfo *curClient){
	fprintf(stderr, "reached handleDraftStatusForWrite, expecting %i bytes of data after header\n", curClient->totalDataExpectedToWrite);
	fprintf(stderr, "curClient->sock: %i\n", curClient->sock);
	// write(curClient->sock, (char*)&curClient->fullHeaderToWrite, HEADERSIZE);
	/* Shawyoun */
	bzero(curClient->fullHeaderToWrite, HEADERSIZE);
	write(curClient->sock, (char*)&curClient->fullDataToWrite, curClient->totalDataExpectedToWrite);
	bzero(curClient->fullDataToWrite, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleDraftStatusForWrite");	
}

void handleDraftStartingForWrite(struct clientInfo *curClient){
	fprintf(stderr, "reached handleDraftStartingForWrite, expecting %i bytes of data after header\n", curClient->totalDataExpectedToWrite);
	fprintf(stderr, "curClient->sock: %i\n", curClient->sock);
	// write(curClient->sock, (char*)&curClient->fullHeaderToWrite, HEADERSIZE);
	/* Shawyoun */
	bzero(curClient->fullHeaderToWrite, HEADERSIZE);
	write(curClient->sock, (char*)&curClient->fullDataToWrite, curClient->totalDataExpectedToWrite);
	bzero(curClient->fullDataToWrite, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleDraftStartingForWrite");	
}

void handleDraftRoundStartForWrite(struct clientInfo * curClient){
	fprintf(stderr, "reached handleDraftRoundStartForWrite, expecting %i bytes of data after header\n", curClient->totalDataExpectedToWrite);
	fprintf(stderr, "curClient->sock: %i\n", curClient->sock);
	// write(curClient->sock, (char*)&curClient->fullHeaderToWrite, HEADERSIZE);
	/* Shawyoun */
	bzero(curClient->fullHeaderToWrite, HEADERSIZE);
	write(curClient->sock, (char*)&curClient->fullDataToWrite, curClient->totalDataExpectedToWrite);
	bzero(curClient->fullDataToWrite, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleDraftRoundStartForWrite");	
}

void handleDraftRoundResultForWrite(struct clientInfo * curClient){
	fprintf(stderr, "reached handleDraftRoundResultForWrite, expecting %i bytes of data after header\n", curClient->totalDataExpectedToWrite);
	fprintf(stderr, "curClient->sock: %i\n", curClient->sock);
	// write(curClient->sock, (char*)&curClient->fullHeaderToWrite, HEADERSIZE);
	/* Shawyoun */
	bzero(curClient->fullHeaderToWrite, HEADERSIZE);
	write(curClient->sock, (char*)&curClient->fullDataToWrite, curClient->totalDataExpectedToWrite);
	bzero(curClient->fullDataToWrite, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleDraftRoundResultForWrite");
}

void handleDraftPass(struct clientInfo* curClient){
	fprintf(stderr, "reached handleDraftPass. Header: %s\n Data: %s\n PasswordLength: %i\n", curClient->fullHeaderToRead, curClient->fullDataToRead, curClient->totalDataExpected);
	write(backEndServerSock, (char*)&curClient->fullHeaderToRead, HEADERSIZE);
	bzero(curClient->fullHeaderToRead, HEADERSIZE);
	write(backEndServerSock, (char*)&curClient->fullDataToRead, curClient->totalDataExpected);
	bzero(curClient->fullDataToRead, MAXDATASIZE);
	/* End Shawyoun*/
	fprintf(stderr, "end of handleDraftPass");	
}
