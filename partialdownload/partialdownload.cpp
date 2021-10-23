#include "resource.h"
#include "Download.h"
#include "Scheduler.h"
#include <windows.h>
#include <Shlobj.h>
#include <shlwapi.h>
#include <string>

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

Download* d = NULL;
Scheduler* s = NULL;

std::wstring GetDlgItemText(HWND hDlg, int item)
{
	int length = GetWindowTextLength(GetDlgItem(hDlg, item));
	if (length > 0)
	{
		WCHAR* buffer = new WCHAR[(size_t)length + 1];
		if (GetWindowText(GetDlgItem(hDlg, item), buffer, length + 1))
		{
			std::wstring ret = buffer;
			delete[] buffer;
			return ret;
		}
		delete[] buffer;
	}
	return L"";
}

long long GetIntInput(long long defaultValue, std::wstring str)
{
	long long ll = 0;
	try
	{
		ll = stoll(str);
	}
	catch (...)
	{
		ll = defaultValue;
	}
	return ll;
}

void StartButton_Click(HWND hDlg)
{
	std::wstring url = GetDlgItemText(hDlg, TXTURL);
	std::wstring downloadFolder = GetDlgItemText(hDlg, TXTDOWNLOADFOLDER);
	long long start = GetIntInput(0, GetDlgItemText(hDlg, TXTSTART));
	long long end = GetIntInput((-1), GetDlgItemText(hDlg, TXTEND));
	std::wstring userName = GetDlgItemText(hDlg, TXTUSERNAME);
	std::wstring password = GetDlgItemText(hDlg, TXTPASSWORD);
	int noDownloader = (int)GetIntInput(5, GetDlgItemText(hDlg, TXTNODOWNLOADER));

	if (!PathFileExistsW(downloadFolder.c_str()))
	{
		MessageBox(hDlg, L"Download folder does not exist.", L"Information", MB_OK | MB_ICONINFORMATION);
		return;
	}
	if (noDownloader < 1 || noDownloader > 10) noDownloader = 5;
	if (url.empty())
	{
		MessageBox(hDlg, L"Empty URL.", L"Information", MB_OK | MB_ICONINFORMATION);
		return;
	}
	if (start < 0)
	{
		MessageBox(hDlg, L"Start position less than zero.", L"Information", MB_OK | MB_ICONINFORMATION);
		return;
	}
	if (end >= 0 && end < start)
	{
		MessageBox(hDlg, L"End position is before start position.", L"Information", MB_OK | MB_ICONINFORMATION);
		return;
	}

	DownloadSection* ds = new DownloadSection;
	ds->Url = url;
	ds->Start = start;
	ds->End = end;
	ds->UserName = userName;
	ds->Password = password;

	DownloadSection* ss = ds->Copy();

	d = new Download();
	d->NoDownloader = noDownloader;
	d->DownloadFolder = downloadFolder;
	d->SummarySection = ss;
	d->Sections.push_back(ds);

	s = new Scheduler(d);
	s->Start();

	SetTimer(hDlg, 1, 1000, NULL);
	EnableWindow(GetDlgItem(hDlg, IDSTART), FALSE);
	EnableWindow(GetDlgItem(hDlg, TXTURL), FALSE);
	EnableWindow(GetDlgItem(hDlg, IDC_BROWSE), FALSE);
	EnableWindow(GetDlgItem(hDlg, TXTSTART), FALSE);
	EnableWindow(GetDlgItem(hDlg, TXTEND), FALSE);
	EnableWindow(GetDlgItem(hDlg, IDPAUSE), TRUE);
}

void PauseButton_Click(HWND hDlg)
{
	if (!s) return;
	DownloadStatus status = s->GetDownloadStatus();
	if (status == DownloadStatus::Finished) return;
	if (status == DownloadStatus::Stopped)
	{
		int noDownloader = (int)GetIntInput(5, GetDlgItemText(hDlg, TXTNODOWNLOADER));
		if (noDownloader < 1 || noDownloader > 10) noDownloader = 5;
		std::wstring userName = GetDlgItemText(hDlg, TXTUSERNAME);
		std::wstring password = GetDlgItemText(hDlg, TXTPASSWORD);
		d->NoDownloader = noDownloader;
		d->SetCredentials(userName, password);
		s->Start();
	}
	if (status == DownloadStatus::DownloadError)
	{
		s->Start();
	}
	if (status == DownloadStatus::Downloading)
	{
		s->Stop(false, false);
	}
	SetTimer(hDlg, 1, 1000, NULL);
}

void Timer_Tick(HWND hDlg)
{
	KillTimer(hDlg, 1);
	DownloadStatus status = s->GetDownloadStatus();
	SetWindowText(GetDlgItem(hDlg, IDC_STATUS), s->GetDownloadStatusDescription().c_str());
	if (status == DownloadStatus::Downloading) SetTimer(hDlg, 1, 1000, NULL);
	else if (status == DownloadStatus::Finished) EnableWindow(GetDlgItem(hDlg, IDPAUSE), FALSE);
}

void Window_Closing(HWND hDlg)
{
	bool canClose = false;
	DownloadStatus status;
	if (!s) canClose = true;
	if (!canClose)
	{
		status = s->GetDownloadStatus();
		if (status == DownloadStatus::Finished) canClose = true;
	}
	if (!canClose)
	{
		if (MessageBox(hDlg, L"Download not finished. Do you want to cancel and quit?", L"Question", MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
		{
			if (status == DownloadStatus::Downloading)
			{
				s->Stop(true, true);
			}
			else
			{
				s->CleanTempFiles();
			}
			canClose = true;
		}
	}
	if (canClose)
	{
		if (s) delete s;
		EndDialog(hDlg, IDOK);
	}
}

void BrowseForFolder(HWND hDlg)
{
	IFileOpenDialog* pFileOpen = NULL;
	DWORD dwOptions;
	IShellItem* pItem = NULL;
	PWSTR pszFilePath = NULL;

	// Create the FileOpenDialog object.
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
		IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

	if (SUCCEEDED(hr))
	{
		// Set the dialog as a folder picker.
		hr = pFileOpen->GetOptions(&dwOptions);
	}
	if (SUCCEEDED(hr))
	{
		hr = pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
	}
	if (SUCCEEDED(hr))
	{
		// Show the Open dialog box.
		hr = pFileOpen->Show(hDlg);
	}
	// Get the file name from the dialog box.
	if (SUCCEEDED(hr))
	{
		hr = pFileOpen->GetResult(&pItem);
	}
	if (SUCCEEDED(hr))
	{
		hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
	}
	// Display the file name to the user.
	if (SUCCEEDED(hr))
	{
		SetWindowText(GetDlgItem(hDlg, TXTDOWNLOADFOLDER), pszFilePath);
		CoTaskMemFree(pszFilePath);
	}
	if (pItem) pItem->Release();
	if (pFileOpen) pFileOpen->Release();
}

std::wstring GetClipboardUrl()
{
	BOOL bResults = TRUE;
	HANDLE hText = NULL;
	PWSTR pText = NULL;
	std::wstring ret;

	if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
	{
		return ret;
	}
	bResults = OpenClipboard(NULL);
	if (bResults)
	{
		hText = GetClipboardData(CF_UNICODETEXT);
		if (NULL == hText) bResults = FALSE;
	}
	else
	{
		return ret;
	}
	if (bResults)
	{
		pText = (PWSTR)GlobalLock(hText);
		if (NULL == pText) bResults = FALSE;
	}
	if (bResults)
	{
		ret = pText;
		if (ret.rfind(L"http", 0) == std::wstring::npos)
		{
			ret = L"";
		}
	}
	if (pText) GlobalUnlock(hText);
	CloseClipboard();
	return ret;
}

INT_PTR CALLBACK WndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	PWSTR path = NULL;
	HRESULT hr;
	std::wstring noDownloader = L"5";
	switch (message)
	{
	case WM_CLOSE:
		Window_Closing(hDlg);
		return (INT_PTR)TRUE;

	case WM_TIMER:
		Timer_Tick(hDlg);
		return (INT_PTR)TRUE;

	case WM_INITDIALOG:
		EnableWindow(GetDlgItem(hDlg, IDPAUSE), FALSE);
		hr = SHGetKnownFolderPath(FOLDERID_Desktop, 0, NULL, &path);
		if (SUCCEEDED(hr))
		{
			SetWindowText(GetDlgItem(hDlg, TXTDOWNLOADFOLDER), path);
		}
		SetWindowText(GetDlgItem(hDlg, TXTNODOWNLOADER), noDownloader.c_str());
		CoTaskMemFree(path);
		SetWindowText(GetDlgItem(hDlg, TXTURL), GetClipboardUrl().c_str());
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_BROWSE)
		{
			BrowseForFolder(hDlg);
			return (INT_PTR)TRUE;
		}
		if (LOWORD(wParam) == IDSTART)
		{
			StartButton_Click(hDlg);
			return (INT_PTR)TRUE;
		}
		if (LOWORD(wParam) == IDPAUSE)
		{
			PauseButton_Click(hDlg);
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nCmdShow)
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (SUCCEEDED(hr))
	{
		DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAINDIALOG), NULL, WndProc);
		CoUninitialize();
		Downloader::DeleteInternetSession();
	}
}
