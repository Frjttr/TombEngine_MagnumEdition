#pragma once
#include "../Global/global.h"

enum LARA_MESH_MASK
{
	LARA_ONLY_LEGS = (0 << 1),
	LARA_ONLY_ARMS = (1 << 1),
	LARA_ONLY_TORSO = (2 << 1),
	LARA_ONLY_HEAD = (4 << 1),
	LARA_ONLY_LEFT_ARM = (8 << 1),
	LARA_ONLY_RIGHT_ARM = (16 << 1),
	LARA_LEGS_TORSO_HEAD = (32 << 1),
	LARA_LEGS_TORSO_HEAD_ARMS = (64 << 1)
};

short GF(short animIndex, short frameToStart); // for lara
short GF2(short objectID, short animIndex, short frameToStart); // for entity

void GetRoomList(short roomNumber, short* roomArray, short* numRooms); // return the value via roomArray and numRooms
void GetRoomList(short roomNumber, vector<short>* DestRoomList);  // return the value via DestRoomList