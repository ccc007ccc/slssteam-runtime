#include "achievements.hpp"

#include "../sdk/CProtoBufMsgBase.hpp"
#include "../sdk/CSteamEngine.hpp"
#include "../sdk/CUser.hpp"

#include "../curl.hpp"
#include "../config.hpp"
#include "../log.hpp"

#include <cstring>
#include <regex>
#include <sstream>
#include <string>


std::string Achievements::getReviewUrl(uint32_t appId)
{
	std::stringstream url;
	url << "https://store.steampowered.com/appreviews/" << appId
		<< "?json=1&filter=recent&language=all&purchase_type=all&num_per_page="
		<< g_config.maxSchemaTries.get();

	return url.str();
}

uint64_t Achievements::getPublicProfileForGame(uint32_t appId)
{
	auto url = getReviewUrl(appId);

	std::string reviews;
	if(Curl::getString(url.c_str(), reviews))
	{
		g_pLog->debug("Failed to get reviewer list for %u!\n", appId);
		return 0;
	}

	//g_pLog->debug("Downloaded reviewers %s\n", reviews.c_str());

	std::regex steamIdFieldsRe("\"steamid\":\"[0-9]+\"");

	auto begin = std::sregex_iterator(reviews.begin(), reviews.end(), steamIdFieldsRe);
	auto end = std::sregex_iterator();

	for(auto i = begin; i != end; ++i)
	{
		std::smatch steamIdMatch = *i;
		std::string steamIdFieldStr = steamIdMatch.str();

		//g_pLog->debug("SteamId match %s\n", match.str().c_str());

		std::regex idRe("[0-9]+");
		if (!std::regex_search(steamIdFieldStr, steamIdMatch, idRe))
		{
			continue;
		}

		g_pLog->debug("Extracted SteamId %s\n", steamIdMatch.str().c_str());
		std::stringstream profileUrl;
		profileUrl << "https://steamcommunity.com/profiles/" << steamIdMatch.str();

		std::string profile;
		if (Curl::getString(profileUrl.str().c_str(), profile))
		{
			g_pLog->debug("Failed to download profile from %s!\n", profileUrl.str().c_str());
			continue;
		}

		std::regex gamesRe("a href=\"https://steamcommunity.com/.*/games/.*\"");
		std::smatch gamesLinkMatch;

		if (!std::regex_search(profile, gamesLinkMatch, gamesRe)) //Check for presence of games tab
		{
			g_pLog->debug("Game list is private! Skipping\n");
			continue;
		}

		return std::stoull(steamIdMatch.str().c_str());
	}

	return 0;
}

void Achievements::recvGetPlayerStatsResponse(CPlayer_GetUserStats_Response* msg)
{
	msg->clear_crc_stats();
	msg->clear_stats();
}

void Achievements::recvGetUserStatsResponse(CMsgClientGetUserStatsResponse *msg)
{
	//Use offline cache when request fails for any reason
	if (msg->eresult() != ERESULT_OK)
	{
		msg->set_eresult(ERESULT_NO_CONNECTION);
		return;
	}

	if (g_pSteamEngine->getUser(0)->isSubscribed(msg->game_id()))
	{
		return;
	}

	msg->clear_achievement_blocks();
	msg->clear_crc_stats();
	msg->clear_stats(); //Clear stats so Steam merges the ones from disk for us
}

void Achievements::recvMessage(CProtoBufMsgBase* msg)
{
	switch(msg->type)
	{
		case EMSG_REQUEST_USERSTATS_RESPONSE:
			recvGetUserStatsResponse(msg->getBody<CMsgClientGetUserStatsResponse>());
			break;
	}
}

bool Achievements::sendGetPlayerStats(CPlayer_GetUserStats_Request* msg)
{
	if (!g_config.maxSchemaTries.get())
	{
		return false;
	}

	if (g_pSteamEngine->getUser(0)->isSubscribed(msg->appid()))
	{
		return false;
	}

	uint64_t ownerId = getPublicProfileForGame(msg->appid());
	if (!ownerId)
	{
		g_pLog->debug("No owner for %u found! Achievements won't be available\n", msg->appid());
		return false;
	}

	msg->clear_crc_stats();
	msg->set_steamid(ownerId);
	g_pLog->debug("CPlayer_GetUserStats_Request->set_steamid(%llu)\n", ownerId);
	return true;
}

void Achievements::sendGetUserStats(CMsgClientGetUserStats* msg)
{
	if (!g_config.maxSchemaTries.get())
	{
		return;
	}

	if (g_pSteamEngine->getUser(0)->isSubscribed(msg->game_id()))
	{
		return;
	}

	uint64_t ownerId = getPublicProfileForGame(msg->game_id());
	if (!ownerId)
	{
		g_pLog->debug("No owner for %u found! Achievements won't be available\n", msg->game_id());
		return;
	}

	msg->clear_crc_stats();
	msg->set_steam_id_for_user(ownerId);
	g_pLog->debug("CMsgClientGetUserStats->set_steam_id_for_user(%llu)\n", ownerId);
}

void Achievements::sendMessage(CProtoBufMsgBase* msg)
{
	switch(msg->type)
	{
		case EMSG_REQUEST_USERSTATS:
			sendGetUserStats(msg->getBody<CMsgClientGetUserStats>());
			break;
	}
}
