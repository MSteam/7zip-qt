// QtExtractSettings.h
//
// Qt/Linux analogue of Common/ZipRegistry.cpp's NExtract registry store
// (milestone C.2). It mirrors NExtract::CInfo and its Save()/Load() semantics
// 1:1, but persists over QSettings instead of the Win32 registry.
//
// Upstream (ZipRegistry.cpp) keeps the extraction settings under
//   HKCU\Software\7-Zip\Extraction
// with the value names: ExtractMode, OverwriteMode, ShowPassword, Security
// (== NtSecurity), ElimDup, SplitDest, PathHistory (the output-path MRU).
//
// Here we keep the SAME logical fields under
//   QSettings("7-Zip", "7-Zip") -> group "Extraction"
// with key names mirroring the upstream value names. The struct keeps the
// 7-Zip idiom (a CInfo-like struct + Load()/Save(), CBoolPair fields, the same
// "_Force" booleans for PathMode/OverwriteMode), so that C.3 can add the
// NCompression equivalent next to it with the same shape.

#ifndef ZIP7_INC_QT_EXTRACT_SETTINGS_H
#define ZIP7_INC_QT_EXTRACT_SETTINGS_H

#include "../../../Common/MyTypes.h"
#include "../../../Common/MyString.h"

#include "../Common/ExtractMode.h"

// NExtractQt: the Qt-backed mirror of NExtract's registry store. Kept in its own
// namespace (not NExtract) so it does not collide with the engine's NExtract
// (ExtractMode.h) which is linked in via the engine archive.
namespace NExtractQt
{
  // Mirrors NExtract::CInfo (ZipRegistry.h). Same fields, same defaults, same
  // load/save behavior. The QSettings org/app/group constants live in the .cpp.
  struct CInfo
  {
    NExtract::NPathMode::EEnum PathMode;
    NExtract::NOverwriteMode::EEnum OverwriteMode;
    bool PathMode_Force;
    bool OverwriteMode_Force;

    CBoolPair SplitDest;
    CBoolPair ElimDup;
    CBoolPair NtSecurity;
    CBoolPair ShowPassword;

    UStringVector Paths; // output-path MRU history (newest first)

    void Save() const;
    void Load();
  };

  // G.9a : the Qt-backed mirror of NExtract::Save_LimitGB / Read_LimitGB
  // (ZipRegistry.cpp:132-138 / 188-196). Stored under [Extraction] "MemLimit"
  // (kMemLimit, byte-identical), the per-archive memory-usage GB limit that
  // CExtractCallbackImp::RequestMemoryUse / CSettingsPage consult. A stored value
  // of 0 or (UInt32)-1 means "no configured limit" (use the engine default);
  // Read_LimitGB returns (UInt32)-1 when nothing is stored (the original's
  // not-found sentinel).
  void   Save_LimitGB(UInt32 limit_GB);
  UInt32 Read_LimitGB();
}

#endif
