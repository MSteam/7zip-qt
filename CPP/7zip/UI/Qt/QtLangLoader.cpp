// QtLangLoader.cpp
// ----------------------------------------------------------------------------
// P.2 i18n : the CLang loader + accessors, for GUI targets that DON'T link the
// agent lib (e.g. the unified archiver `7zqt`). It provides g_QtLang +
// QtLang_LoadFile / QtLang_IsLoaded / QtLang_Get — the SAME surface
// agent/AgentLinuxCompat.cpp provides for the FM/agent-linked targets, MINUS the
// NWindows::MyLoadString un-stub (7zqt has no MyLoadString callers; its dialogs
// call FmLang/QtLang_Get directly). The two TUs are never linked together
// (7zqt_fm links the agent lib; 7zqt links this), so there is exactly one
// g_QtLang per binary.
//
// This is Qt-side glue, not an engine source — CPP/Common/Lang.cpp (the CLang
// parser) is compiled UNCHANGED.
// ----------------------------------------------------------------------------

#ifndef _WIN32

#include "../../../Common/MyString.h"
#include "../../../Common/Lang.h"

namespace NWindowsQtLang {
  CLang g_QtLang;
}

bool QtLang_LoadFile(const FString &txtPath)
{
  return NWindowsQtLang::g_QtLang.Open(txtPath, "7-Zip");
}

bool QtLang_IsLoaded()
{
  return !NWindowsQtLang::g_QtLang.IsEmpty();
}

UString QtLang_Get(unsigned id)
{
  const wchar_t *s = NWindowsQtLang::g_QtLang.Get((UInt32)id);
  return s ? UString(s) : UString();
}

#endif // !_WIN32
