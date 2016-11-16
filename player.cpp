#include <stdio.h>
#include <string>
#include <fstream> 
#include <vector>
#include <sstream>
#include <iostream>
#include <math.h>
#include <iomanip>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "player.h"

using namespace std;

vector<playerInfo> readCSV(string filename) {
	//cout << "in readCSV \n";
	vector<playerInfo> playerData;
	fstream csvStream;
	char *filechars = new char[filename.length() + 1];
	strcpy(filechars, filename.c_str());

	csvStream.open(filechars);
	//cout << "opened file \n";
	string line;

	while(getline(csvStream, line)) {
		//cout << "in while \n";
		if(line.at(0) != '/') {
			playerInfo newPlayer;
			//cout << line << '\n';
			for(int i = 0; i < line.length(); i++) {
				if(line[i] == ' ') line[i] = '_';
				if(line[i] == ',') line[i] = ' ';
				if(line[i] == '"') line[i] = ' ';
			}
			//cout << line << '\n';

			stringstream ss(line);
			string temp;
			ss >> temp; newPlayer.PLAYER_ID = std::stoi(temp,0);
			ss >> newPlayer.PLAYER_NAME;
			for(int i = 0; i < strlen(newPlayer.PLAYER_NAME); i++) {
				if(newPlayer.PLAYER_NAME[i] == '_') newPlayer.PLAYER_NAME[i] = ' ';
			}
			ss >> temp; newPlayer.TEAM_ID = stoi(temp,0);
			ss >> newPlayer.TEAM_ABBREVIATION;
			ss >> temp; newPlayer.AGE = stoi(temp,0);
			ss >> temp; newPlayer.GP = stoi(temp,0);
			ss >> temp; newPlayer.W = stoi(temp,0);
			ss >> temp; newPlayer.L = stoi(temp,0);
			ss >> temp; newPlayer.W_PCT = stof(temp,0);
			ss >> temp; newPlayer.MIN = stof(temp,0);
			ss >> temp; newPlayer.OFF_RATING = stof(temp,0);
			ss >> temp; newPlayer.DEF_RATING = stof(temp,0);
			ss >> temp; newPlayer.NET_RATING = stof(temp,0);
			ss >> temp; newPlayer.AST_PCT = stof(temp,0);
			ss >> temp; newPlayer.AST_TO = stof(temp,0);
			ss >> temp; newPlayer.AST_RATIO = stof(temp,0);
			ss >> temp; newPlayer.OREB_PCT = stof(temp,0);
			ss >> temp; newPlayer.DREB_PCT = stof(temp,0);
			ss >> temp; newPlayer.REB_PCT = stof(temp,0);
			ss >> temp; newPlayer.TM_TOV_PCT = stof(temp,0);
			ss >> temp; newPlayer.EFG_PCT = stof(temp,0);
			ss >> temp; newPlayer.TS_PCT = stof(temp,0);
			ss >> temp; newPlayer.USG_PCT = stof(temp,0);
			ss >> temp; newPlayer.PACE = stof(temp,0);
			ss >> temp; newPlayer.PIE = stof(temp,0);
			ss >> temp; newPlayer.FGM = stof(temp,0);
			ss >> temp; newPlayer.FGA = stof(temp,0);
			ss >> temp; newPlayer.FGM_PG = stof(temp,0);
			ss >> temp; newPlayer.FGA_PG = stof(temp,0);
			ss >> temp; newPlayer.FG_PCT = stof(temp,0);
			ss >> temp; newPlayer.CFID = stoi(temp,0);
			ss >> newPlayer.CFPARAMS;
			strcpy(newPlayer.owner, "Server");

			//cout << "set all player attributes \n";
			playerData.push_back(newPlayer);
			//cout << "added player to playerData \n";
		}
	}
	//cout << "about to print players \n";
	//for(int i = 0; i < playerData.size(); i++) {
	//	cout << playerToString(playerData[i]);
	//	cout << '\n'; 
	//}
	return playerData;
}

string playerToString(playerInfo player) {
	stringstream ss;
	string s = player.PLAYER_NAME;	s += ", ";	s += player.TEAM_ABBREVIATION;
	s += ", Age: "; s += to_string(player.AGE);
	s += ", Wins: "; s += to_string(player.W);
	ss << fixed << setprecision(1) << player.OFF_RATING;
	s += ", Offensive rating: "; s += ss.str(); ss.str(string());
	ss << fixed << setprecision(1) << player.DEF_RATING;
	s += ", Defensive rating "; s += ss.str();
	s += ", Owner: ";  s+= player.owner;

	return s;
}

bool playerExists(vector<playerInfo> playerData, char *playerName) {
	for(int i = 0; i < playerData.size(); i++) {
		if(strcmp(playerData[i].PLAYER_NAME,playerName) == 0) {
			//fprintf(stderr, "Player %s exists!\n", playerName);
			return true;
		}
	}
//	fprintf(stderr, "Player %s does not exist!\n", playerName);
	return false;
}

bool playerDrafted(vector<playerInfo> playerData, char *playerName) {
	for(int i = 0; i < playerData.size(); i++) {
		if(strcmp(playerData[i].PLAYER_NAME,playerName) == 0) {
			if(strcmp(playerData[i].owner,"Server") == 0) {
//				fprintf(stderr, "Player %s is not yet drafted\n", playerName);
				return false;
			} else {
//				fprintf(stderr, "Player %s is drafted\n", playerName);
				return true;
			}
		}
	}
//	fprintf(stderr, "Player %s is not yet drafted\n", playerName);
	return false;
}

string vectorToAugmentedCSV(vector<playerInfo> playerData) {
	string s;
	for(int i = 0; i < playerData.size(); i++) {
		s += to_string(playerData[i].PLAYER_ID); s += ',';
		s += playerData[i].PLAYER_NAME; s+= ',';
		s += to_string(playerData[i].TEAM_ID); s+= ',';
		s += playerData[i].TEAM_ABBREVIATION; s += ',';
		s += to_string(playerData[i].AGE); s += ',';
		s += to_string(playerData[i].GP); s += ',';
		s += to_string(playerData[i].W); s += ',';
		s += to_string(playerData[i].L); s += ',';
		s += to_string(playerData[i].W_PCT); s += ',';
		s += to_string(playerData[i].MIN); s += ',';
		s += to_string(playerData[i].OFF_RATING); s += ',';
		s += to_string(playerData[i].DEF_RATING); s += ',';
		s += to_string(playerData[i].NET_RATING); s += ',';
		s += to_string(playerData[i].AST_PCT); s += ',';
		s += to_string(playerData[i].AST_TO); s += ',';
		s += to_string(playerData[i].AST_RATIO); s += ',';
		s += to_string(playerData[i].OREB_PCT); s += ',';
		s += to_string(playerData[i].DREB_PCT); s += ',';
		s += to_string(playerData[i].REB_PCT); s += ',';
		s += to_string(playerData[i].TM_TOV_PCT); s += ',';
		s += to_string(playerData[i].EFG_PCT); s += ',';
		s += to_string(playerData[i].TS_PCT); s += ',';
		s += to_string(playerData[i].USG_PCT); s += ',';
		s += to_string(playerData[i].PACE); s += ',';
		s += to_string(playerData[i].PIE); s += ',';
		s += to_string(playerData[i].FGM); s += ',';
		s += to_string(playerData[i].FGA); s += ',';
		s += to_string(playerData[i].FGM_PG); s += ',';
		s += to_string(playerData[i].FGA_PG); s += ',';
		s += to_string(playerData[i].FG_PCT); s += ',';
		s += to_string(playerData[i].CFID); s += ',';
		s += playerData[i].CFPARAMS; s += ',';
		s += playerData[i].owner;
		s += '\n';
	}

	return s;
}

vector<playerInfo> readAugmentedCSV(string augCSV) {
	//cout << "in readAugmentedCSV \n";
	vector<playerInfo> playerData;
	istringstream augCSVStream(augCSV);

	string line;

	while(getline(augCSVStream, line)) {
		//cout << "in while \n";
		if(line.at(0) != '/') {
			playerInfo newPlayer;
			//cout << line << '\n';
			for(int i = 0; i < line.length(); i++) {
				if(line[i] == ' ') line[i] = '_';
				if(line[i] == ',') line[i] = ' ';
				if(line[i] == '"') line[i] = ' ';
			}
			//cout << line << '\n';

			stringstream ss(line);
			string temp;
			ss >> temp; newPlayer.PLAYER_ID = std::stoi(temp,0);
			ss >> newPlayer.PLAYER_NAME;
			for(int i = 0; i < strlen(newPlayer.PLAYER_NAME); i++) {
				if(newPlayer.PLAYER_NAME[i] == '_') newPlayer.PLAYER_NAME[i] = ' ';
			}
			ss >> temp; newPlayer.TEAM_ID = stoi(temp,0);
			ss >> newPlayer.TEAM_ABBREVIATION;
			ss >> temp; newPlayer.AGE = stoi(temp,0);
			ss >> temp; newPlayer.GP = stoi(temp,0);
			ss >> temp; newPlayer.W = stoi(temp,0);
			ss >> temp; newPlayer.L = stoi(temp,0);
			ss >> temp; newPlayer.W_PCT = stof(temp,0);
			ss >> temp; newPlayer.MIN = stof(temp,0);
			ss >> temp; newPlayer.OFF_RATING = stof(temp,0);
			ss >> temp; newPlayer.DEF_RATING = stof(temp,0);
			ss >> temp; newPlayer.NET_RATING = stof(temp,0);
			ss >> temp; newPlayer.AST_PCT = stof(temp,0);
			ss >> temp; newPlayer.AST_TO = stof(temp,0);
			ss >> temp; newPlayer.AST_RATIO = stof(temp,0);
			ss >> temp; newPlayer.OREB_PCT = stof(temp,0);
			ss >> temp; newPlayer.DREB_PCT = stof(temp,0);
			ss >> temp; newPlayer.REB_PCT = stof(temp,0);
			ss >> temp; newPlayer.TM_TOV_PCT = stof(temp,0);
			ss >> temp; newPlayer.EFG_PCT = stof(temp,0);
			ss >> temp; newPlayer.TS_PCT = stof(temp,0);
			ss >> temp; newPlayer.USG_PCT = stof(temp,0);
			ss >> temp; newPlayer.PACE = stof(temp,0);
			ss >> temp; newPlayer.PIE = stof(temp,0);
			ss >> temp; newPlayer.FGM = stof(temp,0);
			ss >> temp; newPlayer.FGA = stof(temp,0);
			ss >> temp; newPlayer.FGM_PG = stof(temp,0);
			ss >> temp; newPlayer.FGA_PG = stof(temp,0);
			ss >> temp; newPlayer.FG_PCT = stof(temp,0);
			ss >> temp; newPlayer.CFID = stoi(temp,0);
			ss >> newPlayer.CFPARAMS;
			ss >> newPlayer.owner;
			

			//cout << "set all player attributes \n";
			playerData.push_back(newPlayer);
			//cout << "added player to playerData \n";
		}
	}
	//cout << "about to print players \n";
	//for(int i = 0; i < playerData.size(); i++) {
	//	cout << playerToString(playerData[i]);
	//	cout << '\n'; 
	//}
	return playerData;
}

