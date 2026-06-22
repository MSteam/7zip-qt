// QtOpenArcError.cpp
//
// G.3c : faithful port of FileManager/ExtractCallback.cpp's open-archive
// error-flag decoder. See QtOpenArcError.h.
//
// FileManager/ExtractCallback.cpp is not compiled into the Qt build, so this TU
// re-homes the GetOpenArcErrorMessage logic. The original walks k_ErrorFlagsIds[]
// (indexed by bit position), mapping each set kpv_ErrorFlags_* bit to a
// LangString(IDS_*) line; this port mirrors that table and loop bit-for-bit,
// resolving each id via FmLang(id, <inline .rc English>) — the same
// translation-or-English-fallback precedence the rest of the Qt port uses (cf.
// QtExtractCallback.cpp Qt_SetExtractErrorMessage). The inline literals are the
// GUI/Extract.rc STRINGTABLE text verbatim, so nothing is invented.

#include "QtOpenArcError.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include "../../../Common/IntToString.h"
#include "../../../Common/StringConvert.h"

#include "../../PropID.h"   // kpv_ErrorFlags_EncryptedHeadersError

#include "QtLang.h"          // FmLang(id, english)
#include "../GUI/ExtractRes.h" // IDS_EXTRACT_MSG_* / IDS_OPEN_MSG_* langIDs

// k_ErrorFlagsIds[] — copied VERBATIM from FileManager/ExtractCallback.cpp:452.
// Indexed by bit position i so that (1 << i) is the kpv_ErrorFlags_* bit; the
// duplicate IDS_EXTRACT_MSG_HEADERS_ERROR at i==1,2 is intentional in the
// original (kpv_ErrorFlags_HeadersError and kpv_ErrorFlags_EncryptedHeadersError
// share the "Headers Error" base text, the latter gets the " : Wrong password?"
// suffix appended below).
static const UInt32 k_ErrorFlagsIds[] =
{
  IDS_EXTRACT_MSG_IS_NOT_ARC,
  IDS_EXTRACT_MSG_HEADERS_ERROR,
  IDS_EXTRACT_MSG_HEADERS_ERROR,
  IDS_OPEN_MSG_UNAVAILABLE_START,
  IDS_OPEN_MSG_UNCONFIRMED_START,
  IDS_EXTRACT_MSG_UEXPECTED_END,
  IDS_EXTRACT_MSG_DATA_AFTER_END,
  IDS_EXTRACT_MSG_UNSUPPORTED_METHOD,
  IDS_OPEN_MSG_UNSUPPORTED_FEATURE,
  IDS_EXTRACT_MSG_DATA_ERROR,
  IDS_EXTRACT_MSG_CRC_ERROR
};

// Inline .rc English for each id (GUI/Extract.rc STRINGTABLE), same index order
// as k_ErrorFlagsIds[]. FmLang(id, english) returns the loaded translation when a
// Lang/*.txt carries the id, else this literal (== the .rc fallback) — identical
// precedence to the engine's LangString(id).
static const char * const k_ErrorFlagsEnglish[] =
{
  "Is not archive",
  "Headers Error",
  "Headers Error",
  "Unavailable start of archive",
  "Unconfirmed start of archive",
  "Unexpected end of data",
  "There are some data after the end of the payload data",
  "Unsupported compression method",
  "Unsupported feature",
  "Data error",
  "CRC failed"
};

// Port helper: FmLang(id, english) -> UString (UTF-8 -> wide), matching the way
// QtExtractCallback.cpp marshals the FmLang result into a UString.
static UString FmLangU(unsigned id, const char *english)
{
  const QByteArray u8 = FmLang(id, QString::fromUtf8(english)).toUtf8();
  return GetUnicodeString(AString(u8.constData()));
}

// Faithful mirror of GetOpenArcErrorMessage (FileManager/ExtractCallback.cpp:474):
// one LF-separated line per set bit, the EncryptedHeadersError suffix, and the
// unrecognised-residue hex tail.
UString Qt_GetOpenArcErrorMessage(UInt32 errorFlags)
{
  UString s;

  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_ErrorFlagsIds); i++)
  {
    const UInt32 f = (UInt32)1 << i;
    if ((errorFlags & f) == 0)
      continue;
    UString m = FmLangU(k_ErrorFlagsIds[i], k_ErrorFlagsEnglish[i]);
    if (m.IsEmpty())
      continue;
    if (f == kpv_ErrorFlags_EncryptedHeadersError)
    {
      m += " : ";
      m += FmLangU(IDS_EXTRACT_MSG_WRONG_PSW_GUESS, "Wrong password?");
    }
    if (!s.IsEmpty())
      s.Add_LF();
    s += m;
    errorFlags &= ~f;
  }

  if (errorFlags != 0)
  {
    char sz[16];
    sz[0] = '0';
    sz[1] = 'x';
    ConvertUInt32ToHex(errorFlags, sz + 2);
    if (!s.IsEmpty())
      s.Add_LF();
    s += sz;
  }

  return s;
}
