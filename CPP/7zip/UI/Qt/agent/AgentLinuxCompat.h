// AgentLinuxCompat.h
// ----------------------------------------------------------------------------
// Milestone B.0 : Linux build-compat shim for the 7-Zip Agent layer
// (CPP/7zip/UI/Agent/*).
//
// The Agent layer (CAgent / CAgentFolder / IFolderFolder) is pure COM over the
// engine and has ZERO Win32-GUI coupling, but it was written for the Win32
// build and references a handful of symbols that the Linux COM shim
// (CPP/Common/MyWindows.h, which only pulls in C/7zWindows.h under _WIN32) does
// not provide. This header supplies EXACTLY those missing symbols so the Agent
// sources compile UNCHANGED on Linux.
//
// It is force-included into every Agent translation unit via the CMake
// `-include` compile option (see CPP/7zip/UI/Qt/agent/CMakeLists fragment).
// It deliberately does NOTHING on _WIN32 (the real Windows headers win there).
//
// Gaps resolved here (all ADDITIVE - no engine source is edited by this file):
//
//   1. INVALID_FILE_ATTRIBUTES
//        Used by Agent.h:253 (CAgent::Is_Attrib_ReadOnly). Defined on Windows
//        in C/7zWindows.h:69 as ((DWORD)-1), but that header is _WIN32-only
//        (see CPP/Common/MyWindows.h). We mirror the C/7zWindows.h:69 define
//        VERBATIM below.
//
//   2. HINSTANCE
//        Referenced by Windows/ResourceString.h (pulled in by
//        Agent/ArchiveFolderOpen.cpp) and by `extern HINSTANCE g_hInstance;`.
//        On Windows it is a real handle type; the portable Linux shim already
//        models the sibling handle type HMODULE as `void *`
//        (CPP/Windows/DLL.h:9). We model HINSTANCE the same way.
//
//   3. UINT64
//        Agent/UpdateCallbackAgent.cpp defines its IUpdateCallbackUI methods
//        with the parameter type UINT64 (SetTotal(UINT64), SetCompleted(const
//        UINT64*)). On Windows UINT64 is a Windows-SDK type; the Linux shim
//        only provides UInt64 (C/7zTypes.h). The interface itself is declared
//        with UInt64, so the two spellings are meant to be the same 64-bit
//        unsigned type. We map UINT64 -> UInt64 so the .cpp definitions match
//        the IUpdateCallbackUI declarations exactly, with no source edit.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_AGENT_LINUX_COMPAT_H
#define ZIP7_INC_QT_AGENT_LINUX_COMPAT_H

#ifndef _WIN32

// Pull in the portable COM/type shim first so DWORD / UInt64 / etc. are defined
// before we use them below. MyWindows.h transitively includes C/7zTypes.h
// (which typedefs UINT, DWORD, ULONG) on non-_WIN32 builds.
#include "../../../../Common/MyWindows.h"

// --- Gap 1: INVALID_FILE_ATTRIBUTES (verbatim mirror of C/7zWindows.h:69) ---
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

// --- Gap 2: HINSTANCE / HMODULE (mirror the HMODULE = void* model from -------
//            Windows/DLL.h:9). Agent.h:335 declares
//            CCodecIcons::LoadIcons(HMODULE m), and ResourceString.h /
//            ArchiveFolderOpen.cpp reference HINSTANCE / `extern HINSTANCE
//            g_hInstance`. On Windows both are real handle types from windows.h;
//            the portable Linux build models HMODULE as `void *` (DLL.h). We
//            model both the same way. Windows/DLL.h is included indirectly by
//            the Agent sources, which also typedefs HMODULE as `void *` under
//            the SAME `#ifndef _WIN32` guard, so the two definitions agree; the
//            typedef-once guard below keeps a duplicate typedef harmless.
#ifndef Z7_QT_AGENT_WIN_HANDLES_DEFINED
#define Z7_QT_AGENT_WIN_HANDLES_DEFINED
typedef void *HINSTANCE;
#ifndef ZIP7_INC_WINDOWS_DLL_H   // if Windows/DLL.h already typedef'd HMODULE
typedef void *HMODULE;
#endif
#endif

// --- Gap 3: UINT64 -> UInt64 (so UpdateCallbackAgent.cpp matches its iface) --
#ifndef UINT64
#define UINT64 UInt64
#endif

#endif // !_WIN32

#endif
