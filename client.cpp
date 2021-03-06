// Bernie Birnbaum and Shawyoun Saidon
// Comp 112 Final Project
// Tufts University

// This whole section with includes, defines, and struct definitions would
// ideally be in a seperate header file
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <netdb.h>
#include <string>
#include <fstream> 
#include <vector>
#include <sstream>
#include <iostream>
#include <math.h>
#include <iomanip>
#include "player.h"

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

#define IDLENGTH 20
#define PARTICIPATING_MASK 256
#define ROUNDLENGTH_MASK 255

struct header {
    unsigned short type;
    char sourceID[IDLENGTH];
    char destID[IDLENGTH];
    int length;
    int msgID;
}__attribute__((packed, aligned(1)));

void error(const char *msg) {
    perror(msg);
    exit(1);
}

int min(int a, int b) {
    if(a < b) return a;
    else return b;
}

// 1. Highest level send and recieve methods
void sendMessage();
void readMessage();

// 2. Methods for sending particular messages
void sendHello();
void sendPlayerRequest(bool quiet);
void sendStartDraft();
void sendExit();
void replyPing(int pingID);

// 3. Methods for retrieving data to print or write
void showDrafted(bool checkServer);
void showTeam(char *owner);
void outputResults(int draftNum);

int sockfd, sleepTime, roundLength;
int myMsgID = 0;
bool connected;
bool draftJustStarted = false;

vector<playerInfo> playerData;

char username[IDLENGTH];
char pword[IDLENGTH];
char serv[IDLENGTH] = "Server";

bool lastReadWasPing = false;
bool draftInProgress = false;
bool particpatingInDraft = false;
bool exiting = false; // Global so error message can trigger exit

bool printMessage = true;

int curDraftIndex;
bool requestQuietly;
fd_set active_fd_set, read_fd_set;


/********************************************************************
 *                      Section 0: Main method                      *
 ********************************************************************/
int main(int argc, char *argv[]) {
    if(argc != 3) {
    fprintf(stdout, "Usage: ./client <host> <port>\n");
    return 1;
    }
    fprintf(stdout, "Welcome to Bernie and Shawyoun's final project!\n\n");

    char *host = argv[1];
    int port = atoi(argv[2]);
    char message[1024];

    struct hostent *server;
    struct sockaddr_in serv_addr;

    char response [450];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) error("ERROR opening socket");

    server = gethostbyname(host);
    if(server == NULL) error("ERROR, no such host");

    memset(&serv_addr,0,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr,server->h_addr,server->h_length);

    if(connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) error("ERROR connecting");
    connected = true;
    fprintf(stdout, "Connected to the server!\n");
    
    FD_ZERO(&active_fd_set);
    FD_SET(sockfd,&active_fd_set);
    FD_SET(1,&active_fd_set);

    fprintf(stdout, "What is your username? \n");
    char ch; int i = 0;
    while((read(0,&ch,1) > 0) && (i < 19) ) {
        if(ch == 10) break;
        username[i] = ch;
        i++;
    }
    username[19] = '\0';
    fprintf(stdout, "Your username is %s \n\n", username);
    fprintf(stdout, "What is your password?\n");
    i = 0;
    while((read(0,&ch,1) > 0) && (i < 19)) {
        if(ch == 10) break;
        pword[i] = ch;
        i++;
    }
    pword[19] = '\0';
    fprintf(stdout, "Your password is %s\n\n", pword);

    fprintf(stdout, "How many ms of latency do you want to add to reads and writes?\nNote: More than 2500 may make your user experience subpar\n");
    char buffer[8];
    i = 0;
    while(read(0,&ch,1) > 0){
        if(ch == 10) break;
        buffer[i] = (ch - 48);
        i++;
    }

    sleepTime = 0;
    for(int j = 0; j < i; j++) {
        sleepTime += (buffer[i-j-1] * pow(10,j));
    }

    fprintf(stdout, "Adding %d ms latency on reads and writes\n\n", sleepTime);
    sleepTime = sleepTime * 1000; // *1000 for nanoseconds to millisecons

    sendHello();


    while(!exiting) {
        char ch = '\0'; int i = 0; int message = -1;
        if(printMessage) {
            fprintf(stdout, "Type 0 to send a message, 1 to see all drafted players, 2 to toggle draft readiness, 3 to log out, or 4 to quit permanently\n\n");
        }

        read_fd_set = active_fd_set;

        if(select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0) {
            error("ERROR on select");
        }

        for(int i = 0; i < FD_SETSIZE; i++) {
            if(FD_ISSET(i, &read_fd_set)) {
                if(i == 1) { // iput on stdin
                    while(read(0,&ch,1) > 0) {
                       if(ch == 10) {
                           break;
                        } else {
                            message = ch - 48;
                        } 
                    }

                    printMessage = true;

                    if(message == 0) {
                        sendMessage();
                    } else if (message == 1) {
                        showDrafted(true);
                    } else if (message == 2) {
                        sendStartDraft();
                    } else if (message == 3) {
                        //sendExit(); // We're not going to formally quit
                        exiting = true;
                        break;
                    } else if (message == 4) {
                        sendExit();
                        exiting = true;
                        break;
                    }
                } else {
                    // input from server
                    if(connected) {
                        printMessage = false;
                        readMessage();
                    } else {
                        FD_CLR(sockfd,&read_fd_set);
                        FD_CLR(sockfd,&active_fd_set);
                    }
                }
            }
        }

    }

    fprintf(stdout, "Signed off! Have a nice day\n");
    close(sockfd);

    return 0;
}


/********************************************************************
 *      Section 1: Highest level send (write) and read methods      *
 ********************************************************************/
void sendMessage() {
    // We're gonna build this message piece by piece
    struct header headerToSend;
    char ch; int i = 0;
    char buffer[1028]; char intBuffer[sizeof(int)];
    memset(&headerToSend, 0, sizeof(headerToSend));
    memset(buffer,0,sizeof(buffer));
    fprintf(stdout, "What to send now?\n0 to get list of clients, 1 to send chat, 2 to get current player info, 3 to draft a player \n");
    while(read(0,&ch,1) > 0){
        if(ch == 10) break;
        buffer[i] = (ch - 48);
        i++;
    }
    short type = 0;
    for(int j = 0; j < i; j++) {
        type += (buffer[i-j-1] * pow(10,j));
    }

    //memcpy(&headerToSend.type,type, sizeof(short));
    fprintf(stdout, "You selected type %hu\n",type);
    if(type > 4) {
        fprintf(stdout, "This is not a valid type; aborting message send\n");
        return;
    }

    if(type == 0) {
        headerToSend.type = LIST_REQUEST;
    } else if(type == 1) {
        headerToSend.type = CHAT;
    } else if(type == 2) {
        sendPlayerRequest(false);
        return;
    } else if(type == 3) {
        headerToSend.type = DRAFT_REQUEST;
    }

    memcpy(headerToSend.sourceID,username,strlen(username));
    
    i = 0;
    memset(buffer,0,sizeof(buffer));
    if(headerToSend.type == CHAT) {
        fprintf(stdout, "What is the destID?\n");
        while(read(0,&ch,1) > 0) {
            if(ch == 10) break;
            buffer[i] = ch;
            i++;
        }
    } else {
        strcpy(buffer, "Server");
        i = 7;
    }
    memcpy(headerToSend.destID,buffer,i);

    memset(buffer,0,sizeof(buffer));
    if(headerToSend.type == CHAT) {
        headerToSend.msgID = ++myMsgID;
    } else {
        headerToSend.msgID = 0;
    }

    i = 0;
    memset(buffer,0,sizeof(buffer));
    if(headerToSend.type == CHAT) {
        fprintf(stdout, "What is your message? If no message, press enter.\n");
        while(read(0,&ch,1) > 0) {
            if(ch == 10) break;
            buffer[i] = ch;
            i++;
        }
        buffer[i] = '\0';
        i++;
    } else if(headerToSend.type == DRAFT_REQUEST) {
        if(connected) sendPlayerRequest(true);
        fprintf(stdout, "Which player would you like to draft?\n");
        while(read(0,&ch,1) > 0) {
            if(ch == 10) break;
            buffer[i] = ch;
            i++;
        }
        buffer[i] = '\0';
        i++;

        // If we can resolve this without contact the server, that would be ideal
        if((!playerExists(playerData,buffer) || playerDrafted(playerData,buffer))) {
            fprintf(stdout, "The player you have selected does not exist is or is already drafted.\n");
            return;
        }
    }
    
    headerToSend.type = htons(headerToSend.type);
    headerToSend.length = htonl(i);
    headerToSend.msgID = htonl(headerToSend.msgID);
    int total = sizeof(headerToSend);
    int sent = 0;
    int bytes;

    usleep(sleepTime); // Simulating latency

    do {
        bytes = write(sockfd,(char *)&headerToSend+sent,total-sent);
        if(bytes < 0) error("ERROR writing message to socket");
        if(bytes == 0) break;
        sent+=bytes;
       //fprintf(stdout,"Sent %d bytes of the header\n",sent);
    } while (sent < total);

    if(i > 1) {
        total = i;
        sent = 0;
        while(sent < total) {
            bytes = write(sockfd, buffer+sent, total-sent);
            if(bytes < 0) error("ERROR writing message to socket");
            if(bytes == 0) break;
            sent+= bytes;
            //fprintf(stdout,"Sent %d bytes of the message body\n")
        }
    }

    fprintf(stdout, "Sent!\n");
    if(type == LIST_REQUEST || type == PLAYER_REQUEST) {
        // These types want responses ASAP
        lastReadWasPing = true;
        while(lastReadWasPing && connected) readMessage();
    }
    if(type == DRAFT_REQUEST) {
        showDrafted(true);
    }
}

void readMessage() {
    char headerBuffer[sizeof(header)];
    int total = sizeof(header);
    int received = 0;
    int bytes;

    memset(headerBuffer,0,sizeof(headerBuffer));
    while(received < total) {
        bytes = read(sockfd,headerBuffer+received,total-received);
        if(bytes < 0) error("ERROR reading header from socket\n");
        if(bytes == 0) {
            fprintf(stdout, "Server disconnected!\n");
            connected = false;
            break;
        }
        received+=bytes;
    }
    if(!connected) return;

    struct header headerToRead;
    memcpy((char *)&headerToRead, &headerBuffer[0], sizeof(headerToRead));
    headerToRead.type = ntohs(headerToRead.type);
    headerToRead.length = ntohl(headerToRead.length);
    headerToRead.msgID = ntohl(headerToRead.msgID);

    usleep(sleepTime); // Simulate latency

    //fprintf(stdout,"Header Recieved: type: %hu, sourceID: %s, destID: %s, length: %d, msgID: %d\n",headerToRead.type,headerToRead.sourceID,headerToRead.destID,headerToRead.length,headerToRead.msgID);

    // Handle each type specially, here in the readMessage method
    // TODO: Make these into seperate methods
    if(headerToRead.type == ERROR) {
        fprintf(stderr, "Error recieved, please sign in again\n");
        connected = false;
        exiting = true;
        return;
    }

    if(headerToRead.type == PING) {
        lastReadWasPing = true;
        replyPing(headerToRead.msgID);
        return;
    } else {
        lastReadWasPing = false;
    }

    if(headerToRead.type == DRAFT_END) {
        fprintf(stderr, "Draft is over!\n");
        draftInProgress = false;
        roundLength = 0;
        particpatingInDraft = false;
        
        outputResults(headerToRead.msgID);
        printMessage = true;
    }
    
    if(headerToRead.type == CANNOT_DELIVER) {
        fprintf(stdout, "The server could not deliver your message!\n");
        printMessage = true;
    }

    // The remaining message types involve data written
    if(headerToRead.length > 0) {
        char dataBuffer[headerToRead.length+1];
        memset(dataBuffer,0,sizeof(dataBuffer));
        total = headerToRead.length;
        received = 0;
        while(received < total) {
            bytes = read(sockfd,dataBuffer+received,total-received);
            if(bytes < 0) error("ERROR reading data from socket\n");
            if(bytes == 0) break;
            received+=bytes;
            // fprintf(stderr, "Recieved %d bytes of the data body\n", received);
        }
        
        if(headerToRead.type == CHAT) {
            for(int i = 0; i < headerToRead.length; i++) {
                if(dataBuffer[i] == 0) dataBuffer[i] = 32;
            }
            fprintf(stdout,"Message from %s:\n%s\n\n",headerToRead.sourceID,dataBuffer);
            if(strcmp(serv,headerToRead.sourceID) != 0) printMessage = true;
        }

        if(headerToRead.type == HELLO_ACK) {
            if(headerToRead.msgID == 1) {
                draftInProgress = true;
            } else {
                draftInProgress = false;
            }
            fprintf(stdout, "%s\n", dataBuffer);
        }

        if(headerToRead.type == DRAFT_STATUS) {
            fprintf(stdout, "%s\n", dataBuffer);
            printMessage = true;
        }

        if(headerToRead.type == CLIENT_LIST) {
            for(int i = 0; i < headerToRead.length; i++) {
                if(dataBuffer[i] == '\0') dataBuffer[i] = ' ';
            }
            fprintf(stdout,"Current clients:\n%s\n\n",dataBuffer);
            printMessage = true;
        }

        if(headerToRead.type == PLAYER_RESPONSE) {
            playerData = readAugmentedCSV(dataBuffer);
            if(!requestQuietly) {
                // Proof that we got something; printing it all would be overwhelming
                fprintf(stdout, "Here is partial current player data:\n");
                for(int i = 0; i < playerData.size(); i++) {
                    if(i%50 == 0)
                    cout << playerToString(playerData[i]) << '\n';
                }
                cout << "\n\n";
            }
            printMessage = true;
        }

        if(headerToRead.type == DRAFT_STARTING) {
            draftInProgress = true;
            draftJustStarted = true;

            if((PARTICIPATING_MASK & headerToRead.msgID) != 0) particpatingInDraft = true;
            roundLength = (ROUNDLENGTH_MASK & headerToRead.msgID);
            fprintf(stdout, "%s\n", dataBuffer);
            if(particpatingInDraft) fprintf(stdout, "Each round will last %d seconds. Don't get left behind!\n", roundLength);
            sendPlayerRequest(true);
        }

        if(headerToRead.type == DRAFT_ROUND_START && draftInProgress) {
            if(headerToRead.msgID == 1) {
                // First round of the draft so wipe all previous ownership data
                for(int i = 0; i < playerData.size(); i++) {
                    memset(playerData[i].owner,0,IDLENGTH);
                    strcpy(playerData[i].owner,serv);
                }
            }

            if(playerData.size() == 0) {
                // Something weird happened, but we'll sort it out now
                fprintf(stdout, "Waiting for player data from server...you may miss round %d\n", headerToRead.msgID);
                sendPlayerRequest(true);
                lastReadWasPing = true;
                while(lastReadWasPing && connected) readMessage();
            }

            fprintf(stdout, "Draft round %d:\nPlayer to draft: %s\n", headerToRead.msgID, dataBuffer);

            for(int i = 0; i < playerData.size(); i++) {
                if(strcmp(playerData[i].PLAYER_NAME, dataBuffer) == 0) {
                    curDraftIndex = i;
                    break;
                }
            }

            if(!particpatingInDraft) return;
            // Some context for those actually participating:
            fprintf(stdout, "Stats: Team: %s, Age: %d, FG PCT: %3.1f, O-Rating: %4.1f, D-Rating: %4.1f, Min/G: %3.1f\n", 
                playerData[curDraftIndex].TEAM_ABBREVIATION,playerData[curDraftIndex].AGE,playerData[curDraftIndex].FG_PCT*100,playerData[curDraftIndex].OFF_RATING,playerData[curDraftIndex].DEF_RATING,playerData[curDraftIndex].MIN);
            fprintf(stdout, "1 to attempt to claim! Anything else to pass\n");
            fd_set stdin_set;

            bool madeClaim = false;

            FD_ZERO(&stdin_set);
            FD_SET(1,&stdin_set);

            timeval timeout;
            timeout.tv_sec = roundLength;
            timeout.tv_usec = 0;

            // Client gets a window of exactly roundLength to respond
            // Poll would probably be better here?
            if(select(FD_SETSIZE, &stdin_set, NULL, NULL, &timeout) < 0) {
                error("ERROR on select");
            }

            for(int i = 0; i < FD_SETSIZE; i++) {
                if(FD_ISSET(i, &stdin_set)) {
                    if(i == 1) { // iput on stdin

                        char ch; int input;
                        while(read(0,&ch,1) > 0) {
                                   if(ch == 10) {
                                       break;
                                    } else {
                                        input = ch - 48;
                                    } 
                        }

                        madeClaim = true;

                        struct header draftResponse;
                        memset(&draftResponse,0,sizeof(draftResponse));
                        if(input == 1) {
                            draftResponse.type = htons(DRAFT_REQUEST);
                        } else {
                            draftResponse.type = htons(DRAFT_PASS);
                        }
                        memcpy(draftResponse.sourceID,username,strlen(username));
                        memcpy(draftResponse.destID,serv,strlen(serv));
                        draftResponse.length = htonl(headerToRead.length);
                        draftResponse.msgID = htonl(headerToRead.msgID);

                        int total = sizeof(draftResponse);
                        int sent = 0;
                        int bytes;
                        usleep(sleepTime); // Simulate that latency
                        do {
                            bytes = write(sockfd,(char *)&draftResponse+sent,total-sent);
                            if(bytes < 0) error("ERROR writing message to socket");
                            if(bytes == 0) break;
                            sent+=bytes;
                        } while (sent < total);

                        total = headerToRead.length;
                        sent = 0;
                        while(sent < total) {
                            bytes = write(sockfd, dataBuffer+sent, total-sent);
                            if(bytes < 0) error("ERROR writing password to socket");
                            if(bytes == 0) break;
                            sent+= bytes;
                        }
                    }
                }
            }
            if(madeClaim) {
                fprintf(stdout, "Please wait while other players respond\n\n");
            } else {
                fprintf(stdout, "Your opportunity to claim %s has passed!\n\n", dataBuffer);
            }
        }

        if(headerToRead.type == DRAFT_ROUND_RESULT) {
            if(strlen(dataBuffer) == 0) {
                fprintf(stdout, "No one won round %d of the draft\n\n", headerToRead.msgID+1);
            } else {
                fprintf(stdout, "%s won round %d of the draft\n", dataBuffer, headerToRead.msgID+1);
                memcpy(playerData[curDraftIndex].owner, dataBuffer, headerToRead.length+1);
                showTeam(dataBuffer);
            }
            fprintf(stdout, "The next round will start when all participants are done sending chats, etc.\n");
            printMessage = true;
        }
    }
}


/********************************************************************
 *              Section 2: Send methods of specific types           *
 ********************************************************************/

/* Sends of specific types
   All are fairly self explanatory
   Should probably have one of these for every type */
void sendHello() {
    struct header helloHeader;
    memset(&helloHeader, 0, sizeof(helloHeader));
    helloHeader.type = htons(HELLO);
    memcpy(helloHeader.sourceID,username,strlen(username));
    memcpy(helloHeader.destID,serv,strlen(serv));
    helloHeader.length = htonl(strlen(pword) + 1);
    helloHeader.msgID = htonl(0);

    int total = sizeof(helloHeader);
    int sent = 0;
    int bytes;
    usleep(sleepTime); // Simulate latency
    do {
        bytes = write(sockfd,(char *)&helloHeader+sent,total-sent);
        if(bytes < 0) error("ERROR writing message to socket");
        if(bytes == 0) break;
        sent+=bytes;
        //fprintf(stdout,"Sent %d bytes of the hello header\n",sent);
    } while (sent < total);

    total = strlen(pword) + 1;
    sent = 0;
    while(sent < total) {
        bytes = write(sockfd, pword+sent, total-sent);
        if(bytes < 0) error("ERROR writing password to socket");
        if(bytes == 0) break;
        sent+= bytes;
        //fprintf(stdout,"Sent %d bytes of the password\n");
    }

    lastReadWasPing = true;
    while(lastReadWasPing && connected) readMessage();

    lastReadWasPing = true;
    while(lastReadWasPing && connected) readMessage();

    // sendPlayerRequest(true); // 
}

void sendPlayerRequest(bool quiet) {
    struct header playerReqHeader;
    memset(&playerReqHeader, 0, sizeof(playerReqHeader));
    playerReqHeader.type = htons(PLAYER_REQUEST);
    memcpy(playerReqHeader.sourceID,username,strlen(username));
    memcpy(playerReqHeader.destID,serv,strlen(serv));
    playerReqHeader.length = htonl(0);
    playerReqHeader.msgID = htonl(0);

    int total = sizeof(playerReqHeader);
    int sent = 0;
    int bytes;
    usleep(sleepTime); // Simulate latency
    do {
        bytes = write(sockfd,(char *)&playerReqHeader+sent,total-sent);
        if(bytes < 0) error("ERROR writing message to socket");
        if(bytes == 0) break;
        sent+=bytes;
        //fprintf(stdout,"Sent %d bytes of the hello header\n",sent);
    } while (sent < total);

    requestQuietly = quiet;
    lastReadWasPing = true;
    while(lastReadWasPing && connected) {
        readMessage();
    }
}

void sendStartDraft() {
    struct header startDraftHeader;
    memset(&startDraftHeader, 0, sizeof(startDraftHeader));
    startDraftHeader.type = htons(START_DRAFT);
    memcpy(startDraftHeader.sourceID,username,strlen(username));
    memcpy(startDraftHeader.destID,serv,strlen(serv));
    startDraftHeader.length = htonl(0);
    startDraftHeader.msgID = htonl(0);

    int total = sizeof(startDraftHeader);
    int sent = 0;
    int bytes;
    usleep(sleepTime); // Simulate latency
    do {
        bytes = write(sockfd,(char *)&startDraftHeader+sent,total-sent);
        if(bytes < 0) error("ERROR writing message to socket");
        if(bytes == 0) break;
        sent+=bytes;
        //fprintf(stdout,"Sent %d bytes of the exit header\n",sent);
    } while (sent < total);
    //fprintf(stderr, "Sent sendStartDraft header\n");

    lastReadWasPing = true;
    while(lastReadWasPing && connected) readMessage();

    if(playerData.size() == 0) sendPlayerRequest(true);
}

void sendExit() {
    struct header exitHeader;
    memset(&exitHeader, 0, sizeof(exitHeader));
    exitHeader.type = htons(EXIT);
    memcpy(exitHeader.sourceID,username,strlen(username));
    memcpy(exitHeader.destID,serv,strlen(serv));
    exitHeader.length = htonl(0);
    exitHeader.msgID = htonl(0);

    int total = sizeof(exitHeader);
    int sent = 0;
    int bytes;
    usleep(sleepTime); // Simulate latency
    do {
        bytes = write(sockfd,(char *)&exitHeader+sent,total-sent);
        if(bytes < 0) error("ERROR writing message to socket");
        if(bytes == 0) break;
        sent+=bytes;
        //fprintf(stdout,"Sent %d bytes of the exit header\n",sent);
    } while (sent < total);
}

void replyPing(int pingID) {

    struct timeval curTime;
    char timeBuffer[sizeof(curTime)];
    struct header pingReplyHeader;
    memset(&pingReplyHeader, 0, sizeof(pingReplyHeader));

    // Not actually doing anything with this
    gettimeofday(&curTime,NULL);
    memcpy(timeBuffer,&curTime,sizeof(curTime));

    pingReplyHeader.type = htons(PING_RESPONSE);
    memcpy(pingReplyHeader.sourceID,username,strlen(username));
    memcpy(pingReplyHeader.destID,serv,strlen(serv));
    pingReplyHeader.length = htonl(sizeof(curTime));
    pingReplyHeader.msgID = htonl(pingID);

    int total = sizeof(pingReplyHeader);
    int sent = 0;
    int bytes;

    usleep(sleepTime); // Simulate latency (this time really matters!)

    do {
        bytes = write(sockfd,(char *)&pingReplyHeader+sent,total-sent);
        if(bytes < 0) error("ERROR writing message to socket");
        if(bytes == 0) break;
        sent+=bytes;
       //fprintf(stdout,"Sent %d bytes of the header\n",sent);
    } while (sent < total);

    // This time stamp is not actually used, but hey
    total = sizeof(curTime);
    sent = 0;
    while(sent < total) {
        bytes = write(sockfd, timeBuffer+sent, total-sent);
        if(bytes < 0) error("ERROR writing message to socket");
        if(bytes == 0) break;
        sent+= bytes;
        //fprintf(stdout,"Sent %d bytes of the message body\n")
    }
}


/********************************************************************
 *          Section 3: Methods for printing or saving data          *
 ********************************************************************/
void showDrafted(bool checkServer) {
    cout << "\nAll drafted players: \n";
    if(connected && checkServer) sendPlayerRequest(true); // Don't bother if the server isn't there

    for(int i = 0; i < playerData.size(); i++) {
        if(strcmp(playerData[i].owner, "Server") != 0) {
            // Should probably add QUITTER as one to ignore as well
            cout << playerToString(playerData[i]) << '\n';
        }
    }
    cout << '\n';
}

void showTeam(char *owner) {
    int playerNumber = 1;
    cout << "\nTeam " << owner << ":\n";
    for(int i = 0; i < playerData.size(); i++) {
        if(strcmp(playerData[i].owner, owner) == 0) {
            cout << playerNumber << ". ";
            if(i == curDraftIndex) cout << '*';
            cout << playerData[i].PLAYER_NAME;
            if(i == curDraftIndex) cout << '*';
            cout << '\n';
            playerNumber++;
        }
    }
    cout << '\n';
}

void outputResults(int draftNum) {
    vector<string> teamNames;
    vector<vector<playerInfo>> teams;
    for(int i = 0; i < playerData.size(); i++) {
        if((strcmp(playerData[i].owner,serv) != 0) && (strcmp(playerData[i].owner,"QUITTER") != 0)) {
            bool isNew = true;
            int j;
            for(j = 0; j < teamNames.size(); j++) {
                if(strcmp(teamNames[j].c_str(),playerData[i].owner) == 0) {
                    isNew = false;
                    break;
                }
            }
            if(isNew) {
                teamNames.push_back(playerData[i].owner);
                vector<playerInfo> newPlayerVector;
                teams.push_back(newPlayerVector);
            }
            teams[j].push_back(playerData[i]);
        }
    }

    string filename = "draftresults_" + to_string(draftNum) + ".txt";
    ofstream outputFile(filename);

    outputFile << "Draft " << to_string(draftNum) << " results:\n\n";
    for(int i = 0; i < teamNames.size(); i++) {
        outputFile << "Team " << teamNames[i] << '\n';
        for(int j = 0; j < teams[i].size(); j++) {
            outputFile << to_string(j + 1) << ". " << teams[i][j].PLAYER_NAME << '\n';
        }

        outputFile << '\n';
    }

    cout << "Draft results saved to " << filename << "\n";
}