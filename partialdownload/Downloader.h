#pragma once
#include "DownloadSection.h"
#include <windows.h>
#include <winhttp.h>

class Downloader
{
private:
	static const std::wstring userAgentString;
	static HINTERNET hSession;
	bool downloadStopFlag = false;
	HANDLE hDownloadThread = NULL;
	HINTERNET hConnect = NULL;
	HINTERNET hRequest = NULL;
	void ResetDownloadStatus();
	bool IsDownloadThreadAlive();
	bool CheckDownloadSectionAgainstLogicalErrors();
	bool ConstructHttpRequest();
	std::wstring GetResponseHeaderValue(DWORD dwInfoLevel);
	bool SyncDownloadSectionAgainstHTTPResponse();
	void CleanUpHttpConnection();
	void SetDownloadError(std::wstring errorMessage, DownloadStatus status = DownloadStatus::DownloadError);
	static DWORD WINAPI DownloadThreadProc(LPVOID lParam);
	void DownloadThreadStart();
	void VerifyBytesDownloadedAgainstFile();
public:
	DownloadSection* Section = NULL;
	Downloader(DownloadSection* section);
	bool ChangeDownloadSection(DownloadSection* section);
	bool IsBusy();
	void StopDownloading();
	void StartDownloading();
	void WaitForFinish();
	static void DeleteInternetSession();
	~Downloader();
};
