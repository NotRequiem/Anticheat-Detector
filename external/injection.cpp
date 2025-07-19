#include "injection.hpp"
#include <iostream>
#include <tchar.h>

// IM LAZY TO COMMENT JUST READ THE STD::COUT PRINTS

bool injectDLL(DWORD processId, const std::string& dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (hProcess == NULL) {
        std::cerr << "[-] Failed to open target process. Error: " << GetLastError() << std::endl;
        return false;
    }
    std::cout << "[+] Successfully attached to Minecraft :)" << std::endl;

    LPVOID pDllPathAddr = VirtualAllocEx(hProcess, NULL, dllPath.length() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (pDllPathAddr == NULL) {
        std::cerr << "[-] Failed to allocate memory in target process. Error: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[+] Successfully allocated external virtual memory" << std::endl;

    if (!WriteProcessMemory(hProcess, pDllPathAddr, dllPath.c_str(), dllPath.length() + 1, NULL)) {
        std::cerr << "[-] Failed to write DLL path to target process. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pDllPathAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[+] Successfully wrote internal module into Minecraft" << std::endl;

    HMODULE hModule = GetModuleHandle(_T("kernel32.dll"));
    if (!hModule) {
        std::cerr << "[-] Failed to get handle to kernel module: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pDllPathAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    
    std::cout << "[+] Successfully got kernel handle." << std::endl;

    LPVOID pLoadLibraryA = (LPVOID)GetProcAddress(hModule, "LoadLibraryA"); // dont care if its W or A tbh, ascii is faster
    if (pLoadLibraryA == NULL) {
        std::cerr << "[-] Failed to get address of LoadLibraryA. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pDllPathAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[+] Successfully got .text address of LoadLibraryA." << std::endl;

    SleepEx(500, FALSE); // wait a bit for ntdll to actually do some work

    HANDLE hRemoteThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLibraryA, pDllPathAddr, 0, NULL);
    if (hRemoteThread == NULL) {
        std::cerr << "[-] Failed to create remote thread. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pDllPathAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    std::cout << "[+] External remore threa created. Waiting for callback communication." << std::endl;

    WaitForSingleObject(hRemoteThread, INFINITE);

    CloseHandle(hRemoteThread);
    VirtualFreeEx(hProcess, pDllPathAddr, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}