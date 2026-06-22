// WorkDirSettings.cpp
// ----------------------------------------------------------------------------
// B.5 write-path Linux compat (additive; no engine source touched).
//
// The engine's CPP/7zip/UI/Common/WorkDir.cpp is now compiled into
// sevenzip_agent so CWorkDirTempFile / GetWorkDir back the in-place archive
// rewrite in ArchiveFolderOut.cpp (CommonUpdateOperation builds the new archive
// in a temp file, then MoveToOriginal()s it over the original). WorkDir.cpp's
// CWorkDirTempFile::CreateTempFile() calls NWorkDir::CInfo::Load() once, to pick
// the temp-file directory. On Windows that reads HKCU\Software\7-Zip\Options
// (WorkDirType / WorkDirPath / TempRemovableOnly); its only definition lives in
// the Win32-registry CPP/7zip/UI/Common/ZipRegistry.cpp, which is excluded from
// the Linux build. We supply the one otherwise-missing symbol here, mirroring
// the upstream DEFAULT exactly:
//
//   CInfo::SetDefault() (inline, ZipRegistry.h) sets Mode = NWorkDir::NMode::
//   kSystem (use the system temp folder) and ForRemovableOnly = true.
//
// With kSystem, GetWorkDir() routes through NDir::MyGetTempPath() -> "/tmp/" on
// Linux (Windows/FileDir.cpp) - the semantic equivalent of the Win32 system-temp
// default (GetTempPathW) when no registry value is set.
//
// A future B.8 Options dialog can overlay a QSettings-stored work directory by
// writing NWorkDir::CInfo before CreateTempFile; EXTEND this Load() rather than
// adding a second definition (which would be a duplicate-symbol error). If
// ZipRegistry.cpp is ever added to the Linux build it must stay mutually
// exclusive with this shim for the same reason.
// ----------------------------------------------------------------------------

#include "../../Agent/StdAfx.h"

#include "../../Common/ZipRegistry.h"

// B.8 : the Options "Folders" tab persists a work dir in QSettings ([Options]
// WorkDirUseSystemTemp / WorkDirPath). sevenzip_agent must NOT link Qt, so the
// FM (which CAN read QSettings) hands the chosen dir to the engine through this
// tiny C-ABI setter; CInfo::Load() then copies it. Defaults to SetDefault()
// (kSystem -> /tmp) when the FM never set anything, preserving the prior
// behaviour exactly. This stays the SINGLE definition of NWorkDir::CInfo::Load()
// in the Linux build (mutually exclusive with the excluded Win32 ZipRegistry.cpp,
// per the header comment above).

namespace {
// File-static overlay, defaulted to "unset" (use system temp). Plain globals are
// fine here: the FM is single-threaded for option changes, and CreateTempFile
// reads this on the op thread after the FM set it on the GUI thread.
//
// G.9c : g_WorkDir_Mode carries the full NWorkDir::NMode (kSystem=0/kCurrent=1/
// kSpecified=2), so the FM can now select "Current" (the archive's own dir). Only
// kSpecified consumes g_WorkDir_Path; kSystem and kCurrent ignore it.
bool   g_WorkDir_HasOverlay = false;
int    g_WorkDir_Mode = NWorkDir::NMode::kSystem;
FString g_WorkDir_Path;
}

// C-ABI setter the FM calls at startup (and after Options is accepted). `mode` is
// the NWorkDir::NMode value (0=kSystem, 1=kCurrent, 2=kSpecified). The path is
// consumed only for kSpecified; for kSystem/kCurrent it is ignored and Load()
// uses the engine's own routing (system temp / the archive's own dir).
extern "C" void Qt_SetWorkDir(const wchar_t *path, int mode)
{
  g_WorkDir_HasOverlay = true;
  g_WorkDir_Mode = mode;
  g_WorkDir_Path = path ? us2fs(UString(path)) : FString();
}

// G.9c/G.9e test hook : report the EFFECTIVE NWorkDir::NMode after a Load() — i.e.
// what the engine would use for the in-place-rewrite temp dir given the current
// overlay. Proves the full chain (Qt_SetWorkDir -> CInfo::Load) without touching a
// real archive. Defined here so it shares the file-static overlay above.
extern "C" int Qt_GetEffectiveWorkDirMode()
{
  NWorkDir::CInfo info;
  info.Load();
  return (int)info.Mode;
}

namespace NWorkDir {

void CInfo::Load()
{
  SetDefault();   // kSystem, empty path, ForRemovableOnly = true
  if (!g_WorkDir_HasOverlay)
    return;
  switch (g_WorkDir_Mode)
  {
    case NMode::kCurrent:
      // Put the temp file in the archive's OWN directory (GetWorkDir routes a
      // kCurrent mode through the archive's parent path). No Path needed.
      Mode = NMode::kCurrent;
      break;
    case NMode::kSpecified:
      // Only honor a specified path when one is actually set; an empty path falls
      // back to the kSystem default (SetDefault above), matching the prior shim.
      if (!g_WorkDir_Path.IsEmpty())
      {
        Mode = NMode::kSpecified;
        Path = g_WorkDir_Path;
      }
      break;
    default:
      break;  // kSystem (or any unexpected value): keep SetDefault()
  }
}

}
