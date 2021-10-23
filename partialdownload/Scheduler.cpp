#include "Scheduler.h"
#include "Util.h"
#include <stdexcept>
#include <ctime>
#include <shlwapi.h>

Scheduler::Scheduler(Download* d)
{
	if (!d || !d->SummarySection || d->Sections.empty())
	{
		throw std::runtime_error("Invalid state of parameter d: Scheduler(Download* d)");
	}
	if (d->NoDownloader <= 0 || d->NoDownloader > maxNoDownloader)
	{
		throw std::out_of_range("Number of download threads is out of range.");
	}
	download = d;
	if (download->SummarySection->DownloadStatus == DownloadStatus::Downloading)
	{
		download->SummarySection->DownloadStatus = DownloadStatus::Stopped;
	}
	InitializeCriticalSection(&sectionsLock);
};

Scheduler::~Scheduler()
{
	for (int i = 0; i < maxNoDownloader; i++)
	{
		if (downloaders[i]) delete downloaders[i];
	}
	if (sectionBeingEvaluated) delete sectionBeingEvaluated;
	if (download) delete download;
	DeleteCriticalSection(&sectionsLock);
};

int Scheduler::FindFreeDownloader()
{
	for (int i = 0; i < download->NoDownloader; i++)
	{
		if (!downloaders[i] || !downloaders[i]->IsBusy()) return i;
	}
	return (-1);
};

void Scheduler::StopDownloading()
{
	for (int i = 0; i < download->NoDownloader; i++)
	{
		if (downloaders[i])
		{
			downloaders[i]->StopDownloading();
		}
	}
	for (int i = 0; i < download->NoDownloader; i++)
	{
		if (downloaders[i])
		{
			downloaders[i]->WaitForFinish();
		}
	}
};

int Scheduler::FindDownloaderBySection(DownloadSection* ds)
{
	for (int i = 0; i < download->NoDownloader; i++)
	{
		if (downloaders[i] && downloaders[i]->Section == ds) return i;
	}
	return (-1);
};

void Scheduler::DownloadSectionWithFreeDownloaderIfPossible(DownloadSection* ds)
{
	int freeDownloaderIndex = FindFreeDownloader();
	if (freeDownloaderIndex >= 0)
	{
		if (!downloaders[freeDownloaderIndex])
		{
			downloaders[freeDownloaderIndex] = new Downloader(ds);
		}
		else
		{
			downloaders[freeDownloaderIndex]->ChangeDownloadSection(ds);
		}
		downloaders[freeDownloaderIndex]->StartDownloading();
	}
};

void Scheduler::AutoDownloadSection(DownloadSection* ds)
{
	int downloaderIndex = FindDownloaderBySection(ds);
	if (downloaderIndex >= 0)
	{
		downloaders[downloaderIndex]->StartDownloading();
	}
	else
	{
		DownloadSectionWithFreeDownloaderIfPossible(ds);
	}
};

bool Scheduler::ErrorAndUnstableSectionsExist()
{
	for (DownloadSection* ds : download->Sections)
	{
		DownloadStatus status = ds->DownloadStatus;
		if (status == DownloadStatus::DownloadError ||
			status == DownloadStatus::LogicalError ||
			status == DownloadStatus::PrepareToDownload)
		{
			return true;
		}
	}
	if (sectionBeingEvaluated) return true;
	return false;
};

void Scheduler::EvaluateStatusOfJustCreatedSectionIfExists()
{
	if (!sectionBeingEvaluated) return;

	DownloadStatus ds = sectionBeingEvaluated->DownloadStatus;
	if (ds == DownloadStatus::DownloadError || ds == DownloadStatus::LogicalError)
	{
		// fail to create new section. Throw this section away.
		delete sectionBeingEvaluated;
		sectionBeingEvaluated = NULL;
		return;
	}
	// section creation successful
	if (ds == DownloadStatus::Downloading || ds == DownloadStatus::Finished)
	{
		// add the new section to section chain
		DownloadSection* parent = (DownloadSection*)sectionBeingEvaluated->Tag;
		sectionBeingEvaluated->NextSection = parent->NextSection;
		sectionBeingEvaluated->Tag = NULL;

		EnterCriticalSection(&sectionsLock);
		// Downloader class has been designed in a way which won't cause havoc if Scheduler class does this
		parent->NextSection = sectionBeingEvaluated;
		parent->End = sectionBeingEvaluated->Start - 1;
		download->Sections.push_back(sectionBeingEvaluated);
		LeaveCriticalSection(&sectionsLock);

		sectionBeingEvaluated = NULL;
	}
};

void Scheduler::CreateNewSectionIfFeasible()
{
	if (ErrorAndUnstableSectionsExist() || FindFreeDownloader() == (-1)) return;
	int biggestBeingDownloadedSection = (-1);
	long long biggestDownloadingSectionSize = 0;
	// find current biggest downloading section
	for (int i = 0; i < download->Sections.size(); i++)
	{
		DownloadSection* ds = download->Sections[i];
		if (ds->DownloadStatus == DownloadStatus::Downloading && ds->HttpStatusCode == L"206")
		{
			long long bytesDownloaded = ds->BytesDownloaded;
			if (bytesDownloaded > 0 && ds->GetTotal() - bytesDownloaded > biggestDownloadingSectionSize)
			{
				biggestDownloadingSectionSize = ds->GetTotal() - bytesDownloaded;
				biggestBeingDownloadedSection = i;
			}
		}
	}
	if (biggestBeingDownloadedSection < 0) return;
	// if section size is big enough, split the section to two(creating a new download section)
	// and start downloading the new section without adjusting the size of the old section.
	if (biggestDownloadingSectionSize / 2 > minSectionSize)
	{
		sectionBeingEvaluated = download->Sections[biggestBeingDownloadedSection]->Split();
	}
};

void Scheduler::TryDownloadingAllUnfinishedSections()
{
	if (sectionBeingEvaluated && sectionBeingEvaluated->DownloadStatus == DownloadStatus::Stopped)
	{
		AutoDownloadSection(sectionBeingEvaluated);
	}
	for (DownloadSection* ds : download->Sections)
	{
		DownloadStatus status = ds->DownloadStatus;
		if (status == DownloadStatus::Stopped || status == DownloadStatus::DownloadError)
		{
			AutoDownloadSection(ds);
		}
	}
};

void Scheduler::ProcessSections()
{
	EvaluateStatusOfJustCreatedSectionIfExists();
	CreateNewSectionIfFeasible();
	TryDownloadingAllUnfinishedSections();
};

bool Scheduler::IsSchedulerThreadAlive()
{
	if (!hDownloadThread) return false;
	DWORD result = WaitForSingleObject(hDownloadThread, 0);
	return !(result == WAIT_OBJECT_0);
};

void Scheduler::CleanTempFiles()
{
	for (DownloadSection* ds : download->Sections)
	{
		DeleteFileW(ds->FileName.c_str());
	}
	if (sectionBeingEvaluated)
	{
		DeleteFileW(sectionBeingEvaluated->FileName.c_str());
	}
};

void Scheduler::WaitForFinish()
{
	if (IsSchedulerThreadAlive())
	{
		WaitForSingleObject(hDownloadThread, INFINITE);
		CloseHandle(hDownloadThread);
		hDownloadThread = NULL;
	}
};

bool Scheduler::IsDownloadHalted()
{
	for (DownloadSection* ds : download->Sections)
	{
		DownloadStatus status = ds->DownloadStatus;
		if (status == DownloadStatus::Stopped || status == DownloadStatus::DownloadError ||
			status == DownloadStatus::PrepareToDownload || status == DownloadStatus::Downloading)
			return false;
	}
	if (sectionBeingEvaluated) return false;
	return true;
};

DWORD __stdcall Scheduler::DownloadThreadProc(LPVOID lParam)
{
	Scheduler* s = (Scheduler*)lParam;
	s->DownloadThreadStart();
	return NULL;
};

void Scheduler::DownloadThreadStart()
{
	download->SummarySection->Error = L"";

	while (true)
	{
		// if there is download stop request from other thread
		if (downloadStopFlag)
		{
			StopDownloading();
			download->SummarySection->DownloadStatus = DownloadStatus::Stopped;
			return;
		}
		ProcessSections();
		Sleep(500);
		if (IsDownloadHalted()) break;
	}
	// if there is section with logical error
	if (ErrorAndUnstableSectionsExist())
	{
		SetDownloadError(L"There are sections that are in invalid states. Download cannot continue. Try re-download this file.");
		return;
	}
	if (JoinSectionsToFile())
	{
		CleanTempFiles();
		download->SummarySection->DownloadStatus = DownloadStatus::Finished;
	}
};

bool Scheduler::JoinSectionsToFile()
{
	DownloadSection* ds = download->Sections[0];
	HANDLE hDest = INVALID_HANDLE_VALUE;
	HANDLE hSection = INVALID_HANDLE_VALUE;
	LPBYTE buffer = NULL;
	BOOL bResults = FALSE;
	std::wstring fileNameWithPath;
	if (!download->DownloadFolder.empty() && PathFileExistsW(download->DownloadFolder.c_str()))
	{
		std::wstring fileNameOnly = Util::UrlGetFileName(ds->Url);
		fileNameWithPath = Util::CombinePathAndFileName(download->DownloadFolder, fileNameOnly);
		if (PathFileExistsW(fileNameWithPath.c_str()))
		{
			fileNameOnly = std::to_wstring(time(NULL)) + L'_' + fileNameOnly;
			fileNameWithPath = Util::CombinePathAndFileName(download->DownloadFolder, fileNameOnly);
		}
	}
	else
	{
		SetDownloadError(L"Download folder is not present.");
		return false;
	}

	buffer = new BYTE[bufferSize];
	long long totalFileSize = 0;
	hDest = CreateFileW(fileNameWithPath.c_str(), FILE_GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	bResults = !(INVALID_HANDLE_VALUE == hDest);

	if (bResults)
	{
		while (true)
		{
			if (ds->DownloadStatus == DownloadStatus::Finished)
			{
				totalFileSize += ds->GetTotal();
				hSection = CreateFileW(ds->FileName.c_str(), FILE_GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				if (INVALID_HANDLE_VALUE == hSection)
				{
					bResults = FALSE;
				}
				if (bResults)
				{
					long long bytesRead = 0;
					while (true)
					{
						DWORD bytesToReadThisTime = (ds->GetTotal() - bytesRead >= bufferSize) ? bufferSize : (DWORD)(ds->GetTotal() - bytesRead);
						// reached the end
						if (bytesToReadThisTime == 0) break;
						DWORD bytesReadThisTime = 0, bytesWrittenThisTime = 0;
						bResults = ReadFile(hSection, buffer, bytesToReadThisTime, &bytesReadThisTime, NULL);
						if (bResults)
						{
							bytesRead += bytesReadThisTime;
							bResults = WriteFile(hDest, buffer, bytesReadThisTime, &bytesWrittenThisTime, NULL);
						}
						if (!bResults) break;
					}
				}
				if (bResults)
				{
					CloseHandle(hSection);
				}
			}
			if (bResults)
			{
				if (ds->NextSection) ds = ds->NextSection;
				else break;
			}
			if (!bResults) break;
		}
	}
	if (bResults)
	{
		CloseHandle(hDest);
		download->SummarySection->FileName = fileNameWithPath;
		download->SummarySection->End = download->SummarySection->Start + totalFileSize - 1;
		download->SummarySection->BytesDownloaded = totalFileSize;
	}
	if (!bResults)
	{
		if (download->SummarySection->Error.empty()) SetDownloadError(L"Error occurred: " + std::to_wstring(GetLastError()));
		if (hSection != INVALID_HANDLE_VALUE) CloseHandle(hSection);
		if (hDest != INVALID_HANDLE_VALUE) CloseHandle(hDest);
	}
	delete[] buffer;
	return bResults;
};

void Scheduler::SetDownloadError(std::wstring errorMessage, DownloadStatus status)
{
	download->SummarySection->Error = errorMessage;
	download->SummarySection->LastStatusChange = time(NULL);
	download->SummarySection->DownloadStatus = status;
};

bool Scheduler::IsDownloadResumable()
{
	EnterCriticalSection(&sectionsLock);
	for (DownloadSection* ds : download->Sections)
	{
		if (ds->HttpStatusCode == L"200")
		{
			LeaveCriticalSection(&sectionsLock);
			return false;
		}
	}
	LeaveCriticalSection(&sectionsLock);
	return true;
};

DownloadStatus Scheduler::GetDownloadStatus()
{
	return download->SummarySection->DownloadStatus;
};

std::wstring Scheduler::GetDownloadStatusDescription()
{
	long long totalFileSize = 0;
	long long bytesDownloaded = 0;
	std::wstring statusStr;

	DownloadStatus dStatus = GetDownloadStatus();
	if (dStatus == DownloadStatus::Stopped)
	{
		statusStr.append(L"Download status: Paused.\r\n");
	}
	if (dStatus == DownloadStatus::Downloading)
	{
		statusStr.append(L"Download status: Downloading.\r\n");
	}
	if (dStatus == DownloadStatus::DownloadError)
	{
		statusStr.append(L"Download status: Error Occurred.\r\n");
	}
	if (dStatus == DownloadStatus::Finished)
	{
		statusStr.append(L"Download status: Successfully Finished.\r\n");
	}

	EnterCriticalSection(&sectionsLock);

	for (DownloadSection* ds : download->Sections)
	{
		DownloadStatus sStatus = ds->DownloadStatus;
		if (sStatus == DownloadStatus::LogicalError || sStatus == DownloadStatus::DownloadError)
		{
			statusStr.append(L"Section Error: ");
			statusStr.append(ds->Error);
			statusStr.append(L"\r\n");
		}
		totalFileSize += ds->GetTotal();
		bytesDownloaded += ds->BytesDownloaded;
	}

	LeaveCriticalSection(&sectionsLock);

	if (dStatus == DownloadStatus::DownloadError)
	{
		statusStr.append(L"Download Error: ");
		statusStr.append(download->SummarySection->Error);
		statusStr.append(L"\r\n");
	}
	if (totalFileSize > 0 && bytesDownloaded > totalFileSize) bytesDownloaded = totalFileSize;
	statusStr.append(L"Total ");
	statusStr.append(std::to_wstring(totalFileSize));
	statusStr.append(L" bytes, ");
	statusStr.append(std::to_wstring(bytesDownloaded));
	statusStr.append(L" bytes downloaded.\r\n");
	if (totalFileSize > 0)
	{
		long long percentage = bytesDownloaded * 100 / totalFileSize;
		statusStr.append(std::to_wstring(percentage));
		statusStr.append(L"% completed.\r\n");
	}

	return statusStr;
};

void Scheduler::Start()
{
	DownloadStatus status = GetDownloadStatus();
	if (status == DownloadStatus::Finished || status == DownloadStatus::Downloading) return;
	if (IsSchedulerThreadAlive()) return;
	downloadStopFlag = false;
	if (hDownloadThread) CloseHandle(hDownloadThread);
	hDownloadThread = CreateThread(NULL, 0, DownloadThreadProc, this, 0, NULL);
	if (hDownloadThread)
	{
		download->SummarySection->DownloadStatus = DownloadStatus::Downloading;
	}
};

void Scheduler::Stop(bool cancel, bool wait)
{
	if (GetDownloadStatus() == DownloadStatus::Downloading)
	{
		downloadStopFlag = true;
		if (cancel)
		{
			WaitForFinish();
			CleanTempFiles();
		}
		else
		{
			if (wait)
			{
				WaitForFinish();
			}
		}
	}
	else if (cancel) CleanTempFiles();
};
