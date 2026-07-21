#include "update.hpp"

#include "config.hpp"
#include "curl.hpp"
#include "globals.hpp"
#include "log.hpp"
#include "utils.hpp"
#include "version.hpp"


#include <filesystem>
#include <fstream>
#include <map>
#include <map>
#include <string>

std::map<uint64_t, std::unordered_set<std::string>> Updater::clientHashMap = std::map<uint64_t, std::unordered_set<std::string>>();

constexpr static const char* urls[] =
{
	"https://raw.githubusercontent.com/AceSLS/SLSsteam/refs/heads/main/res/updates.yaml",
	"https://cdn.jsdelivr.net/gh/AceSLS/SLSsteam/res/updates.yaml"
};

bool Updater::init()
{
	std::string data;
	int res;

	bool downloadSuccess = false;

	for(const auto url : urls)
	{
		res = Curl::getString(url, data);
		g_pLog->info("Curl Res: %u for %s with len %i\n", res, url, data.size());

		if (res == 0 && data.size() > 0) //User reported empty responses
		{
			downloadSuccess = true;
			break;
		}
	}

	if(!downloadSuccess)
	{
		data = loadFromCache();
		if(data.size() < 1)
		{
			return false;
		}

		g_pLog->info("Using cached updates.yaml\n");
	}

	g_pLog->debug("updates.yaml:\n%s\n", data.c_str());

	try
	{
		YAML::Node node = YAML::Load(data);
		for (const auto& sub : node["SafeModeHashes"])
		{
			uint64_t version = sub.first.as<uint64_t>();
			clientHashMap[version] = std::unordered_set<std::string>();

			g_pLog->debug("Parsing version %llu\n", version);

			for(const auto& hash : sub.second)
			{
				auto str = hash.as<std::string>();
				clientHashMap[version].emplace(str);

				g_pLog->debug("Added %s to SLSsteam version %llu\n", str.c_str(), version);
			}
		}
	}
	catch(...)
	{
		g_pLog->info("Failed to parse updates!\n");
		return false;
	}

	saveToCache(data);
	return true;
}

std::string Updater::getCacheFilePath()
{
	auto path = g_config.getDir().append("/.updates.yaml");
	return path;
}

void Updater::saveToCache(std::string yaml)
{
	auto path = Updater::getCacheFilePath();

	std::ofstream stream = std::ofstream(path.c_str());
	stream << yaml;
	stream.close();

	g_pLog->debug("Cached res/updates.yaml!\n");
}

std::string Updater::loadFromCache()
{
	auto path = Updater::getCacheFilePath();
	if (!std::filesystem::exists(path))
	{
		return std::string();
	}

	g_pLog->debug("Loading updates.ymal from disk!\n");

	std::ifstream fstream = std::ifstream(path.c_str());
	std::stringstream buf;
	buf << fstream.rdbuf();

	fstream.close();
	return buf.str();
}

bool Updater::verifySafeModeHash()
{
	auto path = std::filesystem::path(g_modSteamClient.path);

	try
	{
		std::string sha256 = Utils::getFileSHA256(path.c_str());
		g_pLog->info("steamclient.so hash is %s\n", sha256.c_str());

		// Steam Deck public beta builds locally verified against the signatures in
		// patterns.cpp by the Linux content-pipeline fork. Keep these exact hashes
		// here because upstream's downloaded SafeMode list does not know fork-only
		// compatibility verification.
		if (sha256 == "b756a016e09ffa64fd1bbeab4ea5092b485f99607eedf7e1cfe71379aef9d82d"
			|| sha256 == "8b3be053ed8f0581bf3ef7c14d49dc2e07052fcc7e5ded45398abee5c618a7cb")
		{
			g_pLog->info("steamclient.so hash matched a locally verified build\n");
			return true;
		}

		if (!clientHashMap.contains(VERSION))
		{
			return false;
		}

		const auto& safeHashes = clientHashMap[VERSION];
		if (safeHashes.contains(sha256))
		{
			return true;
		}

		return false;
	}
	catch(std::runtime_error& err)
	{
		g_pLog->debug("Unable to read steamclient.so hash!\n");
		return false;
	}

	return true;
}
