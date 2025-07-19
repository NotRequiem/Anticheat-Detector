#include "utils.hpp"
#include <iostream>
#include <filesystem>
#include <shlobj.h>
#include <windows.h> 

#pragma comment(lib, "Shell32.lib")

namespace fs = std::filesystem;

std::string getDownloadsPath() {
    PWSTR path = NULL;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path);
    std::string downloadsPath = "";

    if (SUCCEEDED(hr)) {
        std::wstring ws(path);
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &ws[0], (int)ws.size(), NULL, 0, NULL, NULL);
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &ws[0], (int)ws.size(), &str[0], size_needed, NULL, NULL);
        downloadsPath = str;
    }
    CoTaskMemFree(path);
    return downloadsPath;
}

std::string findOrPromptForDllPath(const std::string& dllName) {
    /* Get the path of the executable itself
     * THIS IS because if you try to just get the current location path without constructing it from the module, since its a console app, it will look in C:\Windows\System32 
     * rather than in the location we want 
     */
    wchar_t exePathBuf[MAX_PATH];
    GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);

    // Get the directory containing the executable
    fs::path exePath = exePathBuf;
    fs::path exeDir = exePath.parent_path();
    fs::path dllInExeDir = exeDir / dllName;

    // Check for the DLL in the executable's directory
    if (fs::exists(dllInExeDir)) {
        std::cout << "[+] DLL found in the application directory: " << dllInExeDir.string() << std::endl;
        return dllInExeDir.string();
    }

    // If not found, check the Downloads folder as a fallback because its the most reasonable site where this project would be currently saved
    std::string downloadsPathStr = getDownloadsPath();
    if (!downloadsPathStr.empty()) {
        fs::path downloadsPath = fs::path(downloadsPathStr) / dllName;
        if (fs::exists(downloadsPath)) {
            std::cout << "[+] DLL found in the Downloads folder: " << downloadsPath.string() << std::endl;
            return downloadsPath.string();
        }
    }

    // If still not found then wtf wtf, prompt the user until a valid path is provided
    std::string userInputPath;
    while (true) {
        std::cout << "[-] DLL not found in default locations." << std::endl;
        std::cout << "Please enter the full path to the DLL: ";
        std::getline(std::cin, userInputPath);

        // Remove quotes if the user is dumb and pasted a quoted path
        if (!userInputPath.empty() && userInputPath.front() == '"' && userInputPath.back() == '"') {
            userInputPath = userInputPath.substr(1, userInputPath.length() - 2);
        }

        if (fs::exists(userInputPath) && userInputPath.ends_with(".dll")) {
            return userInputPath;
        }
        else {
            std::cerr << "[!] Invalid path or file does not exist. Please try again." << std::endl;
        }
    }
}