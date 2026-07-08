#include "achievements.hpp"

#include "../sdk/CAPIJob.hpp"
#include "../sdk/CClientUnifiedServiceTransport.hpp"
#include "../sdk/CProtoBufMsgBase.hpp"
#include "../sdk/CSteamEngine.hpp"
#include "../sdk/CUser.hpp"

#include "../curl.hpp"
#include "../config.hpp"
#include "../log.hpp"

#include <cstdint>
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

std::unordered_set<uint64_t> Achievements::getOwnersForGame(uint32_t appId)
{
	auto list = std::unordered_set<uint64_t>();
	if (!g_config.maxSchemaTries.get())
	{
		return list;
	}

	auto url = getReviewUrl(appId);
	std::string reviews;

	if(Curl::getString(url.c_str(), reviews))
	{
		g_pLog->debug("Failed to get reviewer list for %u!\n", appId);
		return list;
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
		list.emplace(std::stoull(steamIdMatch.str().c_str()));
	}

	return list;
}

uint32_t Achievements::sendAndRecvGetPlayerStats(CClientUnifiedServiceTransport* serviceTransport, CPlayer_GetUserStats_Request* send, CPlayer_GetUserStats_Response* recv)
{
	//Don't do anything for legit apps
	if (g_pSteamEngine->getUser(0)->isSubscribed(send->appid()))
	{
		return ERESULT_NO_RESULT;
	}

	auto reviewers = getOwnersForGame(send->appid());

	send->clear_crc_stats();

	for(const auto& id : reviewers)
	{
		send->set_steamid(id);
		g_pLog->debug("CPlayer_GetUserStats_Request->set_steamid(%llu)\n", id);

		if (serviceTransport->sendAndRecvMsg(GET_PLAYER_STATS_SERVICE_NAME, send, recv) != ERESULT_OK)
		{
			continue;
		}

		//Clear stats so steam merges them for us
		recv->clear_crc_stats();
		recv->clear_stats();

		g_pLog->debug("Using stats from %llu for %u\n", id, send->appid());
		return ERESULT_OK;
	}

	g_pLog->debug("No schemas for %u found! Falling back to offline cache\n", send->appid());
	return ERESULT_NO_CONNECTION;
}

uint32_t Achievements::sendAndRecvGetUserStats(CAPIJob* job, CProtoBufMsgBase* send, uint32_t timeOut, CProtoBufMsgBase* recv, uint32_t targetType)
{
	auto sendBdy = send->getBody<CMsgClientGetUserStats>();

	if (g_pSteamEngine->getUser(0)->isSubscribed(sendBdy->game_id()))
	{
		return 0;
	}

	auto recvBdy = recv->getBody<CMsgClientGetUserStatsResponse>();
	auto owners = getOwnersForGame(sendBdy->game_id());

	sendBdy->clear_crc_stats();

	for(const auto& id : owners)
	{
		sendBdy->set_steam_id_for_user(id);
		g_pLog->debug("CMsgClientGetUserStats->set_steam_id_for_user(%llu)\n", id);

		const uint32_t ret = job->sendAndRecv(send, timeOut, recv, targetType);
		if (!ret)
		{
			continue;
		}

		if (recvBdy->eresult() != ERESULT_OK)
		{
			continue;
		}
		
		recvBdy->clear_achievement_blocks();
		recvBdy->clear_crc_stats();
		recvBdy->clear_stats();

		return ret;
	}

	g_pLog->debug("No schemas for %u found! Falling back to offline cache\n", sendBdy->game_id());
	recvBdy->set_eresult(ERESULT_NO_CONNECTION);
	return 1;
}
