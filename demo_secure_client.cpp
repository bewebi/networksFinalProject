#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
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
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "demo_ssl_utils.h"

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

struct header {
    short type;
    char sourceID[20];
    char destID[20];
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
void sendMessage();
void readMessage();

void sendHello();
void sendPlayerRequest(bool quiet);
void sendExit();

void showDrafted();

int sockfd, sleepTime;
int myMsgID = 0;
bool connected;

vector<playerInfo> playerData;
char username[20];
char pword[20];
char serv[20] = "Server";

bool requestQuietly;

SSL_CTX * ctx;
SSL * ssl;

int main(int argc, char *argv[]) {
    if(argc != 4 && argc != 5) {
	fprintf(stdout, "Usage: ./client hostname <port> <sslmethod> [trust]\nFor sslmethod: 1 = TLS 1, 2 = TLS 1.1, 3 = TLS 1.2. If no value is entered, the highest possible version is negotiated.\nFor trust, just enter the word trust to accept self-signed certificates.\n");
	return 1;
    }
    fprintf(stdout, "Welcome to Bernie and Shawyoun's final project!\n\n");

    char *host = argv[1];
    int port = atoi(argv[2]);
    char message[1024];
    //strcpy(message, argv[3]);

    struct hostent *server;
    struct sockaddr_in serv_addr;
    //int sockfd, bytes, sent, recieved, total;
    char response [450];
	
	ctx = newSSLContext(atoi(argv[3]));
	if(argc != 5 || strcmp("trust", argv[4]) != 0)
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) error("ERROR opening socket");

    server = gethostbyname(host);
    if(server == NULL) error("ERROR, no such host");

    memset(&serv_addr,0,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr,server->h_addr,server->h_length);

    if(connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) error("ERROR connecting");
	ssl = SSL_new(ctx);	
	SSL_set_fd(ssl, sockfd);
	if ( SSL_connect(ssl) != 1){			/* perform the connection */
        	ERR_print_errors_fp(stderr);
		exit(1);
	}
    connected = true;
    fprintf(stdout, "Connected to the server!\n");
   
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
    sendHello();

    while(1) {
    	char ch = '\0'; int i = 0; int message = -1;
    	fprintf(stdout, "Type 0 to send a message, 1 to wait for a message from another player, 2 to see all drafted players, or 3 to quit\n");
    	while(read(0,&ch,1) > 0) {
	       if(ch == 10) {
	           break;
    	    } else {
        	   	message = ch - 48;
	        } 
    	}
    	if(message == 0) {
	        sendMessage();
	    } else if (message == 1) {  
    	    readMessage();
    	} else if (message == 2) {
            showDrafted();
        } else if (message == 3) {
    	    sendExit();
            break;
    	}
    }
    fprintf(stdout, "Quitting! Have a nice day\n");
	// SSL_free(ssl);
    close(sockfd);
	SSL_CTX_free(ctx);
    return 0;
}

void sendMessage() {
    struct header headerToSend;
    char ch; int i = 0;
    char buffer[1028]; char intBuffer[sizeof(int)];
    memset(&headerToSend, 0, sizeof(headerToSend));
    memset(buffer,0,sizeof(buffer));
    fprintf(stdout, "What to send now?\n3 = LIST_REQUEST, 5 = CHAT, 6 = EXIT, 9 = PLAYER_REQUEST, 11 = DRAFT_REQUEST \n");
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
    if(type != 3 && type != 5 && type != 6 && type != 9 && type != 11) {
        fprintf(stdout, "This is not a valid type; aborting message send\n");
        return;
    }

    if(type == PLAYER_REQUEST) {
        sendPlayerRequest(false);
        return;
    }
    headerToSend.type = type;

    i = 0;

    memcpy(headerToSend.sourceID,username,strlen(username));
    //fprintf(stdout, "sourceID is %s\n",headerToSend.sourceID);
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
    //fprintf(stdout, "destID is %s\n",headerToSend.destID);

    i = 0;
    memset(buffer,0,sizeof(buffer));
    if(headerToSend.type == CHAT) {
        headerToSend.msgID = ++myMsgID;
    } else {
        headerToSend.msgID = 0;
    }
    //fprintf(stdout, "Your message has msgID %d\n",headerToSend.msgID);


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
        if(i > 1) fprintf(stdout, "Your message of size %d is as follows: %s\n\n",i,buffer);
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
    do {
    	bytes = SSL_write(ssl,(char *)&headerToSend+sent,total-sent);
	ERR_print_errors_fp(stderr);
    	if(bytes < 0) error("ERROR writing message to socket");
    	if(bytes == 0) break;
    	sent+=bytes;
	   //fprintf(stdout,"Sent %d bytes of the header\n",sent);
    } while (sent < total);

    if(i > 1) {
    	total = i;
    	sent = 0;
	   while(sent < total) {
	        bytes = SSL_write(ssl, buffer+sent, total-sent);
    	    if(bytes < 0) error("ERROR writing message to socket");
	        if(bytes == 0) break;
    	    sent+= bytes;
	        //fprintf(stdout,"Sent %d bytes of the message body\n")
    	}
    }

    if(type == LIST_REQUEST || type == PLAYER_REQUEST) {
        readMessage();
    }
    if(type == DRAFT_REQUEST) {
        showDrafted();
    }
}

void readMessage() {
    char headerBuffer[50];
    int total = 50;
    int received = 0;
    int bytes;

    memset(headerBuffer,0,sizeof(headerBuffer));
    while(received < total) {
	bytes = SSL_read(ssl,headerBuffer+received,total-received);
	if(bytes < 0) error("ERROR reading header from socket\n");
	if(bytes == 0) break;
	received+=bytes;
    }

    struct header headerToRead;
    memcpy((char *)&headerToRead, &headerBuffer[0], 50);
    headerToRead.type = ntohs(headerToRead.type);
    headerToRead.length = ntohl(headerToRead.length);
    headerToRead.msgID = ntohl(headerToRead.msgID);

//    fprintf(stdout,"Header Recieved: type: %hu, sourceID: %s, destID: %s, length: %d, msgID: %d\n",headerToRead.type,headerToRead.sourceID,headerToRead.destID,headerToRead.length,headerToRead.msgID);
	if(headerToRead.type == ERROR) {
        fprintf(stderr, "Error recieved, please sign in again\n");
    }

    if(headerToRead.length > 0) {
	    char dataBuffer[headerToRead.length+1];
    	memset(dataBuffer,0,sizeof(dataBuffer));
    	total = headerToRead.length;
    	received = 0;
    	while(received < total) {
    	    bytes = SSL_read(ssl,dataBuffer+received,total-received);
    	    if(bytes < 0) error("ERROR reading data from socket\n");
    	    if(bytes == 0) break;
    	    received+=bytes;
//            fprintf(stderr, "Recieved %d bytes of the data body\n", received);
	    }

        if(headerToRead.type != PLAYER_RESPONSE) {
        	for(int i = 0; i < headerToRead.length; i++) {
        	    if(dataBuffer[i] == 0) dataBuffer[i] = 32;
	        }
        	fprintf(stdout,"Message from %s:\n%s\n",headerToRead.sourceID,dataBuffer);
        } else {
            //fprintf(stdout, "augCSV: \n %s", dataBuffer);
            playerData = readAugmentedCSV(dataBuffer);
            if(!requestQuietly) {
                fprintf(stdout, "Here is partial current player data:\n");
                for(int i = 0; i < playerData.size(); i++) {
                    if(i%50 == 0)
                    cout << playerToString(playerData[i]) << '\n';
                }
                cout << '\n';
            }
        }
    }
}

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
    do {
        bytes = SSL_write(ssl,(char *)&helloHeader+sent,total-sent);
        if(bytes < 0) error("ERROR writing message to socket");
        if(bytes == 0) break;
        sent+=bytes;
        //fprintf(stdout,"Sent %d bytes of the hello header\n",sent);
    } while (sent < total);

    total = strlen(pword) + 1;
    sent = 0;
    while(sent < total) {
        bytes = SSL_write(ssl, pword+sent, total-sent);
        if(bytes < 0) error("ERROR writing password to socket");
        if(bytes == 0) break;
        sent+= bytes;
        //fprintf(stdout,"Sent %d bytes of the password\n");
    }

    readMessage();
    readMessage();

    sendPlayerRequest(true);
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
    do {
        bytes = SSL_write(ssl,(char *)&exitHeader+sent,total-sent);
        if(bytes < 0) error("ERROR writing message to socket");
        if(bytes == 0) break;
        sent+=bytes;
        //fprintf(stdout,"Sent %d bytes of the exit header\n",sent);
    } while (sent < total);
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
    do {
        bytes = SSL_write(ssl,(char *)&playerReqHeader+sent,total-sent);
        if(bytes < 0) error("ERROR writing message to socket");
        if(bytes == 0) break;
        sent+=bytes;
        //fprintf(stdout,"Sent %d bytes of the hello header\n",sent);
    } while (sent < total);

    requestQuietly = quiet;
    readMessage();
}

void showDrafted() {
    cout << "\nAll drafted players: \n";
    if(connected) sendPlayerRequest(true);
    for(int i = 0; i < playerData.size(); i++) {
        if(strcmp(playerData[i].owner, "Server") != 0) {
            cout << playerToString(playerData[i]) << '\n';
        }
    }
    cout << '\n';
}
