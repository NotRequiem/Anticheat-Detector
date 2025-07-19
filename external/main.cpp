#include "main.hpp"

int main() {
    std::string dllName = "anticheat_detector.dll";
    std::string dllPath = findOrPromptForDllPath(dllName);

    std::wstring processName = L"javaw.exe";
    DWORD processId = 0;

    std::cout << "[+] Open Minecraft..." << std::endl;

    processId = getProcessIdByName(processName);
    if (processId != 0) {
        std::cout << "[-] Minecraft is already running. Restart it to start the injection." << std::endl;
        while (getProcessIdByName(processName) != 0) {
            SleepEx(1000, FALSE);
        }
        std::cout << "[+] Nice you closed Minecraft :o" << std::endl;
    }

    while (true) {
        processId = getProcessIdByName(processName);
        if (processId != 0) {
            std::cout << "[+] Injecting into " << processId << "'s virtual address space" << std::endl;
            if (injectDLL(processId, dllPath)) {
                std::cout << "[!] IMPORTANT: DONT CLOSE THE NEW CONSOLE, closing the window will make Windows issue a CNTRL + C command to Minecraft, effectively terminating it." << std::endl;
                listenForDetection();
                break;
            }
            else {
                std::cerr << "[!] Halting due to injection failure." << std::endl;
                break;
            }
        }
        SleepEx(1000, FALSE);
    }

    std::cout << "\nProgram finished. Press Enter to exit." << std::endl;
    std::cin.get();
    return 0;
}