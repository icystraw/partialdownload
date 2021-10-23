#include "Util.h"
#include <tuple>
#include <windows.h>

std::wstring Util::CreateGuid()
{
	UUID uuid;
	RPC_WSTR szUuid = NULL;
	std::wstring guid;
	std::ignore = UuidCreate(&uuid);
	std::ignore = UuidToStringW(&uuid, &szUuid);
	guid = (WCHAR*)szUuid;
	RpcStringFreeW(&szUuid);
	return guid;
};

std::wstring Util::UrlGetFileName(std::wstring url)
{
	size_t indexSlash = url.find_last_of(L"/\\");
	if (indexSlash != std::wstring::npos)
	{
		url.erase(0, indexSlash + 1);
		size_t indexQuestionMark = url.find_first_of(L'?');
		if (indexQuestionMark != std::wstring::npos)
		{
			url = url.substr(0, indexQuestionMark);
		}
	}
	if (url.length() == 0)
	{
		url = L"download_file";
	}
	for (size_t i = 0; i < url.length(); i++)
	{
		wchar_t c = url[i];
		if (c == L'\\' || c == L'/' ||
			c == L':' || c == L'*' ||
			c == L'?' || c == L'"' ||
			c == L'<' || c == L'>' ||
			c == L'|')
			url[i] = L'_';
	}
	return url;
};

std::wstring Util::CombinePathAndFileName(std::wstring path, std::wstring file)
{
	if (path.length() > 0)
	{
		if (path[path.length() - 1] == L'\\')
		{
			return path + file;
		}
		else
		{
			return path + L'\\' + file;
		}
	}
	return file;
};