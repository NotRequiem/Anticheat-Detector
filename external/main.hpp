#pragma once

#include <iostream>
#include <windows.h>
#include <string>
#include <tlhelp32.h>
#include <filesystem>
#include <shlobj.h>

#include "utils.hpp"
#include "injection.hpp"
#include "ipc.hpp"
#include "process.hpp"

#pragma comment(lib, "Shell32.lib")

namespace fs = std::filesystem;

const char* IPC_EVENT = "Global\\AnticheatDetectedEventACD";
const char* IPC_SHARED = "Global\\AnticheatNameSharedMemACD";
const int SHARED_VIRTUAL_MEMORY = 256;