#include <stdio.h>
#include <string>
#include <fstream> 
#include <vector>
#include <sstream>
#include <iostream>
#include <math.h>
#include <iomanip>

using namespace std;

struct playerInfo {
	int PLAYER_ID;
	char PLAYER_NAME[50];
	int TEAM_ID;
	char TEAM_ABBREVIATION[4];
	int AGE;
	int GP;
	int W;
	int L;
	float W_PCT;
	float MIN;
	float OFF_RATING;
	float DEF_RATING;
	float NET_RATING;
	float AST_PCT;
	float AST_TO;
	float AST_RATIO;
	float OREB_PCT;
	float DREB_PCT;
	float REB_PCT;
	float TM_TOV_PCT;
	float EFG_PCT;
	float TS_PCT;
	float USG_PCT;
	float PACE;
	float PIE;
	float FGM;
	float FGA;
	float FGM_PG;
	float FGA_PG;
	float FG_PCT;
	float CFID;
	char CFPARAMS[50];
	char owner[50];
}__attribute__((packed, aligned(1)));

vector<playerInfo> readCSV(string filename);
string playerToString(playerInfo player);
string vectorToAugmentedCSV(vector<playerInfo> playerData);
vector<playerInfo> readAugmentedCSV(string augCSV);