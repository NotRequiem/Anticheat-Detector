#include "ipc.hpp"

// here rather than on header because otherwise u will get linker errors
const char* IPC_EVENT_NAME = "AnticheatDetectedEventACD";
const char* IPC_SHARED_MEM_NAME = "AnticheatNameSharedMemACD";
const int SHARED_MEM_SIZE = 256;

// ANSI escape codes for colors
const char* ANSI_COLOR_GREEN = "\033[92m";
const char* ANSI_COLOR_RESET = "\033[0m";

void listenForDetection() {
    // virtual terminal processing for colored output
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        std::cerr << "[-] Could not get standard output handle. Error: " << GetLastError() << std::endl;
        return;
    }
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) {
        std::cerr << "[-] Could not get console mode. Error: " << GetLastError() << std::endl;
        return;
    }
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, dwMode)) {
        std::cerr << "[-] Could not set console mode. Error: " << GetLastError() << std::endl;
    }

    std::cout << "[+] Shared memory listener started. Waiting for internal module..." << std::endl;

    HANDLE hEvent = NULL;
    const int max_retries = 10; // try for 5 seconds (10 * 500ms)
    int retries = 0;

    while (retries < max_retries) {
        hEvent = OpenEventA(SYNCHRONIZE, FALSE, IPC_EVENT_NAME);
        if (hEvent != NULL) {
            break;
        }
        retries++;
        Sleep(500);
    }

    if (hEvent == NULL) {
        std::cerr << "[-] Could not open IPC event after " << (max_retries * 500) / 1000 << " seconds. Injection may have failed or DLL is unresponsive. Error: " << GetLastError() << std::endl;
        std::cerr << "This can be normal to happen if the injector was too fast when injecting and ntdll worker threads were not spawned yet, please re-try. "<< std::endl;

        return;
    }

    std::cout << "[+] IPC channel with module established successfully. Listening for signal..." << std::endl;

    std::string lastPrintedAnticheat = "";

    while (true) {
        DWORD waitResult = WaitForSingleObject(hEvent, INFINITE);

        if (waitResult == WAIT_OBJECT_0) {
            HANDLE hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, IPC_SHARED_MEM_NAME);
            if (hMapFile) {
                LPVOID pBuf = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, SHARED_MEM_SIZE);
                if (pBuf) {
                    std::string currentDetection(static_cast<char*>(pBuf));

                    if (currentDetection != lastPrintedAnticheat && !currentDetection.empty()) {
                        // get the current time
                        auto now = std::chrono::system_clock::now();
                        auto in_time_t = std::chrono::system_clock::to_time_t(now);

                        // create a tm struct to hold the time info because otherwise MSVC WILL CRY A LOT
                        std::tm timeinfo = {};
                        localtime_s(&timeinfo, &in_time_t);

                        // Format and print the success message with color and timestamp
                        std::cout << "\n" << ANSI_COLOR_GREEN << "[SUCCESS] anticheat detected" << ANSI_COLOR_RESET << std::endl;
                        std::cout << "  > Detected: " << currentDetection << std::endl;
                        std::cout << "  > Time: " << std::put_time(&timeinfo, "%Y-%m-%d %X") << std::endl;

                        lastPrintedAnticheat = currentDetection;
                    }

                    UnmapViewOfFile(pBuf);
                }
                CloseHandle(hMapFile);
            }
            ResetEvent(hEvent);
        }
        else {
            std::cerr << "[-] Wait failed. Shutting down listener. Error: " << GetLastError() << std::endl;
            break;
        }
    }

    CloseHandle(hEvent);
}