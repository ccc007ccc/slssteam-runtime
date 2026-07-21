#include "ticket.hpp"

#include "fakeappid.hpp"

#include "../config.hpp"
#include "../globals.hpp"

#include "../sdk/CProtoBufMsgBase.hpp"
#include "../sdk/CSteamEngine.hpp"
#include "../sdk/CUser.hpp"
#include "../sdk/EResult.hpp"
#include "../sdk/IClientUtils.hpp"

#include "base64/base64.hpp"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/emittermanip.h"

#include <filesystem>
#include <fstream>
#include <ios>
#include <cstring>
#include <chrono>
#include <limits>
#include <mutex>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace
{
	constexpr uint32_t appTicketSteamIdOffset = 8;
	constexpr uint32_t appTicketAppIdOffset = 16;
	constexpr uint32_t appTicketSignatureSize = 128;
	constexpr uint32_t localAppTicketSourceAppId = 7;
	constexpr uint32_t encryptedAppTicketCallback = 154;
	constexpr auto authorizationLifetime = std::chrono::seconds(30);

	// OpenSteamTool keys authorization by Windows process and CPipeClient. SLSsteam
	// runs inside one Linux Steam process, so HSteamPipe plus a launch generation is
	// the equivalent isolation boundary available at this layer.
	enum class AuthorizationStage
	{
		Authorizing,
		EndAuthorization,
	};

	struct AuthorizedIdentity
	{
		uint32_t appId = 0;
		uint32_t steamId = 0;
		uint64_t launchGeneration = 0;
		AuthorizationStage stage = AuthorizationStage::Authorizing;
		std::chrono::steady_clock::time_point armedAt {};
	};

	std::mutex authorizationMutex;
	std::unordered_map<uint32_t, AuthorizedIdentity> authorizedIdentities;
	std::unordered_map<uint32_t, uint64_t> launchGenerations;
	std::unordered_map<uint64_t, uint32_t> pendingEncryptedTicketCalls;

	uint32_t currentPipe()
	{
		if (!g_pClientUtils) return 0;
		const uint32_t* pipe = g_pClientUtils->getPipeIndex();
		return pipe ? *pipe : 0;
	}

	int hexNibble(char value)
	{
		if (value >= '0' && value <= '9') return value - '0';
		if (value >= 'a' && value <= 'f') return value - 'a' + 10;
		if (value >= 'A' && value <= 'F') return value - 'A' + 10;
		return -1;
	}

	std::string decodeHexTicket(uint32_t appId, const std::string& name, const std::string& hex)
	{
		if (hex.empty()) return {};
		if (hex.size() % 2 != 0)
		{
			g_pLog->debug("Ignoring %s for %u: hexadecimal string has odd length\n", name.c_str(), appId);
			return {};
		}

		std::string bytes(hex.size() / 2, '\0');
		for (size_t i = 0; i < bytes.size(); i++)
		{
			const int high = hexNibble(hex[i * 2]);
			const int low = hexNibble(hex[i * 2 + 1]);
			if (high < 0 || low < 0)
			{
				g_pLog->debug("Ignoring %s for %u: invalid hexadecimal character\n", name.c_str(), appId);
				return {};
			}
			bytes[i] = static_cast<char>((high << 4) | low);
		}
		return bytes;
	}

	uint32_t extractSteamAccountId(const std::string& ticket)
	{
		if (ticket.size() < appTicketSteamIdOffset + sizeof(uint64_t)) return 0;
		uint64_t steamId = 0;
		std::memcpy(&steamId, ticket.data() + appTicketSteamIdOffset, sizeof(steamId));
		return static_cast<uint32_t>(steamId & std::numeric_limits<uint32_t>::max());
	}

	std::string getConfiguredAppTicket(uint32_t appId)
	{
		const auto tickets = g_config.appTickets.get();
		const auto it = tickets.find(appId);
		if (it == tickets.end()) return {};
		return decodeHexTicket(appId, "AppTickets", it->second);
	}

	bool writeCacheFile(const std::string& path, const YAML::Emitter& node)
	{
		const std::string temporaryPath = path + ".tmp";
		std::ofstream ofs(temporaryPath, std::ios::out | std::ios::binary | std::ios::trunc);
		if (!ofs)
		{
			g_pLog->debug("Unable to open ticket cache file %s\n", temporaryPath.c_str());
			return false;
		}

		ofs.write(node.c_str(), node.size());
		ofs.close();
		if (!ofs)
		{
			g_pLog->debug("Unable to write ticket cache file %s\n", temporaryPath.c_str());
			std::error_code removeError;
			std::filesystem::remove(temporaryPath, removeError);
			return false;
		}

		std::error_code renameError;
		std::filesystem::rename(temporaryPath, path, renameError);
		if (renameError)
		{
			g_pLog->debug("Unable to replace ticket cache file %s: %s\n", path.c_str(), renameError.message().c_str());
			std::error_code removeError;
			std::filesystem::remove(temporaryPath, removeError);
			return false;
		}

		return true;
	}
}

std::map<uint32_t, Ticket::SavedTicket> Ticket::ticketMap = std::map<uint32_t, SavedTicket>();
std::map<uint32_t, Ticket::SavedTicket> Ticket::encryptedTicketMap = std::map<uint32_t, SavedTicket>();

std::string Ticket::getTicketDir()
{
	std::stringstream ss;
	ss << g_config.getDir().c_str() << "/cache";

	const auto dir = ss.str();
	std::error_code error;
	if (!std::filesystem::exists(dir, error))
	{
		std::filesystem::create_directories(dir, error);
		if (error)
		{
			g_pLog->debug("Unable to create ticket cache directory %s: %s\n", dir.c_str(), error.message().c_str());
		}
	}

	return ss.str();
}

std::string Ticket::getTicketPath(uint32_t appId)
{
	std::stringstream ss;
	ss << getTicketDir().c_str() << "/ticket_" << appId << ".yaml";

	return ss.str();
}

Ticket::SavedTicket Ticket::getCachedTicket(uint32_t appId)
{
	if (const std::string configured = getConfiguredAppTicket(appId); !configured.empty())
	{
		SavedTicket ticket {};
		ticket.steamId = extractSteamAccountId(configured);
		ticket.ticket = configured;
		return ticket;
	}

	if (ticketMap.contains(appId))
	{
		return ticketMap[appId];
	}

	SavedTicket ticket {};

	const auto path = getTicketPath(appId);
	if (!std::filesystem::exists(path.c_str()))
	{
		return ticket;
	}

	g_pLog->debug("Reading ticket for %u\n", appId);
	try
	{
		auto node = YAML::LoadFile(path);
		ticket.steamId = node["steamId"].as<uint32_t>();
		ticket.ticket = base64::from_base64(node["ticket"].as<std::string>());
	}
	catch (const std::exception& error)
	{
		g_pLog->debug("Unable to read ticket cache %s: %s\n", path.c_str(), error.what());
		return {};
	}
	//g_pLog->debug("Ticket: %u, %s\n", ticket.steamId, ticket.ticket.c_str());

	ticketMap[appId] = ticket;

	return ticket;
}

bool Ticket::isManagedTicketApp(uint32_t appId)
{
	if (g_config.isAddedAppId(appId)) return true;
	return g_config.injectedDepots.get().contains(appId);
}

uint32_t Ticket::getSpoofSteamId(uint32_t appId)
{
	if (!isManagedTicketApp(appId)) return 0;
	const SavedTicket ticket = getCachedTicket(appId);
	if (const uint32_t embedded = extractSteamAccountId(ticket.ticket); embedded) return embedded;
	return ticket.steamId;
}

bool Ticket::armSteamIdSpoof(uint32_t appId, uint32_t steamId)
{
	const uint32_t pipe = currentPipe();
	if (!pipe || !appId || !steamId) return false;
	std::lock_guard lock(authorizationMutex);
	const uint64_t generation = launchGenerations[appId];
	const auto existing = authorizedIdentities.find(pipe);
	if (g_config.getDenuvoGameOwner(appId) && existing != authorizedIdentities.end() &&
		existing->second.appId == appId && existing->second.launchGeneration == generation &&
		existing->second.stage == AuthorizationStage::EndAuthorization)
	{
		g_pLog->debug("Ticket authorization window already ended for AppId %u on pipe %u\n", appId, pipe);
		return false;
	}

	authorizedIdentities[pipe] = {
		appId,
		steamId,
		generation,
		AuthorizationStage::Authorizing,
		std::chrono::steady_clock::now(),
	};
	g_pLog->debug("Started ticket authorization for AppId %u on pipe %u\n", appId, pipe);
	return true;
}

uint32_t Ticket::consumeSteamIdSpoof(uint32_t appId)
{
	const uint32_t pipe = currentPipe();
	if (!pipe) return 0;
	std::lock_guard lock(authorizationMutex);
	const auto it = authorizedIdentities.find(pipe);
	if (it == authorizedIdentities.end()) return 0;
	if (appId && it->second.appId != appId) return 0;
	if (it->second.launchGeneration != launchGenerations[it->second.appId] ||
		it->second.stage != AuthorizationStage::Authorizing ||
		std::chrono::steady_clock::now() - it->second.armedAt > authorizationLifetime)
	{
		authorizedIdentities.erase(it);
		return 0;
	}
	const uint32_t steamId = it->second.steamId;
	it->second.steamId = 0;
	it->second.stage = AuthorizationStage::EndAuthorization;
	g_pLog->debug("Ended ticket authorization for AppId %u on pipe %u\n", appId, pipe);
	return steamId;
}

bool Ticket::getAppOwnershipTicket(uint32_t appId, AppOwnershipTicket& ticket, AppTicketSource source)
{
	ticket = {};
	if (!isManagedTicketApp(appId)) return false;

	if (source == AppTicketSource::CacheOnly || source == AppTicketSource::CacheThenForge)
	{
		const SavedTicket cached = getCachedTicket(appId);
		if (cached.ticket.size() >= sizeof(uint32_t))
		{
			uint32_t signatureOffset = 0;
			std::memcpy(&signatureOffset, cached.ticket.data(), sizeof(signatureOffset));
			if (signatureOffset <= cached.ticket.size() &&
				cached.ticket.size() - signatureOffset >= appTicketSignatureSize)
			{
				ticket.data = cached.ticket;
				ticket.totalSize = static_cast<uint32_t>(ticket.data.size());
				ticket.appIdOffset = appTicketAppIdOffset;
				ticket.steamIdOffset = appTicketSteamIdOffset;
				ticket.signatureOffset = signatureOffset;
				ticket.signatureSize = appTicketSignatureSize;
				ticket.steamId = extractSteamAccountId(ticket.data);
				return true;
			}
			g_pLog->debug("Ignoring malformed AppTicket for %u\n", appId);
		}
	}

	if (source == AppTicketSource::CacheOnly) return false;

	const SavedTicket sourceTicket = getCachedTicket(localAppTicketSourceAppId);
	if (sourceTicket.ticket.size() <= appTicketSignatureSize) return false;
	const size_t signedSize = sourceTicket.ticket.size() - appTicketSignatureSize;
	if (sourceTicket.ticket.size() > std::numeric_limits<uint32_t>::max() - sizeof(appId)) return false;

	ticket.data.reserve(sourceTicket.ticket.size() + sizeof(appId));
	ticket.data.append(sourceTicket.ticket.data(), signedSize);
	ticket.data.append(reinterpret_cast<const char*>(&appId), sizeof(appId));
	ticket.data.append(sourceTicket.ticket.data() + signedSize, appTicketSignatureSize);
	ticket.totalSize = static_cast<uint32_t>(sourceTicket.ticket.size());
	ticket.appIdOffset = ticket.totalSize - appTicketSignatureSize;
	ticket.steamIdOffset = appTicketSteamIdOffset;
	ticket.signatureOffset = ticket.appIdOffset + sizeof(appId);
	ticket.signatureSize = appTicketSignatureSize;
	ticket.steamId = extractSteamAccountId(ticket.data);
	g_pLog->debug("Using local AppTicket source %u for %u\n", localAppTicketSourceAppId, appId);
	return true;
}

bool Ticket::saveTicketToCache(CMsgClientGetAppOwnershipTicketResponse* resp)
{
	const uint32_t appId = resp->app_id();

	g_pLog->debug("Saving ticket for %u...\n", appId);

	auto bytes = resp->ticket();

	YAML::Emitter node;
	node << YAML::BeginMap;
	node << YAML::Key << "steamId";
	node << YAML::Value << g_currentSteamId;
	node << YAML::Key << "ticket";
	node << YAML::Value << base64::to_base64(bytes);
	node << YAML::EndMap;

	const auto path = Ticket::getTicketPath(appId);
	if (!writeCacheFile(path, node))
	{
		return false;
	}

	g_pLog->once("Saved ticket for %u\n", appId);

	//TODO: Skip copy
	SavedTicket ticket {};
	ticket.steamId = g_currentSteamId;
	ticket.ticket = bytes;
	ticketMap[appId] = ticket;
	
	return true;
}

void Ticket::launchApp(uint32_t appId)
{
	{
		std::lock_guard lock(authorizationMutex);
		launchGenerations[appId]++;
		for (auto it = authorizedIdentities.begin(); it != authorizedIdentities.end(); )
		{
			if (it->second.appId == appId) it = authorizedIdentities.erase(it);
			else ++it;
		}
	}

	auto ticket = getCachedTicket(appId);
	if (!ticket.ticket.size())
	{
		return;
	}

	g_pSteamEngine->getUser(0)->updateAppOwnershipTicket(appId, reinterpret_cast<void*>(ticket.ticket.data()), ticket.ticket.size());
	g_pLog->once("Force loaded AppOwnershipTicket for %i\n", appId);
}

void Ticket::getTicketOwnershipExtendedData(uint32_t appId)
{
	const uint32_t steamId = getSpoofSteamId(appId);
	if (!steamId)
	{
		return;
	}

	armSteamIdSpoof(appId, steamId);
}

std::string Ticket::getConfiguredEncryptedTicket(uint32_t appId)
{
	if (!isManagedTicketApp(appId)) return {};
	const auto tickets = g_config.encryptedAppTickets.get();
	const auto it = tickets.find(appId);
	if (it == tickets.end()) return {};
	return decodeHexTicket(appId, "EncryptedAppTickets", it->second);
}

std::string Ticket::getEncryptedTicketData(uint32_t appId)
{
	if (const std::string configured = getConfiguredEncryptedTicket(appId); !configured.empty()) return configured;
	const SavedTicket cached = getCachedEncryptedTicket(appId);
	if (cached.ticket.empty()) return {};
	CMsgClientRequestEncryptedAppTicketResponse response;
	if (!response.ParseFromString(cached.ticket) || response.app_id() != appId ||
		!response.has_encrypted_app_ticket()) return {};
	return response.encrypted_app_ticket().encrypted_ticket();
}

void Ticket::recordEncryptedTicketCall(uint64_t call, uint32_t appId)
{
	if (!call || !appId || getEncryptedTicketData(appId).empty()) return;
	std::lock_guard lock(authorizationMutex);
	pendingEncryptedTicketCalls[call] = appId;
	g_pLog->debug("Recorded EncryptedAppTicket call %llu for %u\n", call, appId);
}

bool Ticket::getEncryptedTicketCallResult(uint64_t call, void* callback, uint32_t callbackSize, uint32_t expectedCallback, bool* failed)
{
	std::lock_guard lock(authorizationMutex);
	const auto it = pendingEncryptedTicketCalls.find(call);
	if (it == pendingEncryptedTicketCalls.end()) return false;
	if (!callback || callbackSize < sizeof(int32_t) || expectedCallback != encryptedAppTicketCallback)
	{
		return false;
	}
	const int32_t result = ERESULT_OK;
	std::memcpy(callback, &result, sizeof(result));
	if (failed) *failed = false;
	pendingEncryptedTicketCalls.erase(it);
	return true;
}

std::string Ticket::getEncryptedTicketPath(uint32_t appId)
{
	std::stringstream ss;
	ss << getTicketDir().c_str() << "/encryptedTicket_" << appId << ".yaml";

	return ss.str();
}

Ticket::SavedTicket Ticket::getCachedEncryptedTicket(uint32_t appId)
{
	const uint32_t realAppId = FakeAppIds::getRealAppIdForCurrentPipe();
	const uint32_t fakeAppId = FakeAppIds::getFakeAppId(realAppId);

	SavedTicket ticket {};

	if (realAppId && fakeAppId && appId != realAppId)
	{
		g_pLog->once("Returning empty cached encrypted ticket for %u because it's set to %u\n", realAppId, fakeAppId);
		return ticket;
	}

	if (encryptedTicketMap.contains(appId))
	{
		return encryptedTicketMap[appId];
	}

	const auto path = getEncryptedTicketPath(appId);
	if (!std::filesystem::exists(path.c_str()))
	{
		return ticket;
	}

	g_pLog->debug("Reading encrypted ticket for %u\n", appId);
	try
	{
		auto node = YAML::LoadFile(path);
		ticket.steamId = node["steamId"].as<uint32_t>();
		ticket.ticket = base64::from_base64(node["encryptedTicket"].as<std::string>());
	}
	catch (const std::exception& error)
	{
		g_pLog->debug("Unable to read encrypted ticket cache %s: %s\n", path.c_str(), error.what());
		return {};
	}
	//g_pLog->debug("Ticket: %u, %s\n", ticket.steamId, ticket.ticket.c_str());

	encryptedTicketMap[appId] = ticket;

	return ticket;
}

bool Ticket::saveEncryptedTicketToCache(CMsgClientRequestEncryptedAppTicketResponse* resp)
{
	const uint32_t appId = resp->app_id();

	g_pLog->debug("Saving encrypted ticket for %u...\n", appId);

	auto bytes = resp->SerializeAsString();

	YAML::Emitter node;
	node << YAML::BeginMap;
	node << YAML::Key << "steamId";
	node << YAML::Value << g_currentSteamId;
	node << YAML::Key << "encryptedTicket";
	//node << YAML::Value << YAML::EncodeBase64(reinterpret_cast<const unsigned char*>(bytes.c_str()), bytes.size());
	node << YAML::Value << base64::to_base64(bytes);
	node << YAML::EndMap;

	const auto path = getEncryptedTicketPath(appId);
	if (!writeCacheFile(path, node))
	{
		return false;
	}

	g_pLog->once("Saved encrypted ticket for %u\n", appId);

	//TODO: Skip copy
	SavedTicket ticket {};
	ticket.steamId = g_currentSteamId;
	ticket.ticket = bytes;
	encryptedTicketMap[appId] = ticket;
	
	return true;
}

void Ticket::recvEncryptedAppTicket(CMsgClientRequestEncryptedAppTicketResponse* msg)
{
	if (msg->eresult() == ERESULT_OK)
	{
		saveEncryptedTicketToCache(msg);
		return;
	}

	if (const std::string configured = getConfiguredEncryptedTicket(msg->app_id()); !configured.empty())
	{
		const uint32_t appId = msg->app_id();
		msg->Clear();
		msg->set_app_id(appId);
		msg->set_eresult(ERESULT_OK);
		msg->mutable_encrypted_app_ticket()->set_encrypted_ticket(configured);
		g_pLog->debug("Using configured EncryptedAppTicket for %u\n", appId);
		return;
	}

	SavedTicket ticket = getCachedEncryptedTicket(msg->app_id());
	if(!ticket.steamId)
	{
		return;
	}

	CMsgClientRequestEncryptedAppTicketResponse cachedResponse;
	if (!cachedResponse.ParseFromString(ticket.ticket) || cachedResponse.app_id() != msg->app_id())
	{
		g_pLog->debug("Ignoring invalid encrypted ticket cache for %u\n", msg->app_id());
		return;
	}

	msg->CopyFrom(cachedResponse);
	g_pLog->debug("Using encryptedTicket_%u from disk\n", msg->app_id());
}

void Ticket::recvAppTicket(CMsgClientGetAppOwnershipTicketResponse* msg)
{
	if(msg->eresult() == ERESULT_OK)
	{
		saveTicketToCache(msg);
		return;
	}

	//We do not load tickets from disk in the network layer, otherwise they won't be loaded in offline mode
}

void Ticket::recvMsg(CProtoBufMsgBase* msg)
{
	switch(msg->type)
	{
		case EMSG_APPOWNERSHIPTICKET_RESPONSE:
			recvAppTicket(msg->getBody<CMsgClientGetAppOwnershipTicketResponse>());
			break;

		case EMSG_ENCRYPTED_APPTICKET_RESPONSE:
			recvEncryptedAppTicket(msg->getBody<CMsgClientRequestEncryptedAppTicketResponse>());
			break;
	}
}
