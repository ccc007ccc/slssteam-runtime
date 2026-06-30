#include "dlc.hpp"

#include "../sdk/CAppOwnershipInfo.hpp"
#include "../sdk/CSteamEngine.hpp"
#include "../sdk/CUser.hpp"

#include "../config.hpp"

#include "apps.hpp"
#include "fakeappid.hpp"


bool DLC::shouldUnlockDlc(uint32_t appId)
{
	if (g_config.shouldExcludeAppId(appId))
	{
		return false;
	}

	uint32_t pipeAppId = FakeAppIds::getRealAppIdForCurrentPipe();
	//Don't unlock inside the SteamClient (AppId 0)
	//Enable blacklisting whole games
	if (!pipeAppId || g_config.shouldExcludeAppId(pipeAppId, true))
	{
		return false;
	}

	if (g_pSteamEngine->getUser(0)->isSubscribed(appId))
	{
		return false;
	}
	
	return true;
}

bool DLC::checkAppOwnership(uint32_t appId, CAppOwnershipInfo *info)
{
	if (!shouldUnlockDlc(appId))
	{
		return false;
	}

	Apps::unlockApp(appId, info);

	return true;
}

bool DLC::isDlcEnabled(uint32_t appId)
{
	return shouldUnlockDlc(appId);
}

bool DLC::isAppDlcInstalled(uint32_t appId)
{
	return shouldUnlockDlc(appId);
}

bool DLC::userSubscribedInTicket(uint32_t appId)
{
	//Might want to compare the steamId param to the g_currentSteamId in the future
	//Although not doing that might also work for Dedicated servers?
	return shouldUnlockDlc(appId);
}

uint32_t DLC::getDlcCount(uint32_t appId)
{
	const auto dlcData = g_config.dlcData.get();
	if (dlcData.contains(appId))
	{
		return dlcData.at(appId).dlcIds.size();
	}

	return 0;
}

bool DLC::getDlcDataByIndex(uint32_t appId, int index, uint32_t* dlcId, bool* available, char* dlcName, size_t& dlcNameLen)
{
	if (!dlcId || !available || !dlcName)
	{
		return false;
	}

	auto dlcData = g_config.dlcData.get();
	if (dlcData.contains(appId))
	{
		auto& data = dlcData[appId];
		auto dlc = std::next(data.dlcIds.begin(), index);

		*dlcId = dlc->first;
		*available = true;

		//No clue if we have to check for errors during printf since the devs hopefully didn't fuck
		//up the dlcNameLen. Who knows though
		snprintf(dlcName, dlcNameLen, "%s", dlc->second.c_str());

		return true;
	}
	else if (!g_config.shouldExcludeAppId(*dlcId))
	{
		*available = true;
	}

	return false;
}
