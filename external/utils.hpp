#ifndef UTILS_H
#define UTILS_H

#include <string>

// Searches for the DLL in the app's directory and other common locations,
// then prompts the user if not found
std::string findOrPromptForDllPath(const std::string& dllName);

// Gets the full path to the user's Downloads folder
std::string getDownloadsPath();

#endif // UTILS_H