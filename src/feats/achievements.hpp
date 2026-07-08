#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

class CAPIJob;
class CClientUnifiedServiceTransport;
class CPlayer_GetUserStats_Request;
class CPlayer_GetUserStats_Response;
class CProtoBufMsgBase;

namespace Achievements
{
	constexpr const char* GET_PLAYER_STATS_SERVICE_NAME = "Player.GetUserStats#1";

	std::string getReviewUrl(uint32_t appId);
	std::unordered_set<uint64_t> getReviewersForGame(uint32_t appId);

	//CPlayer_GetUserStats
	uint32_t sendAndRecvGetPlayerStats(CClientUnifiedServiceTransport* serviceTransport, CPlayer_GetUserStats_Request* send, CPlayer_GetUserStats_Response* recv);
	//GetUserStats
	uint32_t sendAndRecvGetUserStats(CAPIJob* job, CProtoBufMsgBase* send, const uint32_t timeOut, CProtoBufMsgBase* recv, const uint32_t targetType);
}
