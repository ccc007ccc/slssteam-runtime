#include "content.hpp"

#include "../config.hpp"
#include "../curl.hpp"
#include "../log.hpp"
#include "../sdk/CProtoBufMsgBase.hpp"
#include "../sdk/EResult.hpp"
#include "../sdk/protobufs/content_manifest.pb.h"

#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr const char* MANIFEST_JOB = "ContentServerDirectory.GetManifestRequestCode#1";
	constexpr auto PENDING_MAX_AGE = std::chrono::seconds(60);
	constexpr size_t MAX_PENDING_JOBS = 256;

	struct PendingDepotKey
	{
		uint32_t depotId;
		std::chrono::steady_clock::time_point created;
	};

	struct PendingManifestCode
	{
		std::shared_future<uint64_t> future;
		std::chrono::steady_clock::time_point created;
	};

	std::mutex stateMutex;
	std::unordered_map<uint64_t, PendingDepotKey> pendingDepotKeys;
	std::unordered_map<uint64_t, PendingManifestCode> pendingManifestCodes;

	bool isManagedDepot(uint32_t depotId)
	{
		if (g_config.depotKeys.get().contains(depotId))
		{
			return true;
		}
		for (const auto& [appId, depots] : g_config.injectedDepots.get())
		{
			(void)appId;
			if (depots.contains(depotId))
			{
				return true;
			}
		}
		return false;
	}

	void replaceAll(std::string& value, const std::string& needle, const std::string& replacement)
	{
		for (size_t pos = 0; (pos = value.find(needle, pos)) != std::string::npos; pos += replacement.size())
		{
			value.replace(pos, needle.size(), replacement);
		}
	}

	std::optional<uint64_t> parseDecimal(const std::string& body)
	{
		size_t begin = 0;
		while (begin < body.size() && std::isspace(static_cast<unsigned char>(body[begin])))
		{
			begin++;
		}

		size_t end = begin;
		while (end < body.size() && std::isdigit(static_cast<unsigned char>(body[end])))
		{
			end++;
		}
		if (end > begin)
		{
			try
			{
				return std::stoull(body.substr(begin, end - begin));
			}
			catch (...)
			{
			}
		}

		const size_t content = body.find("\"content\"");
		if (content == std::string::npos)
		{
			return std::nullopt;
		}
		begin = body.find_first_of("0123456789", content + 9);
		if (begin == std::string::npos)
		{
			return std::nullopt;
		}
		end = begin;
		while (end < body.size() && std::isdigit(static_cast<unsigned char>(body[end])))
		{
			end++;
		}
		try
		{
			return std::stoull(body.substr(begin, end - begin));
		}
		catch (...)
		{
			return std::nullopt;
		}
	}

	uint64_t fetchManifestCode(uint32_t appId, uint32_t depotId, uint64_t manifestId)
	{
		std::vector<std::string> urls;
		const std::string configuredUrl = g_config.manifestCodeUrl.get();
		if (!configuredUrl.empty())
		{
			urls.emplace_back(configuredUrl);
		}
		else
		{
			constexpr std::array<const char*, 3> defaults = {
				"https://manifest.opensteamtool.com/{manifest_id}",
				"http://gmrc.wudrm.com/manifest/{manifest_id}",
				"https://manifest.steam.run/api/manifest/{manifest_id}",
			};
			urls.assign(defaults.begin(), defaults.end());
		}

		const uint32_t timeout = g_config.manifestCodeTimeout.get();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
		for (std::string url : urls)
		{
			replaceAll(url, "{app_id}", std::to_string(appId));
			replaceAll(url, "{depot_id}", std::to_string(depotId));
			replaceAll(url, "{manifest_id}", std::to_string(manifestId));

			const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
				deadline - std::chrono::steady_clock::now()
			).count();
			if (remaining <= 0)
			{
				break;
			}

			std::string body;
			if (Curl::getString(url.c_str(), body, static_cast<uint32_t>(remaining)) != 0)
			{
				g_pLog->debug("Manifest code request failed via %s\n", url.c_str());
				continue;
			}
			const auto code = parseDecimal(body);
			if (code && *code != 0)
			{
				return *code;
			}
			g_pLog->debug("Manifest code response was invalid via %s\n", url.c_str());
		}
		g_pLog->debug("Manifest code lookup failed for %u/%u/%llu\n", appId, depotId, manifestId);
		return 0;
	}

	std::optional<std::vector<uint8_t>> decodeDepotKey(const std::string& value)
	{
		if (value.size() != 64)
		{
			return std::nullopt;
		}
		std::vector<uint8_t> bytes;
		bytes.reserve(32);
		for (size_t i = 0; i < value.size(); i += 2)
		{
			auto nibble = [](char ch) -> int {
				if (ch >= '0' && ch <= '9') return ch - '0';
				if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
				if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
				return -1;
			};
			const int high = nibble(value[i]);
			const int low = nibble(value[i + 1]);
			if (high < 0 || low < 0)
			{
				return std::nullopt;
			}
			bytes.push_back(static_cast<uint8_t>((high << 4) | low));
		}
		return bytes;
	}
}

void ContentHooks::sendMsg(CProtoBufMsgBase* msg)
{
	if (!msg || !msg->header || !g_config.enableContentHooks.get())
	{
		return;
	}

	if (msg->type == EMSG_GET_DEPOT_DECRYPTION_KEY)
	{
		auto request = msg->getBody<CMsgClientGetDepotDecryptionKey>();
		if (!request || !request->has_depot_id() || !msg->header->has_jobid_source())
		{
			return;
		}
		if (!g_config.depotKeys.get().contains(request->depot_id()))
		{
			return;
		}
		std::lock_guard lock(stateMutex);
		const auto now = std::chrono::steady_clock::now();
		for (auto it = pendingDepotKeys.begin(); it != pendingDepotKeys.end();)
		{
			if (now - it->second.created > PENDING_MAX_AGE)
			{
				it = pendingDepotKeys.erase(it);
			}
			else
			{
				++it;
			}
		}
		if (pendingDepotKeys.size() >= MAX_PENDING_JOBS)
		{
			g_pLog->debug("DepotKey queue is full; skipping job %llu\n", msg->header->jobid_source());
			return;
		}
		pendingDepotKeys[msg->header->jobid_source()] = {
			request->depot_id(),
			now,
		};
		return;
	}

	if (msg->type != EMSG_SERVICE_METHOD_CALL_FROM_CLIENT ||
		!msg->header->has_target_job_name() ||
		msg->header->target_job_name() != MANIFEST_JOB ||
		!msg->header->has_jobid_source())
	{
		return;
	}

	auto request = msg->getBody<CContentServerDirectory_GetManifestRequestCode_Request>();
	if (!request || !request->has_depot_id() || !request->has_manifest_id())
	{
		return;
	}
	const uint32_t appId = request->has_app_id() ? request->app_id() : 0;
	const uint32_t depotId = request->depot_id();
	const uint64_t manifestId = request->manifest_id();
	const uint64_t jobId = msg->header->jobid_source();
	if (!isManagedDepot(depotId))
	{
		return;
	}

	{
		std::lock_guard lock(stateMutex);
		const auto now = std::chrono::steady_clock::now();
		for (auto it = pendingManifestCodes.begin(); it != pendingManifestCodes.end();)
		{
			if (now - it->second.created > PENDING_MAX_AGE)
			{
				it = pendingManifestCodes.erase(it);
			}
			else
			{
				++it;
			}
		}
		if (pendingManifestCodes.size() >= MAX_PENDING_JOBS)
		{
			g_pLog->debug("Manifest code queue is full; skipping job %llu\n", jobId);
			return;
		}
	}

	auto task = std::async(std::launch::async, [appId, depotId, manifestId]() {
		return fetchManifestCode(appId, depotId, manifestId);
	});
	std::lock_guard lock(stateMutex);
	pendingManifestCodes[jobId] = {
		task.share(),
		std::chrono::steady_clock::now(),
	};
}

void ContentHooks::recvMsg(CProtoBufMsgBase* msg)
{
	if (!msg || !msg->header || !g_config.enableContentHooks.get())
	{
		return;
	}

	if (msg->type == EMSG_GET_DEPOT_DECRYPTION_KEY_RESPONSE)
	{
		auto response = msg->getBody<CMsgClientGetDepotDecryptionKeyResponse>();
		uint32_t depotId = response && response->has_depot_id() ? response->depot_id() : 0;
		if (msg->header->has_jobid_target())
		{
			std::lock_guard lock(stateMutex);
			auto it = pendingDepotKeys.find(msg->header->jobid_target());
			if (it != pendingDepotKeys.end())
			{
				if (!depotId)
				{
					depotId = it->second.depotId;
				}
				pendingDepotKeys.erase(it);
			}
		}
		if (!response || !depotId)
		{
			return;
		}
		const auto keys = g_config.depotKeys.get();
		const auto it = keys.find(depotId);
		if (it == keys.end())
		{
			return;
		}
		const auto key = decodeDepotKey(it->second);
		if (!key)
		{
			g_pLog->notify("Invalid DepotKey for %u; expected 64 hexadecimal characters", depotId);
			return;
		}
		response->set_eresult(ERESULT_OK);
		response->set_depot_id(depotId);
		response->set_depot_encryption_key(key->data(), key->size());
		msg->header->set_eresult(ERESULT_OK);
		g_pLog->once("Injected DepotKey for %u\n", depotId);
		return;
	}

	if (msg->type != EMSG_SERVICE_METHOD_RESPONSE || !msg->header->has_jobid_target())
	{
		return;
	}

	std::shared_future<uint64_t> future;
	{
		std::lock_guard lock(stateMutex);
		auto it = pendingManifestCodes.find(msg->header->jobid_target());
		if (it == pendingManifestCodes.end())
		{
			return;
		}
		future = it->second.future;
		pendingManifestCodes.erase(it);
	}

	const uint32_t timeout = g_config.manifestCodeTimeout.get();
	if (future.wait_for(std::chrono::seconds(timeout)) != std::future_status::ready)
	{
		g_pLog->debug("Manifest code request timed out for job %llu\n", msg->header->jobid_target());
		return;
	}
	const uint64_t code = future.get();
	if (!code)
	{
		return;
	}
	auto response = msg->getBody<CContentServerDirectory_GetManifestRequestCode_Response>();
	if (!response)
	{
		return;
	}
	response->set_manifest_request_code(code);
	msg->header->set_eresult(ERESULT_OK);
	g_pLog->once("Injected manifest request code for job %llu\n", msg->header->jobid_target());
}
