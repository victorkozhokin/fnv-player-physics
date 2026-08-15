#pragma once

// The handful of kernel32 imports this plugin needs, declared by hand.
//
// Pulling in <Windows.h> would tie the build to a Windows SDK installation for
// no benefit -- these few functions are the entire OS surface used here, and
// declaring them directly is what lets the plugin cross-compile from macOS.

#include "game/types.h"

using DWORD  = unsigned long;
using BOOL   = int;
using HANDLE = void*;

inline constexpr UInt32 PAGE_EXECUTE_READWRITE = 0x40;
inline constexpr UInt32 GENERIC_WRITE          = 0x40000000;
inline constexpr UInt32 FILE_SHARE_READ        = 0x00000001;
inline constexpr UInt32 CREATE_ALWAYS          = 2;
inline constexpr UInt32 FILE_ATTRIBUTE_NORMAL  = 0x80;

inline HANDLE const INVALID_HANDLE_VALUE = (HANDLE)(unsigned)-1;

// GET_FILEEX_INFO_LEVELS::GetFileExInfoStandard.
inline constexpr UInt32 kGetFileExInfoStandard = 0;

// WIN32_FILE_ATTRIBUTE_DATA, with the FILETIMEs spelled out as pairs of DWORDs
// so no Windows headers are needed to describe it.
struct FileAttributeData {
	DWORD attributes;
	DWORD creationLow,   creationHigh;
	DWORD lastAccessLow, lastAccessHigh;
	DWORD lastWriteLow,  lastWriteHigh;
	DWORD sizeHigh,      sizeLow;
};

extern "C" {

__declspec(dllimport) BOOL __stdcall VirtualProtect(
	void *address, UInt32 size, DWORD newProtect, DWORD *oldProtect);

__declspec(dllimport) HANDLE __stdcall CreateFileA(
	const char *fileName, DWORD access, DWORD shareMode, void *security,
	DWORD creation, DWORD flags, HANDLE templateFile);

__declspec(dllimport) BOOL __stdcall WriteFile(
	HANDLE file, const void *buffer, DWORD size, DWORD *written, void *overlapped);

__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE object);

__declspec(dllimport) BOOL __stdcall CreateDirectoryA(const char *path, void *security);

__declspec(dllimport) void __stdcall OutputDebugStringA(const char *text);

__declspec(dllimport) BOOL __stdcall GetFileAttributesExA(
	const char *fileName, UInt32 infoLevel, void *fileInformation);

__declspec(dllimport) UInt32 __stdcall GetPrivateProfileStringA(
	const char *section, const char *key, const char *defaultValue,
	char *out, UInt32 outSize, const char *fileName);

} // extern "C"
