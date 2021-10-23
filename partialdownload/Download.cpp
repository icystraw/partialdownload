#include "Download.h"

Download::~Download()
{
	for (DownloadSection* section : Sections)
	{
		delete section;
	}
	delete SummarySection;
};

void Download::SetCredentials(std::wstring userName, std::wstring password)
{
	SummarySection->UserName = userName;
	SummarySection->Password = password;
	for (DownloadSection* section : Sections)
	{
		section->UserName = userName;
		section->Password = password;
	}
};