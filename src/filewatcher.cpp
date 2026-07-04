#include "filewatcher.hpp"

#include "log.hpp"

#include <sys/inotify.h>
#include <unistd.h>


//TODO: Investigate why gcc complains when put into CFileWatcher itself
void* watchLoop(void* args)
{
	auto watcher = reinterpret_cast<CFileWatcher*>(args);
	g_pLog->debug("Started FileWatcher %u\n", watcher->notifyFd);

	for(;;)
	{
		g_pLog->debug("Watching for changes...\n");

		inotify_event event {};
		size_t size = read(watcher->notifyFd, &event, sizeof(inotify_event));
		if (!size)
		{
			continue;
		}

		const char* fileName = watcher->fileFdMap[event.wd].c_str();

		g_pLog->debug("inotify %u(%s) -> %u\n", event.wd, fileName, event.mask);
		watcher->onModify();

		//Remove & readd the file because some file editors move the file instead of writing to it
		//doing so will need us to readd it, otherwise no events will fire anymore
		close(event.wd);
		watcher->fileFdMap.erase(event.wd);
		watcher->addFile(fileName);

		g_pLog->debug("Readded FileWatcher for %s!\n", fileName);
	}

	return nullptr;
}

CFileWatcher::CFileWatcher(FileModifyEvent_t onModify)
{
	this->onModify = onModify;

	notifyFd = inotify_init();
	g_pLog->debug("Created notify fd %i\n", notifyFd);
}

CFileWatcher::~CFileWatcher()
{
	if (watchThread)
	{
		stop();
	}

	if (notifyFd != -1)
	{
		close(notifyFd);

		for(const auto& fd : fileFdMap)
		{
			if (fd.first == -1)
			{
				continue;
			}

			close(fd.first);
		}
	}
}

int CFileWatcher::addFile(const char* path)
{
	int fd = inotify_add_watch(notifyFd, path, IN_MODIFY);
	if (fd == -1)
	{
		return fd;
	}

	fileFdMap[fd] = std::string(path);
	g_pLog->debug("Added %s to FileWatcher %i\n", path, notifyFd);
	return fd;
}

bool CFileWatcher::start()
{
	int code = pthread_create(&watchThread, nullptr, &watchLoop, this);
	return code == 0;
}

void CFileWatcher::stop()
{
	pthread_cancel(watchThread);
}
