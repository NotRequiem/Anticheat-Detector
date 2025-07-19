#pragma once

#include <windows.h>

// SHARED IPC CONSTANTS 
inline const char* IPC_EVENT_NAME = "AnticheatDetectedEventACD"; // IPC Handles & Names (Session-Local, No "Global\\") because then users would be forced to run minecraft as admin
inline const char* IPC_SHARED_MEM_NAME = "AnticheatNameSharedMemACD";
inline const int SHARED_MEM_SIZE = 256;

// SHARED GLOBAL HANDLES 
extern HANDLE g_hIpcEvent;
extern HANDLE g_hIpcMapFile;