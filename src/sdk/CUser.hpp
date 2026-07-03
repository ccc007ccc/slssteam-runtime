#pragma once

#include "EResult.hpp"

#include <cstdint>

class CAppOwnershipInfo;

enum class ECallbackType : uint32_t
{
	LicensesUpdate_t = 0x7d,
	AppOwnershipTicketReceived_t = 0xf907c,
	AppLicensesChanged_t = 0xf90be
};

struct AppOwnershipTicketReceived_t
{
	EResult result;
	uint32_t appId;
};

struct AppLicensesChanged_t
{
	static constexpr unsigned int MAX_APPS_PER_CALLBACK = 0x40;

	bool reloadAll;
	bool firstLoad;
	uint8_t __pad_0x2[0x2];
	uint32_t remainingPackets;
	uint32_t count;
	uint32_t apps[MAX_APPS_PER_CALLBACK];
	uint32_t appsAdded;
};

class CUser
{
public:
	bool checkAppOwnership(uint32_t appId, CAppOwnershipInfo* pInfo);
	bool isSubscribed(uint32_t appId);

	void postCallback(ECallbackType type, void* pCallback, uint32_t callbackSize);
	void updateAppOwnershipTicket(uint32_t appId, void* pTicket, uint32_t len);
};
