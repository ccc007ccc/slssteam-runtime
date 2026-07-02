#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

class CAppOwnershipInfo;
class CProtoBufMsgBase;
class CMsgClientGamesPlayed;
class CMsgClientPICSProductInfoRequest;

struct AppStateInfo_t;

namespace Apps
{
	extern bool applistRequested;
	extern std::map<uint32_t, int> appIdOwnerOverride;

	bool unlockApp(uint32_t appId, CAppOwnershipInfo* info, uint32_t ownerId);
	bool unlockApp(uint32_t appId, CAppOwnershipInfo* info);

	bool checkAppOwnership(uint32_t appId, CAppOwnershipInfo* info);
	void getSubscribedApps(uint32_t* appList, uint32_t size, uint32_t& count);
	void getAppStateInfo(uint32_t appId, AppStateInfo_t* info);

	bool shouldDisableCloud(uint32_t appId);
	bool shouldDisableCDKey(uint32_t appId);
	bool shouldDisableUpdates(uint32_t appId);

	void sendGamesPlayed(CMsgClientGamesPlayed* msg);
	void sendPICSInfoRequest(CMsgClientPICSProductInfoRequest* msg);
	void sendMsg(CProtoBufMsgBase* msg);
};
