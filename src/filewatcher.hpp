#pragma once

#include <filesystem>
#include <pthread.h>
#include <sys/inotify.h>
#include <unordered_map>

typedef void(*FileModifyEvent_t)();

class CFileWatcher
{
	pthread_t watchThread;

public:
	int notifyFd;
	std::unordered_map<int, std::filesystem::path> fileFdMap;

	FileModifyEvent_t onModify;

	CFileWatcher(FileModifyEvent_t onModify);
	~CFileWatcher();

	int addFile(const char* path);
	bool start();
	void stop();
};
