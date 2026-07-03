#include "api.hpp"

#include "sdk/IClientAppManager.hpp"

#include "config.hpp"
#include "filewatcher.hpp"
#include "utils.hpp"

#include <ios>
#include <iterator>
#include <mutex>


namespace SLSAPI
{
	const char* path = "/tmp/SLSsteam.API";
	std::fstream fstream;
	CFileWatcher* watcher;

	std::mutex installsMutex;
	std::vector<InstallCommand_t> installs;
}

bool SLSAPI::isEnabled()
{
	return g_config.api.get() && fstream.is_open();
}

void SLSAPI::onFileChange()
{
	//Hot reload support :)
	if (!isEnabled())
	{
		return;
	}

	//Shitty way to reopen the stream. We have to do this, otherwise the fstream gets invalidated when running echo >
	fstream.close();
	fstream.open(path);

	char cmd[128];
	fstream.getline(cmd, sizeof(cmd));

	g_pLog->debug("API Running %s\n", cmd);

	auto split = Utils::strsplit(cmd, "|");
	if (strcmp(split[0].c_str(), "install") == 0 && split.size() > 2)
	{
		try
		{
			uint32_t appId = std::strtoul(split[1].c_str(), nullptr, 10);
			uint32_t library = std::strtoul(split[2].c_str(), nullptr, 10);

			std::lock_guard guard(installsMutex);
			installs.emplace_back(InstallCommand_t { appId, library } );
		}
		catch(...)
		{
			g_pLog->info("API Failed to parse %s or %s!\n", split[1].c_str(), split[2].c_str());
		}
	}
}

void SLSAPI::init()
{
	fstream = std::fstream(path, std::ios::in | std::ios::out);

	watcher = new CFileWatcher(onFileChange);
	watcher->addFile(path);
	watcher->start();

	g_pLog->debug("SLSsteam API initialized!\n");
}

void SLSAPI::runIPCFrame()
{
	std::lock_guard guard(installsMutex);

	while(installs.size())
	{
		const auto app = installs.begin();
		g_pClientAppManager->installApp(app->appId, app->libraryIndex);
		installs.erase(app);

		g_pLog->debug("Installed %u to %u\n", app->appId, app->libraryIndex);
	}
}
