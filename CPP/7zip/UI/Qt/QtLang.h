// QtLang.h
// ----------------------------------------------------------------------------
// P.2 i18n : the Qt-side helper over 7-Zip's OWN CLang loader.
//
// 7-Zip i18n = CPP/Common/Lang.cpp (CLang) reading a Lang/*.txt UTF-8 file keyed
// by NUMERIC ids — the SAME IDS_*/IDM_* ids the community translation corpus
// uses (FileManager/resource.h, GUI/*Res.h). The .txt precedence is exactly the
// upstream LangString() (LangUtils.cpp:137): loaded txt wins, else the .rc
// built-in English.
//
// On Linux there is no compiled .rc STRINGTABLE, so the "English built-in" is
// carried INLINE at each call site as a literal (which IS the .rc text). FmLang()
// implements the same precedence:
//
//     FmLang(id, "English")  ==  g_QtLang.Get(id), else "English"
//
// where g_QtLang is the active translation (loaded by QtLang_LoadFile, empty =>
// English). This is byte-equivalent to upstream LangString(id) with the .rc
// STRINGTABLE entry as fallback — no invented strings.
//
// `id` MUST be the ORIGINAL numeric IDS_*/IDM_* id (see the tables in
// FileManager/resource.h) so a community Lang/*.txt matches unchanged.
//
// Header-only: the global + loader live in agent/AgentLinuxCompat.cpp (linked
// into sevenzip_agent, one definition program-wide). MyLoadString is the
// un-stubbed bridge declared in Windows/ResourceString.h.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_LANG_H
#define ZIP7_INC_QT_LANG_H

#include "../../../Common/MyString.h"

#include <QtCore/QString>

// Load a Lang/*.txt (CLang::Open with the "7-Zip" id-0 guard). false => the file
// was absent / had a bad signature / wrong id-0 => g_QtLang stays empty (English).
// Defined in agent/AgentLinuxCompat.cpp (linked into sevenzip_agent).
bool QtLang_LoadFile(const FString &txtPath);
bool QtLang_IsLoaded();

// Win32-type-free accessor for the loaded translation (so this header needn't pull
// in HINSTANCE/UINT). Returns the txt entry for `id`, or empty when absent. This
// is the SAME g_QtLang the un-stubbed NWindows::MyLoadString(id) consults — so
// FmLang(id, english) and the engine's LangString(id) resolve identically.
UString QtLang_Get(unsigned id);

// Translation-or-English-fallback. id = the ORIGINAL IDS_*/IDM_* numeric id;
// english = the inline literal that equals the .rc STRINGTABLE / dialog-template
// text. Returns the loaded translation when the txt carries this id, else english.
inline QString FmLang(unsigned id, const QString &english)
{
  const UString s = QtLang_Get(id);   // empty when no/!matching txt
  if (s.IsEmpty())
    return english;
  return QString::fromWCharArray(s.Ptr(), (int)s.Len());
}

#endif
