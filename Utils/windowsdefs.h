#pragma once

#include "typedefs.hpp"

#ifdef _WIN32
typedef void* HANDLE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef DWORD *LPDWORD;
typedef DWORD *PDWORD;
typedef void *LPVOID;
typedef int64_t LONG_PTR, *PLONG_PTR;
typedef uint64_t ULONG_PTR, *PULONG_PTR;
typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR;

typedef struct _SYSTEM_INFO {
    union {
        DWORD dwOemId;          // Obsolete field...do not use
        struct {
            WORD wProcessorArchitecture;
            WORD wReserved;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

extern "C" HANDLE GetStdHandle(int);
extern "C" int WriteConsoleA(HANDLE, const char*, int, int*, void*);
extern "C" bool GetConsoleMode(HANDLE hConsoleHandle, LPDWORD lpMode);
extern "C" bool SetConsoleMode(HANDLE hConsoleHandle, DWORD dwMode);
extern "C" bool GetProductInfo(DWORD dwOSMajorVersion, DWORD dwOSMinorVersion, DWORD dwSpMajorVersion, DWORD dwSpMinorVersion, PDWORD pdwReturnedProductType);
extern "C" void GetSystemInfo(LPSYSTEM_INFO lpSystemInfo);

const int STD_OUTPUT_HANDLE = -11;
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING  0x0004
#else
extern "C" long write(int, const char*, long);
#endif
