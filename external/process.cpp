#include "process.hpp"
#include <tlhelp32.h>

// ok i know its faster to do it with EnumProcesses and OpenProcess then getting the module file name and i already did it 100 times
// but this takes less lines of code so idc if my program is 5ms slower just in this case :)
DWORD getProcessIdByName(const std::wstring& processName) {
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(PROCESSENTRY32W);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (Process32FirstW(snapshot, &entry) == TRUE) {
        while (Process32NextW(snapshot, &entry) == TRUE) {
            if (_wcsicmp(entry.szExeFile, processName.c_str()) == 0) {
                CloseHandle(snapshot);
                return entry.th32ProcessID;
            }
        }
    }

    CloseHandle(snapshot);
    return 0;
}