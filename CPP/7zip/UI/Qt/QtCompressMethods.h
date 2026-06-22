// QtCompressMethods.h
//
// VERBATIM replica of the method / format lookup tables and the EMethodID enum
// from GUI/CompressDialog.cpp (milestone C.3b-1).
//
// WHY REPLICATED (not reused): these tables are file-static (in an anonymous /
// translation-unit scope) inside GUI/CompressDialog.cpp, which cannot be compiled
// under Qt/Linux (it includes Windows/Control/ComboBox.h -> <CommCtrl.h>). The
// tables themselves are pure data and the EMethodID enum is pure declaration, so
// (exactly as we did for NCompressDialog::CInfo in QtCompressInfo.h) we lift them
// here VERBATIM. They encode the exact per-format method sets, level masks and the
// format-capability flags the Qt dialog's setMethod/setDictionary/setOrder logic
// indexes against; keeping them byte-identical to the original guarantees the Qt
// Method/Dictionary/Order combos offer the same choices the upstream dialog does.
//
// The bytes between the "=== verbatim from CompressDialog.cpp ===" markers are
// copied EXACTLY from CPP/7zip/UI/GUI/CompressDialog.cpp (lines 124-364), only
// moved into a header (the file-static linkage becomes `inline` so the tables can
// live in a header shared by QtCompressDialog.cpp). Do not "improve" them; fidelity
// is the point. The diff (modulo `static`->`inline` and the kCopy/kLZMA name
// qualification needed to share them) is documented in the milestone report.

#ifndef ZIP7_INC_QT_COMPRESS_METHODS_H
#define ZIP7_INC_QT_COMPRESS_METHODS_H

#include "../../../Common/MyTypes.h"   // UInt32
#include "../../../../C/7zTypes.h"     // (Z7_ARRAY_SIZE via MyTypes chain) - harmless

#ifndef Z7_ARRAY_SIZE
#define Z7_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

// LPCSTR is a Windows typedef; on Linux the engine's Common headers do not define
// it, so the tables use `const char * const` directly (the original's `LPCSTR`
// expands to exactly that). Documented as the sole verbatim deviation forced by
// the platform, alongside static->inline.
namespace NCompressMethods {

// =========================================================================
// === verbatim from GUI/CompressDialog.cpp (lines 124-364) ================
// =========================================================================

enum EMethodID
{
  kCopy,
  kLZMA,
  kLZMA2,
  kPPMd,
  kBZip2,
  kDeflate,
  kDeflate64,
  kPPMdZip,
  // kZSTD,
  kSha256,
  kSha1,
  kCrc32,
  kCrc64,
  kGnu,
  kPosix
};

inline const char * const kMethodsNames[] =
{
    "Copy"
  , "LZMA"
  , "LZMA2"
  , "PPMd"
  , "BZip2"
  , "Deflate"
  , "Deflate64"
  , "PPMd"
  // , "ZSTD"
  , "SHA256"
  , "SHA1"
  , "CRC32"
  , "CRC64"
  , "GNU"
  , "POSIX"
};

inline const EMethodID g_7zMethods[] =
{
  kLZMA2,
  kLZMA,
  kPPMd,
  kBZip2
  , kDeflate
  , kDeflate64
  // , kZSTD
  , kCopy
};

inline const EMethodID g_7zSfxMethods[] =
{
  kCopy,
  kLZMA,
  kLZMA2,
  kPPMd
};

inline const EMethodID g_ZipMethods[] =
{
  kDeflate,
  kDeflate64,
  kBZip2,
  kLZMA,
  kPPMdZip
  // , kZSTD
};

inline const EMethodID g_GZipMethods[] =
{
  kDeflate
};

inline const EMethodID g_BZip2Methods[] =
{
  kBZip2
};

inline const EMethodID g_XzMethods[] =
{
  kLZMA2
};

/*
static const EMethodID g_ZstdMethods[] =
{
  kZSTD
};
*/

/*
static const EMethodID g_SwfcMethods[] =
{
  kDeflate
  // kLZMA
};
*/

inline const EMethodID g_TarMethods[] =
{
  kGnu,
  kPosix
};

inline const EMethodID g_HashMethods[] =
{
    kSha256
  , kSha1
  // , kCrc32
  // , kCrc64
};

inline const UInt32 kFF_Filter      = 1 << 0;
inline const UInt32 kFF_Solid       = 1 << 1;
inline const UInt32 kFF_MultiThread = 1 << 2;
inline const UInt32 kFF_Encrypt     = 1 << 3;
inline const UInt32 kFF_EncryptFileNames  = 1 << 4;
inline const UInt32 kFF_MemUse      = 1 << 5;
inline const UInt32 kFF_SFX         = 1 << 6;

/*
static const UInt32 kFF_Time_Win  = 1 << 10;
static const UInt32 kFF_Time_Unix = 1 << 11;
static const UInt32 kFF_Time_DOS  = 1 << 12;
static const UInt32 kFF_Time_1ns  = 1 << 13;
*/

struct CFormatInfo
{
  const char * Name;
  UInt32 LevelsMask;
  unsigned NumMethods;
  const EMethodID *MethodIDs;

  UInt32 Flags;

  bool Filter_() const { return (Flags & kFF_Filter) != 0; }
  bool Solid_() const { return (Flags & kFF_Solid) != 0; }
  bool MultiThread_() const { return (Flags & kFF_MultiThread) != 0; }
  bool Encrypt_() const { return (Flags & kFF_Encrypt) != 0; }
  bool EncryptFileNames_() const { return (Flags & kFF_EncryptFileNames) != 0; }
  bool MemUse_() const { return (Flags & kFF_MemUse) != 0; }
  bool SFX_() const { return (Flags & kFF_SFX) != 0; }
};

#define METHODS_PAIR(x) Z7_ARRAY_SIZE(x), x

inline const CFormatInfo g_Formats[] =
{
  {
    "",
    // (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
    ((UInt32)1 << 10) - 1,
    // (UInt32)(Int32)-1,
    0, NULL,
    kFF_MultiThread | kFF_MemUse
  },
  {
    "7z",
    // (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
    (1 << 10) - 1,
    METHODS_PAIR(g_7zMethods),
    kFF_Filter | kFF_Solid | kFF_MultiThread | kFF_Encrypt |
    kFF_EncryptFileNames | kFF_MemUse | kFF_SFX
    // | kFF_Time_Win
  },
  {
    "Zip",
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
    METHODS_PAIR(g_ZipMethods),
    kFF_MultiThread | kFF_Encrypt | kFF_MemUse
    // | kFF_Time_Win | kFF_Time_Unix | kFF_Time_DOS
  },
  {
    "GZip",
    (1 << 1) | (1 << 5) | (1 << 7) | (1 << 9),
    METHODS_PAIR(g_GZipMethods),
    kFF_MemUse
    // | kFF_Time_Unix
  },
  {
    "BZip2",
    (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
    METHODS_PAIR(g_BZip2Methods),
    kFF_MultiThread | kFF_MemUse
  },
  {
    "xz",
    // (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
    (1 << 10) - 1 - (1 << 0), // store (1 << 0) is not supported
    METHODS_PAIR(g_XzMethods),
    kFF_Solid | kFF_MultiThread | kFF_MemUse
  },
  /*
  {
    "zstd",
    // (1 << (MY_ZSTD_LEVEL_MAX + 1)) - 1,
    (1 << (9 + 1)) - 1,
    METHODS_PAIR(g_ZstdMethods),
    // kFF_Solid |
    kFF_MultiThread
    | kFF_MemUse
  },
  */
/*
  {
    "Swfc",
    (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
    METHODS_PAIR(g_SwfcMethods),
    0
  },
*/
  {
    "Tar",
    (1 << 0),
    METHODS_PAIR(g_TarMethods),
    0
    // kFF_Time_Unix | kFF_Time_Win // | kFF_Time_1ns
  },
  {
    "wim",
    (1 << 0),
    0, NULL,
    0
    // | kFF_Time_Win
  },
  {
    "Hash",
    (0 << 0),
    METHODS_PAIR(g_HashMethods),
    0
  }
};

inline bool IsMethodSupportedBySfx(int methodID)
{
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_7zSfxMethods); i++)
    if (methodID == g_7zSfxMethods[i])
      return true;
  return false;
}

// =========================================================================
// === end verbatim ========================================================
// =========================================================================

} // namespace NCompressMethods

#endif
