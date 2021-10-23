#pragma once
#include <string>

class Util
{
public:
	static std::wstring CreateGuid();
	static std::wstring UrlGetFileName(std::wstring url);
	static std::wstring CombinePathAndFileName(std::wstring path, std::wstring file);
};