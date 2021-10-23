#include "DownloadSection.h"
#include "Util.h"
#include <windows.h>

DownloadSection::DownloadSection()
{
	WCHAR lpBuffer[MAX_PATH + 1];
	std::wstring tempFolder;
	if (GetTempPathW(MAX_PATH + 1, lpBuffer))
	{
		tempFolder = lpBuffer;
	}
	FileName = tempFolder + Util::CreateGuid();
};

DownloadSection* DownloadSection::Copy()
{
	DownloadSection* newSection = new DownloadSection();
	newSection->Url = Url;
	newSection->Start = Start;
	newSection->End = End;
	newSection->UserName = UserName;
	newSection->Password = Password;

	return newSection;
};

DownloadSection* DownloadSection::Split()
{
	long long _BytesDownloaded = BytesDownloaded;
	DownloadSection* newSection = new DownloadSection();
	newSection->Url = Url;
	newSection->Start = Start + _BytesDownloaded + (End - (Start + _BytesDownloaded)) / 2;
	newSection->End = End;
	if (newSection->Start > newSection->End)
	{
		delete newSection;
		return nullptr;
	}
	newSection->UserName = UserName;
	newSection->Password = Password;
	newSection->LastModified = LastModified;
	newSection->Tag = this;
	return newSection;
};

long long DownloadSection::GetTotal()
{
	return End - Start + 1;
};