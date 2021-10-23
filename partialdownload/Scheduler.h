#pragma once
#include "Download.h"
#include "Downloader.h"

class Scheduler
{
private:
	static const int maxNoDownloader = 10;
	static const long long minSectionSize = 5242880;
	static const int bufferSize = 5242880;
	Downloader* downloaders[maxNoDownloader] = {};
	Download* download = NULL;
	DownloadSection* sectionBeingEvaluated = NULL;
	bool downloadStopFlag = false;
	HANDLE hDownloadThread = NULL;
	CRITICAL_SECTION sectionsLock;
	int FindFreeDownloader();
	void StopDownloading();
	int FindDownloaderBySection(DownloadSection* ds);
	void DownloadSectionWithFreeDownloaderIfPossible(DownloadSection* ds);
	void AutoDownloadSection(DownloadSection* ds);
	bool ErrorAndUnstableSectionsExist();
	void EvaluateStatusOfJustCreatedSectionIfExists();
	void CreateNewSectionIfFeasible();
	void TryDownloadingAllUnfinishedSections();
	void ProcessSections();
	bool IsSchedulerThreadAlive();
	void WaitForFinish();
	bool IsDownloadHalted();
	static DWORD WINAPI DownloadThreadProc(LPVOID lParam);
	void DownloadThreadStart();
	bool JoinSectionsToFile();
	void SetDownloadError(std::wstring errorMessage, DownloadStatus status = DownloadStatus::DownloadError);
public:
	bool IsDownloadResumable();
	DownloadStatus GetDownloadStatus();
	std::wstring GetDownloadStatusDescription();
	void Start();
	void Stop(bool cancel, bool wait);
	void CleanTempFiles();
	Scheduler(Download* d);
	~Scheduler();
};
