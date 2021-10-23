#pragma once
#include "DownloadSection.h"
#include <vector>

class Download
{
public:
	std::vector<DownloadSection*> Sections;
	DownloadSection* SummarySection = NULL;
	std::wstring DownloadFolder;
	int NoDownloader = 5;
	~Download();
	void SetCredentials(std::wstring userName, std::wstring password);
};
