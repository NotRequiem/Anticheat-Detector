#pragma once

#include <windows.h>
#include <iostream>
#include <string>
#include <cstdarg>
#include <cstdio>

enum LogLevel {
    INFO,
    DETAIL,
    SUCCESS,
    FATAL
};

HANDLE GetConsoleOutputHandle();

void Log(LogLevel level, const char* format, ...);

void DetachConsole(); // I cant name it FreeConsole because thats a winapi function that doesnt accept overloading

void SpawnConsole();