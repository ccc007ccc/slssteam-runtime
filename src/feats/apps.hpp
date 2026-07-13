#pragma once

#include <cstdint>
#include <unordered_set>

class CProtoBufMsgBase;
class CMsgClientGamesPlayed;
class CMsgClientPICSProductInfoRequest;
class CMsgClientPICSProductInfoResponse;

struct AppOwnershipInfo_t;

namespace Apps
{
	extern bool applistRequested;

	bool unlockApp(uint32_t appId, AppOwnershipInfo_t* info, uint32_t ownerId);
	bool unlockApp(uint32_t appId, AppOwnershipInfo_t* info);

	bool checkAppOwnership(uint32_t appId, AppOwnershipInfo_t* info);
	void getSubscribedApps(uint32_t* appList, uint32_t size, uint32_t& count);
	void parseProductInfoFromResponse(CMsgClientPICSProductInfoResponse* msg);
	void runIPCFrame();

	void postAppLicensesChanged(const std::unordered_set<uint32_t>& apps);

	bool shouldDisableCloud(uint32_t appId);
	bool shouldDisableCDKey(uint32_t appId);
	bool shouldDisableUpdates(uint32_t appId);

	void filterLastPlayedTimes(const char* name, void* recv);

	void sendGamesPlayed(CMsgClientGamesPlayed* msg);
	void sendPICSInfoRequest(CMsgClientPICSProductInfoRequest* msg);
	void sendMsg(CProtoBufMsgBase* msg);
};
