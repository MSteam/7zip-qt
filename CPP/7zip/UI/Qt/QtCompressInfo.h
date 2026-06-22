// QtCompressInfo.h
//
// VERBATIM replica of NCompressDialog::CInfo + NCompressDialog::NUpdateMode from
// GUI/CompressDialog.h (milestone C.3a).
//
// WHY REPLICATED (not reused): the upstream GUI/CompressDialog.h cannot be
// compiled under Qt/Linux because it includes Windows/Control/ComboBox.h and
// Windows/Control/Edit.h, which pull in <CommCtrl.h> (a Win32-only header). The
// CInfo struct itself, however, is pure data (UString / enums / CBoolPair /
// CRecordVector / NCompression::CMemUse) and has no Win32 dependency. So, exactly
// as we did for CProgressSync, we lift JUST the data contract here verbatim. This
// is the OUTPUT data contract the Qt compress dialog produces and the
// CInfo->CUpdateOptions translation consumes; keeping it byte-identical to the
// original guarantees the translation in QtCompressGUI.cpp matches UpdateGUI.cpp.
//
// The bytes below the "=== verbatim from CompressDialog.h ===" marker are copied
// exactly from CPP/7zip/UI/GUI/CompressDialog.h (lines 18-105), only re-indented
// for this file. Do not "improve" them; fidelity is the point.
//
// C.3a SCOPE NOTE: the Qt dialog populates only the CORE fields
//   UpdateMode, PathMode, Level, Method(empty=auto), FormatIndex, ArcPath,
//   Password, EncryptionMethod, EncryptHeadersIsAllowed/EncryptHeaders.
// The ADVANCED fields are left at the CInfo() ctor's auto/default sentinels
//   Dict64 = -1, Order = -1, NumThreads = -1, Level = -1 (until set),
//   SolidIsSpecified = false, MemUsage default, VolumeSizes empty, the time/link
//   CBoolPairs default.
// The engine then auto-derives method/dictionary/solid/threads/memory from Level
// (exactly like `7zz a -mx=N`). Those auto fields are the C.3b UI surface.

#ifndef ZIP7_INC_QT_COMPRESS_INFO_H
#define ZIP7_INC_QT_COMPRESS_INFO_H

#include "../../../Common/MyString.h"
#include "../../../Common/MyTypes.h" // CBoolPair

#include "../../../Common/Wildcard.h"     // NWildcard::ECensorPathMode, k_RelatPath
#include "../Common/ZipRegistry.h"        // NCompression::CMemUse

// =========================================================================
// === verbatim from GUI/CompressDialog.h (lines 18-105) ===================
// =========================================================================
namespace NCompressDialog
{
  namespace NUpdateMode
  {
    enum EEnum
    {
      kAdd,
      kUpdate,
      kFresh,
      kSync
    };
  }

  struct CInfo
  {
    NUpdateMode::EEnum UpdateMode;
    NWildcard::ECensorPathMode PathMode;

    bool SolidIsSpecified;
    // bool MultiThreadIsAllowed;
    UInt64 SolidBlockSize;
    UInt32 NumThreads;

    NCompression::CMemUse MemUsage;

    CRecordVector<UInt64> VolumeSizes;

    UInt32 Level;
    UString Method;
    UInt64 Dict64;
    // UInt64 Dict64_Chain;
    bool OrderMode;
    UInt32 Order;
    UString Options;

    UString EncryptionMethod;

    bool SFXMode;
    bool OpenShareForWrite;
    bool DeleteAfterCompressing;

    CBoolPair SymLinks;
    CBoolPair HardLinks;
    CBoolPair AltStreams;
    CBoolPair NtSecurity;

    CBoolPair PreserveATime;

    UInt32 TimePrec;
    CBoolPair MTime;
    CBoolPair CTime;
    CBoolPair ATime;
    CBoolPair SetArcMTime;

    UString ArcPath; // in: Relative or abs ; out: Relative or abs

    // FString CurrentDirPrefix;
    bool KeepName;

    bool GetFullPathName(UString &result) const;

    int FormatIndex;

    UString Password;
    bool EncryptHeadersIsAllowed;
    bool EncryptHeaders;

    CInfo():
        UpdateMode(NCompressDialog::NUpdateMode::kAdd),
        PathMode(NWildcard::k_RelatPath),
        SFXMode(false),
        OpenShareForWrite(false),
        DeleteAfterCompressing(false),
        FormatIndex(-1)
    {
      Level = Order = (UInt32)(Int32)-1;
      NumThreads = (UInt32)(Int32)-1;
      SolidIsSpecified = false;
      Dict64 = (UInt64)(Int64)(-1);
      // Dict64_Chain = (UInt64)(Int64)(-1);
      OrderMode = false;
      Method.Empty();
      Options.Empty();
      EncryptionMethod.Empty();
      TimePrec = (UInt32)(Int32)(-1);
      // C.3a additions to the verbatim ctor (fields the original initializes
      // elsewhere in OnInit; we default them here so a CInfo is fully valid):
      EncryptHeadersIsAllowed = false;
      EncryptHeaders = false;
      KeepName = false;
      SolidBlockSize = 0;
    }
  };
}
// =========================================================================
// === end verbatim ========================================================
// =========================================================================

#endif
