// QtOpenArcError.h
// ----------------------------------------------------------------------------
// G.3c : open-archive error-flag decoding.
//
// Header-declared, reusable port of FileManager/ExtractCallback.cpp's
//   UString GetOpenArcErrorMessage(UInt32 errorFlags)   (~:473-510)
// which decodes a kpidErrorFlags / kpidWarningFlags bitmask (kpv_ErrorFlags_*)
// into the human-readable, per-bit prose ("Is not archive", "Headers Error",
// "Unexpected end of data", "Encrypted Headers Error : Wrong password?", ...).
//
// FileManager/ExtractCallback.cpp is NOT in the Qt CMake build, so the original
// definition is unavailable to link against; this TU re-homes it (mirroring the
// original body bit-for-bit, with the engine's LangString(id)/AddLangString
// swapped for the port's FmLang(id, <inline .rc English>) precedence exactly as
// QtExtractCallback.cpp's Qt_SetExtractErrorMessage does). The inline literals
// are the .rc STRINGTABLE text (GUI/Extract.rc), so nothing is invented.
//
// Callers (G.3c):
//   * QtPropertiesDialog.cpp   — render kpidErrorFlags/kpidWarningFlags as text
//   * QtExtractCallback.cpp     — OpenResult: decoded error/warning per level
//   * QtUpdateCallback.cpp      — OpenResult: decoded error/warning per level
//   * the G.3 open-path corrupt-archive dialog
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_OPEN_ARC_ERROR_H
#define ZIP7_INC_QT_OPEN_ARC_ERROR_H

#include "../../../Common/MyString.h"
#include "../../../Common/MyTypes.h"

// Decode a kpv_ErrorFlags_* bitmask to multi-line prose. Mirrors
// FileManager/ExtractCallback.cpp GetOpenArcErrorMessage: one LF-separated line
// per recognised bit (with the EncryptedHeadersError " : Wrong password?"
// suffix), then any unrecognised residue as a "0x...." hex tail. Empty when
// errorFlags == 0.
UString Qt_GetOpenArcErrorMessage(UInt32 errorFlags);

#endif
