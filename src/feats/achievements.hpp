#pragma once

#include <cstdint>
#include <string>

class CMsgClientGetUserStats;
class CMsgClientGetUserStatsResponse;

class CProtoBufMsgBase;

class CPlayer_GetUserStats_Request;
class CPlayer_GetUserStats_Response;

namespace Achievements
{
	std::string getReviewUrl(uint32_t appId);
	uint64_t getPublicProfileForGame(uint32_t appId);

	void recvGetPlayerStatsResponse(CPlayer_GetUserStats_Response* msg);
	void recvGetUserStatsResponse(CMsgClientGetUserStatsResponse* msg);
	void recvMessage(CProtoBufMsgBase* msg);

	bool sendGetPlayerStats(CPlayer_GetUserStats_Request* msg);
	void sendGetUserStats(CMsgClientGetUserStats* msg);
	void sendMessage(CProtoBufMsgBase* msg);
}
