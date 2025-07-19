#ifndef PROCESS_H
#define PROCESS_H

#include <windows.h>
#include <string>

DWORD getProcessIdByName(const std::wstring& processName);

#endif 