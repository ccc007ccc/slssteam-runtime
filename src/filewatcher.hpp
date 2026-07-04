#pragma once

#include <pthread.h>
#include <string>
#include <unordered_map>

typedef void(*FileModifyEvent_t)();

class CFileWatcher
{
	pthread_t watchThread;

public:
	int notifyFd;
	std::unordered_map<int, std::string> fileFdMap;

	FileModifyEvent_t onModify;

	CFileWatcher(FileModifyEvent_t onModify);
	~CFileWatcher();

	int addFile(const char* path);
	bool start();
	void stop();
};
