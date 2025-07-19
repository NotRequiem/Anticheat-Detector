#include "console.hpp" 

// it holds the keys to the console kingdom
static HANDLE g_hConsoleOutput = NULL;

// A simple getter. If you lose the handle, this function is your best friend
HANDLE GetConsoleOutputHandle() {
    return g_hConsoleOutput;
}

// Summons a console from our INFINITE VOID
void SpawnConsole() {
    AllocConsole(); // DOMAIN EXPANSION

    // Now we need to actually talk to it. "CONOUT$" is the magic incantation
    g_hConsoleOutput = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    // Basically we're telling printf and cout where to send their letters
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);

    SetConsoleTitleA("Anticheat Detector Console"); // every masterpiece needs a title
} 

// Puts the toys back in the box before Mom (the OS) gets angry
void DetachConsole() {
    // If the handle is valid, we gently close it :)
    if (g_hConsoleOutput != NULL && g_hConsoleOutput != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hConsoleOutput);
        g_hConsoleOutput = NULL; 
    }
    ::FreeConsole();
}

void Log(LogLevel level, const char* format, ...) {
    HANDLE hConsole = GetConsoleOutputHandle();
    if (hConsole == NULL || hConsole == INVALID_HANDLE_VALUE) {
        return;
    }

    char buffer[2048];
    va_list args; 
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args); 

    std::string prefix;
    WORD color;

    switch (level) {
    case INFO:    prefix = "[INFO] ";    color = FOREGROUND_BLUE | FOREGROUND_INTENSITY; break; // For your daily dose of "what's happening"
    case SUCCESS: prefix = "[SUCCESS] "; color = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break; // The sweet, sweet color of things not breaking (i had nightmates trying to debug my code)
    case FATAL:   prefix = "[FATAL] ";   color = FOREGROUND_RED | FOREGROUND_INTENSITY; break;   // Houston, we have a problem. Everything is on fire
    case DETAIL:  prefix = "  -> ";     color = FOREGROUND_GREEN | FOREGROUND_BLUE; break;      // For the micromanagers who need to know EVERYTHING
    default:      prefix = "[LOG] ";     color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break; // White. The "I guess it's a log?" color from Lunar or Badlion or whateva
    }

    std::string output = prefix + buffer + "\n";
    DWORD bytesWritten;

    // Set the text color, write the message, then set it back to boring white yep so we don't accidentally color everything that comes after
    SetConsoleTextAttribute(hConsole, color);
    WriteConsoleA(hConsole, output.c_str(), static_cast<DWORD>(output.length()), &bytesWritten, NULL);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}