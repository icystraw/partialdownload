#include "Downloader.h"
#include <ctime>
#include <stdexcept>
#include <strsafe.h>
#include <shlwapi.h>

const std::wstring Downloader::userAgentString = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/92.0.4515.131 Safari/537.36";
HINTERNET Downloader::hSession = NULL;

void Downloader::ResetDownloadStatus()
{
	if (Section->DownloadStatus == DownloadStatus::PrepareToDownload || Section->DownloadStatus == DownloadStatus::Downloading)
	{
		Section->DownloadStatus = DownloadStatus::Stopped;
	}
};

Downloader::Downloader(DownloadSection* section)
{
	if (!section) throw std::runtime_error("Parameter section cannot be null: Downloader(DownloadSection* section)");
	Section = section;
	ResetDownloadStatus();
	if (!hSession)
	{
		hSession = WinHttpOpen(userAgentString.c_str(),
			WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS, 0);
	}
};

bool Downloader::IsDownloadThreadAlive()
{
	if (!hDownloadThread) return false;
	DWORD result = WaitForSingleObject(hDownloadThread, 0);
	return !(result == WAIT_OBJECT_0);
};

bool Downloader::CheckDownloadSectionAgainstLogicalErrors()
{
	if (Section->Start < 0)
	{
		SetDownloadError(L"Download start position less than zero.", DownloadStatus::LogicalError);
		return false;
	}
	if (Section->End >= 0 && Section->Start > Section->End)
	{
		SetDownloadError(L"Download start position greater than end position.", DownloadStatus::LogicalError);
		return false;
	}
	if (Section->Url.empty() || Section->FileName.empty())
	{
		SetDownloadError(L"Download URL or target file name missing.", DownloadStatus::LogicalError);
		return false;
	}
	return true;
};

bool Downloader::ChangeDownloadSection(DownloadSection* section)
{
	if (IsBusy()) return false;
	Section = section;
	ResetDownloadStatus();
	return true;
};

bool Downloader::IsBusy()
{
	DownloadStatus ds = Section->DownloadStatus;
	return (ds == DownloadStatus::PrepareToDownload || ds == DownloadStatus::Downloading);
};

void Downloader::StopDownloading()
{
	if (!IsBusy()) return;
	downloadStopFlag = true;
};

bool Downloader::ConstructHttpRequest()
{
	CleanUpHttpConnection();
	BOOL bResults = FALSE;
	DWORD dwSslFlags =
		SECURITY_FLAG_IGNORE_UNKNOWN_CA |
		SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
		SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
		SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
	const WCHAR* ppwszAcceptTypes[] = { L"*/*", NULL };
	WCHAR* hostName = NULL;
	WCHAR* urlPath = NULL;
	DWORD dwOptionValue = WINHTTP_DISABLE_REDIRECTS;
	URL_COMPONENTS urlComp;
	std::wstring range = L"Range: bytes=";

	// Initialize the URL_COMPONENTS structure.
	ZeroMemory(&urlComp, sizeof(urlComp));
	urlComp.dwStructSize = sizeof(urlComp);
	// Set required component lengths to non-zero 
	// so that they are cracked.
	urlComp.dwSchemeLength = (DWORD)-1;
	urlComp.dwHostNameLength = (DWORD)-1;
	urlComp.dwUrlPathLength = (DWORD)-1;
	urlComp.dwExtraInfoLength = (DWORD)-1;

	bResults = WinHttpCrackUrl(Section->Url.c_str(), 0, 0, &urlComp);

	if (bResults)
	{
		int bufferSize = urlComp.dwHostNameLength + 1;
		hostName = new WCHAR[bufferSize];
		StringCbCopyW(hostName, bufferSize * sizeof(WCHAR), urlComp.lpszHostName);

		bufferSize = urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength + 1;
		urlPath = new WCHAR[bufferSize];
		StringCbCopyW(urlPath, bufferSize * sizeof(WCHAR), urlComp.lpszUrlPath);
	}

	if (bResults && !hSession)
	{
		bResults = FALSE;
		SetDownloadError(L"There is no valid HTTP session.");
	}

	if (bResults)
	{
		// Specify an HTTP server.
		hConnect = WinHttpConnect(hSession, hostName,
			urlComp.nPort, 0);
		if (!hConnect) bResults = FALSE;
	}

	if (bResults)
	{
		// Create an HTTP request handle.
		hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath,
			NULL, WINHTTP_NO_REFERER,
			ppwszAcceptTypes,
			urlComp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH : WINHTTP_FLAG_REFRESH);
		if (!hRequest) bResults = FALSE;
	}

	// Ignore ssl errors
	if (bResults)
		bResults = WinHttpSetOption(
			hRequest,
			WINHTTP_OPTION_SECURITY_FLAGS,
			&dwSslFlags,
			sizeof(dwSslFlags));
	// Disable redirects
	if (bResults)
		bResults = WinHttpSetOption(
			hRequest,
			WINHTTP_OPTION_DISABLE_FEATURE,
			&dwOptionValue,
			sizeof(dwOptionValue));

	// authenticate with server
	if (bResults && !Section->UserName.empty() && !Section->Password.empty())
	{
		bResults = WinHttpSetCredentials(hRequest, WINHTTP_AUTH_TARGET_SERVER,
			WINHTTP_AUTH_SCHEME_BASIC, Section->UserName.c_str(), Section->Password.c_str(), NULL);
	}

	// set range header
	if (bResults)
	{
		long long _start = Section->Start + Section->BytesDownloaded;
		range += std::to_wstring(_start);
		range += L'-';
		if (Section->End >= 0)
		{
			range += std::to_wstring(Section->End);
		}
		bResults = WinHttpAddRequestHeaders(hRequest,
			range.c_str(),
			(ULONG)-1L,
			WINHTTP_ADDREQ_FLAG_ADD);
	}

	if (!bResults)
	{
		if (Section->Error.empty()) SetDownloadError(L"Error occurred: " + std::to_wstring(GetLastError()));
		CleanUpHttpConnection();
	}
	if (hostName) delete[] hostName;
	if (urlPath) delete[] urlPath;

	return bResults;
};

std::wstring Downloader::GetResponseHeaderValue(DWORD dwInfoLevel)
{
	std::wstring ret;
	if (!hRequest) return ret;
	DWORD dwSize = 0;
	LPVOID lpOutBuffer = NULL;

	WinHttpQueryHeaders(hRequest, dwInfoLevel,
		WINHTTP_HEADER_NAME_BY_INDEX, NULL,
		&dwSize, WINHTTP_NO_HEADER_INDEX);

	if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		lpOutBuffer = new WCHAR[dwSize / sizeof(WCHAR)];

		if (WinHttpQueryHeaders(hRequest,
			dwInfoLevel,
			WINHTTP_HEADER_NAME_BY_INDEX,
			lpOutBuffer, &dwSize,
			WINHTTP_NO_HEADER_INDEX))
		{
			ret = (WCHAR*)lpOutBuffer;
		}

		delete[] lpOutBuffer;
	}

	return ret;
};

bool Downloader::SyncDownloadSectionAgainstHTTPResponse()
{
	if (!hRequest) return false;

	std::wstring statusCode = Section->HttpStatusCode;
	std::wstring lastModified = GetResponseHeaderValue(WINHTTP_QUERY_LAST_MODIFIED);
	std::wstring sContentLength = GetResponseHeaderValue(WINHTTP_QUERY_CONTENT_LENGTH);
	long long contentLength = 0;
	if (statusCode.empty())
	{
		SetDownloadError(L"HTTP status code missing.");
		return false;
	}
	if (sContentLength.empty()) contentLength = (-1);
	else if (!StrToInt64ExW(sContentLength.c_str(), STIF_DEFAULT, &contentLength))
	{
		SetDownloadError(L"Invalid format for content length.");
		return false;
	}

	if (statusCode == L"416")
	{
		std::wstring range = GetResponseHeaderValue(WINHTTP_QUERY_CONTENT_RANGE);
		SetDownloadError(L"Requested range not satisfiable. Content-Range: " + range);
		return false;
	}
	if (statusCode != L"200" && statusCode != L"206")
	{
		SetDownloadError(L"HTTP request not successful. Maybe try again later. Status: " + statusCode);
		return false;
	}
	if (Section->LastModified != L"NOTSET" && Section->LastModified != lastModified)
	{
		SetDownloadError(L"Content changed since last time you download it. Please re-download this file.");
		return false;
	}
	if (statusCode == L"200")
	{
		// if requested section is not from the beginning and server does not support resuming
		if (Section->Start > 0)
		{
			SetDownloadError(L"Server does not support resuming, however requested section is not from the beginning of file.");
			return false;
		}
		if (contentLength != (-1))
		{
			if (Section->End < 0) Section->End = contentLength - 1;
			else if (contentLength < Section->GetTotal())
			{
				SetDownloadError(L"Content length returned from server is smaller than the section requested. Content length: " + sContentLength);
				return false;
			}
		}
		Section->BytesDownloaded = 0;
	}
	if (statusCode == L"206")
	{
		if (contentLength == (-1))
		{
			SetDownloadError(L"HTTP Content-Length missing.");
			return false;
		}
		if (Section->End >= 0 && Section->Start + Section->BytesDownloaded + contentLength - 1 != Section->End)
		{
			SetDownloadError(L"Content length from server does not match requested download section. Content length: " + sContentLength);
			return false;
		}
		// if it is a new download and all goes well
		if (Section->End < 0)
		{
			Section->End = Section->Start + contentLength - 1;
		}
	}
	Section->LastModified = lastModified;

	return true;
};

void Downloader::StartDownloading()
{
	if (IsBusy()) return;
	if (IsDownloadThreadAlive()) return;
	downloadStopFlag = false;
	if (Section->DownloadStatus == DownloadStatus::Finished) return;
	if (!CheckDownloadSectionAgainstLogicalErrors()) return;
	if (Section->DownloadStatus == DownloadStatus::DownloadError)
	{
		if (time(NULL) - Section->LastStatusChange < 10) return;
	}
	if (hDownloadThread) CloseHandle(hDownloadThread);
	hDownloadThread = CreateThread(NULL, 0, DownloadThreadProc, this, 0, NULL);
	if (hDownloadThread)
	{
		Section->DownloadStatus = DownloadStatus::PrepareToDownload;
	}
};

void Downloader::WaitForFinish()
{
	if (IsDownloadThreadAlive())
	{
		WaitForSingleObject(hDownloadThread, INFINITE);
		CloseHandle(hDownloadThread);
		hDownloadThread = NULL;
	}
	downloadStopFlag = false;
};

void Downloader::DeleteInternetSession()
{
	if (hSession) WinHttpCloseHandle(hSession);
	hSession = NULL;
};

void Downloader::SetDownloadError(std::wstring errorMessage, DownloadStatus status)
{
	Section->Error = errorMessage;
	Section->LastStatusChange = time(NULL);
	Section->DownloadStatus = status;
};

void Downloader::CleanUpHttpConnection()
{
	if (hRequest) WinHttpCloseHandle(hRequest);
	if (hConnect) WinHttpCloseHandle(hConnect);
	hRequest = NULL;
	hConnect = NULL;
};

DWORD WINAPI Downloader::DownloadThreadProc(LPVOID lParam)
{
	Downloader* d = (Downloader*)lParam;
	d->DownloadThreadStart();
	return NULL;
};

void Downloader::VerifyBytesDownloadedAgainstFile()
{
	if (Section->BytesDownloaded == 0) return;
	HANDLE hFile = CreateFileW(Section->FileName.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE == hFile)
	{
		Section->BytesDownloaded = 0;
		return;
	}
	LARGE_INTEGER fileSize;
	fileSize.QuadPart = 0;
	if (GetFileSizeEx(hFile, &fileSize))
	{
		if (fileSize.QuadPart != Section->BytesDownloaded)
		{
			Section->BytesDownloaded = 0;
		}
	}
	else
	{
		Section->BytesDownloaded = 0;
	}
	CloseHandle(hFile);
};

void Downloader::DownloadThreadStart()
{
	Section->HttpStatusCode = L"";
	Section->Error = L"";
	VerifyBytesDownloadedAgainstFile();
	if (Section->End >= 0 && Section->BytesDownloaded >= Section->GetTotal())
	{
		Section->DownloadStatus = DownloadStatus::Finished;
		return;
	}
	HANDLE hFile = INVALID_HANDLE_VALUE;
	LPBYTE buffer = NULL;
	const DWORD bufferSize = 524288;
	DWORD dwNumberOfBytesRead = 0;
	DWORD dwNumberOfBytesWritten;
	long long currentEnd = Section->End;
	BOOL bResults = ConstructHttpRequest();
	// Send a request.
	if (bResults)
		bResults = WinHttpSendRequest(hRequest,
			WINHTTP_NO_ADDITIONAL_HEADERS,
			0, WINHTTP_NO_REQUEST_DATA, 0,
			0, 0);
	// End the request.
	if (bResults)
		bResults = WinHttpReceiveResponse(hRequest, NULL);
	if (bResults)
	{
		Section->HttpStatusCode = GetResponseHeaderValue(WINHTTP_QUERY_STATUS_CODE);
		// handle redirects
		int retry = 0;
		while (Section->HttpStatusCode == L"301" || Section->HttpStatusCode == L"302" ||
			Section->HttpStatusCode == L"307" || Section->HttpStatusCode == L"308")
		{
			if (retry == 5) break;
			std::wstring location = GetResponseHeaderValue(WINHTTP_QUERY_LOCATION);
			if (location.empty())
			{
				bResults = FALSE;
				SetDownloadError(L"Redirect Location header is missing.");
			}
			if (bResults)
			{
				Section->Url = location;
				bResults = ConstructHttpRequest();
			}
			if (bResults)
				// Send a request.
				bResults = WinHttpSendRequest(hRequest,
					WINHTTP_NO_ADDITIONAL_HEADERS,
					0, WINHTTP_NO_REQUEST_DATA, 0,
					0, 0);
			// End the request.
			if (bResults)
				bResults = WinHttpReceiveResponse(hRequest, NULL);
			if (bResults)
			{
				Section->HttpStatusCode = GetResponseHeaderValue(WINHTTP_QUERY_STATUS_CODE);
				retry++;
			}
			else break;
		}
	}
	if (bResults)
		bResults = SyncDownloadSectionAgainstHTTPResponse();
	if (bResults && downloadStopFlag)
	{
		CleanUpHttpConnection();
		Section->DownloadStatus = DownloadStatus::Stopped;
		return;
	}
	if (bResults)
	{
		// append to target file
		hFile = CreateFileW(Section->FileName.c_str(), FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (INVALID_HANDLE_VALUE == hFile)
		{
			bResults = FALSE;
		}
	}
	if (bResults)
	{
		Section->DownloadStatus = DownloadStatus::Downloading;
		currentEnd = Section->End;
		buffer = new BYTE[bufferSize];
		bResults = WinHttpReadData(hRequest, buffer, bufferSize, &dwNumberOfBytesRead);
	}
	if (bResults)
	{
		while (dwNumberOfBytesRead > 0)
		{
			bResults = WriteFile(hFile, buffer, dwNumberOfBytesRead, &dwNumberOfBytesWritten, NULL);
			if (bResults)
			{
				Section->BytesDownloaded += dwNumberOfBytesRead;
				// End can be reduced by Scheduler thread.
				currentEnd = Section->End;
				if (currentEnd >= 0 && Section->BytesDownloaded >= (currentEnd - Section->Start + 1)) break;
				if (downloadStopFlag)
				{
					CloseHandle(hFile);
					CleanUpHttpConnection();
					delete[] buffer;
					Section->DownloadStatus = DownloadStatus::Stopped;
					return;
				}
				bResults = WinHttpReadData(hRequest, buffer, bufferSize, &dwNumberOfBytesRead);
			}
			if (!bResults) break;
		}
	}
	if (bResults)
	{
		CloseHandle(hFile);
		CleanUpHttpConnection();
		delete[] buffer;
		currentEnd = Section->End;
		if (currentEnd >= 0 && Section->BytesDownloaded < (currentEnd - Section->Start + 1))
		{
			SetDownloadError(L"Download stream reached the end, but not enough data transmitted.");
			return;
		}
		if (Section->HttpStatusCode == L"200" && Section->End < 0)
		{
			Section->End = Section->BytesDownloaded - 1;
		}
		Section->DownloadStatus = DownloadStatus::Finished;
	}

	if (!bResults)
	{
		if (Section->Error.empty()) SetDownloadError(L"Error occurred: " + std::to_wstring(GetLastError()));
		if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
		CleanUpHttpConnection();
		if (buffer) delete[] buffer;
	}
};

Downloader::~Downloader()
{
	if (hDownloadThread)
	{
		CloseHandle(hDownloadThread);
		hDownloadThread = NULL;
	}
};