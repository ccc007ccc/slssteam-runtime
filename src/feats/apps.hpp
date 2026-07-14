#pragma once

#include <cstdint>
#include <unordered_set>

class CProtoBufMsgBase;
class CMsgClientGamesPlayed;
class CMsgClientPICSProductInfoRequest;
class CMsgClientPICSProductInfoResponse;
class CPlayer_GetLastPlayedTimes_Response;

template<typename T> class CUtlVector;

struct AppOwnershipInfo_t;
struct DepotInfo_t;

namespace Apps
{
	extern bool applistRequested;

	bool unlockApp(uint32_t appId, AppOwnershipInfo_t* info, uint32_t ownerId);
	bool unlockApp(uint32_t appId, AppOwnershipInfo_t* info);

	void buildDepotDependency(uint32_t appId, CUtlVector<DepotInfo_t>* depots, CUtlVector<DepotInfo_t>* sharedDepots);
	bool checkAppOwnership(uint32_t appId, AppOwnershipInfo_t* info);
	void getSubscribedApps(uint32_t* appList, uint32_t size, uint32_t& count);
	void parseProductInfoFromResponse(CMsgClientPICSProductInfoResponse* msg);
	void runIPCFrame();

	void postAppLicensesChanged(const std::unordered_set<uint32_t>& apps);

	bool shouldDisableCloud(uint32_t appId);
	bool shouldDisableCDKey(uint32_t appId);
	bool shouldDisableUpdates(uint32_t appId);

	void sendAndRecvLastPlayedTimes(const char* name, CPlayer_GetLastPlayedTimes_Response* recv);
	void sendGamesPlayed(CMsgClientGamesPlayed* msg);
	void sendPICSInfoRequest(CMsgClientPICSProductInfoRequest* msg);
	void sendMsg(CProtoBufMsgBase* msg);
};
