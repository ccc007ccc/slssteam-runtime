#pragma once

#include <cstdint>
#include <map>
#include <string>

class CMsgClientGetAppOwnershipTicketResponse;
class CMsgClientRequestEncryptedAppTicketResponse;
class CProtoBufMsgBase;

namespace Ticket
{
	enum class AppTicketSource
	{
		CacheOnly,
		ForgeOnly,
		CacheThenForge,
	};

	class AppOwnershipTicket
	{
public:
		std::string data;
		uint32_t totalSize = 0;
		uint32_t appIdOffset = 16;
		uint32_t steamIdOffset = 8;
		uint32_t signatureOffset = 0;
		uint32_t signatureSize = 128;
		uint32_t steamId = 0;
	};

	class SavedTicket
	{
public:
		uint32_t steamId = 0;
		std::string ticket;
	};

	extern std::map<uint32_t, SavedTicket> ticketMap;
	extern std::map<uint32_t, SavedTicket> encryptedTicketMap;

	std::string getTicketDir();

	std::string getTicketPath(uint32_t appId);
	SavedTicket getCachedTicket(uint32_t appId);
	bool saveTicketToCache(CMsgClientGetAppOwnershipTicketResponse* resp);
	bool getAppOwnershipTicket(uint32_t appId, AppOwnershipTicket& ticket, AppTicketSource source);
	uint32_t getSpoofSteamId(uint32_t appId);
	bool isManagedTicketApp(uint32_t appId);
	bool armSteamIdSpoof(uint32_t appId, uint32_t steamId);
	uint32_t consumeSteamIdSpoof(uint32_t appId);

	void launchApp(uint32_t appId);
	void getTicketOwnershipExtendedData(uint32_t appId);

	std::string getEncryptedTicketPath(uint32_t appId);
	SavedTicket getCachedEncryptedTicket(uint32_t appId);
	bool saveEncryptedTicketToCache(CMsgClientRequestEncryptedAppTicketResponse* resp);
	std::string getConfiguredEncryptedTicket(uint32_t appId);
	std::string getEncryptedTicketData(uint32_t appId);
	void recordEncryptedTicketCall(uint64_t call, uint32_t appId);
	bool getEncryptedTicketCallResult(uint64_t call, void* callback, uint32_t callbackSize, uint32_t expectedCallback, bool* failed);

	void recvEncryptedAppTicket(CMsgClientRequestEncryptedAppTicketResponse* msg);
	void recvAppTicket(CMsgClientGetAppOwnershipTicketResponse* msg);
	void recvMsg(CProtoBufMsgBase* msg);
}
