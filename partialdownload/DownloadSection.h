#pragma once
#include "DownloadStatus.h"
#include <string>

class DownloadSection
{
public:
	std::wstring Url;
	std::wstring FileName;
	DownloadStatus DownloadStatus = DownloadStatus::Stopped;
	long long Start = 0;
	long long End = 0;
	long long BytesDownloaded = 0;
	std::wstring HttpStatusCode;
	std::wstring UserName;
	std::wstring Password;
	std::wstring Error;
	std::wstring LastModified = L"NOTSET";
	time_t LastStatusChange = 0;
	DownloadSection* NextSection = NULL;
	void* Tag = NULL;
	DownloadSection();
	DownloadSection* Copy();
	DownloadSection* Split();

	long long GetTotal();
};
