#ifndef INJECTION_H
#define INJECTION_H

#include <windows.h>
#include <string>

bool injectDLL(DWORD processId, const std::string& dllPath);

#endif 