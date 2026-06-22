// AgentLinuxCompat.cpp
// ----------------------------------------------------------------------------
// Milestone B.0 : Linux link-compat definitions for the 7-Zip Agent layer.
//
// The Agent sources reference two symbols that only the Win32 build provides at
// link time. This TU supplies faithful, minimal Linux definitions so the Agent
// layer links without touching engine sources. (Compile-time gaps are handled
// by the force-included AgentLinuxCompat.h; this file handles LINK-time gaps.)
//
//   * g_hInstance
//       Agent/ArchiveFolderOpen.cpp has `extern HINSTANCE g_hInstance;` (the
//       module handle). On Windows the host EXE defines it; on Linux there is
//       no module handle, and the only Agent use is passing it to MyLoadString
//       for the internal icon-extension resource string (a GUI-only feature).
//       We define it as NULL, exactly as the Linux console front end leaves it.
//
//   * NWindows::MyLoadString(HINSTANCE, UINT, UString&)
//       Declared in Windows/ResourceString.h, pulled in by ArchiveFolderOpen.cpp
//       (CCodecIcons::LoadIcons). The real impl (Windows/ResourceString.cpp)
//       calls the Win32 ::LoadStringW API and does NOT compile on Linux, so it
//       is excluded from every Linux build. The ONLY caller in the Agent layer
//       is CCodecIcons::LoadIcons, which loads a ":"-separated list of
//       file-extension -> icon-index pairs from a binary resource. Linux has no
//       such resource, so the faithful result is an EMPTY string (no icon
//       associations), which LoadIcons already handles gracefully (it just
//       produces an empty IconPairs list). Archive browsing - the B.0 goal -
//       never depends on icon strings. We provide a minimal Linux impl that
//       returns the empty string, documented as such.
//
// Both definitions live in their own TU (not a header) so there is exactly one
// definition program-wide, matching how the engine front ends define g_hInstance.
// ----------------------------------------------------------------------------

#include "AgentLinuxCompat.h"

#ifndef _WIN32

#include "../../../../Common/MyString.h"
#include "../../../../Common/Lang.h"          // P.2 : the CLang loader (engine, unchanged)
#include "../../../../Windows/ResourceString.h"

// The module-handle symbol referenced by Agent/ArchiveFolderOpen.cpp.
// No module handle exists on Linux; NULL mirrors the Linux console front end.
HINSTANCE g_hInstance = NULL;

// ----------------------------------------------------------------------------
// P.2 i18n : the loaded translation table.
//
// On Windows MyLoadString = ::LoadStringW over the compiled .rc STRINGTABLE. On
// Linux there is no STRINGTABLE; the faithful i18n path is 7-Zip's OWN CLang
// loader (CPP/Common/Lang.cpp, compiled unchanged) reading a Lang/*.txt keyed by
// the SAME numeric IDS_*/IDM_* ids the community .txt corpus uses.
//
// g_QtLang is the active translation: empty when no/!matching file is loaded, in
// which case every MyLoadString(id) returns empty and the UI keeps its inline
// English literal (which equals the .rc built-in). When a valid Lang/*.txt is
// loaded, MyLoadString(id) returns g_QtLang.Get(id) — byte-equivalent to the
// upstream LangString() precedence (loaded txt wins, else English).
// ----------------------------------------------------------------------------
namespace NWindowsQtLang {
  CLang g_QtLang;
}

// CLang::Open(path, "7-Zip") — same signature/guard LangUtils.cpp:30 uses. It
// validates the ";!@Lang2@!UTF-8!" signature and the id-0 == "7-Zip" entry, and
// returns false (leaving g_QtLang cleared => English) on any mismatch.
bool QtLang_LoadFile(const FString &txtPath)
{
  return NWindowsQtLang::g_QtLang.Open(txtPath, "7-Zip");
}

bool QtLang_IsLoaded()
{
  return !NWindowsQtLang::g_QtLang.IsEmpty();
}

// Clean, Win32-type-free accessor for the Qt helper (QtLang.h / FmLang). Returns
// the loaded translation for `id`, or empty when absent => caller uses English.
// Kept separate from MyLoadString so QtLang.h needn't pull in HINSTANCE/UINT
// (ResourceString.h), which only the agent-compat TUs define.
UString QtLang_Get(unsigned id)
{
  const wchar_t *s = NWindowsQtLang::g_QtLang.Get((UInt32)id);
  return s ? UString(s) : UString();
}

namespace NWindows {

// Linux stand-in for the Win32 resource-string loader. The HINSTANCE 3-arg form
// is used only by CCodecIcons::LoadIcons for icon-extension association strings,
// which Linux does not ship; it routes to the id-only form (empty unless a txt
// happens to carry that id — harmless, LoadIcons handles empty gracefully).
void MyLoadString(HINSTANCE /* hInstance */, UINT resourceID, UString &dest)
{
  MyLoadString(resourceID, dest);
}

// The two single-argument overloads are the ones LangString() (LangUtils.h:37-39)
// calls. P.2 un-stub: consult the loaded CLang. Return the translation when the
// id is present, else EMPTY — the caller's FmLang()/LangString fallback then
// supplies the inline English literal.
UString MyLoadString(UINT resourceID)
{
  const wchar_t *s = NWindowsQtLang::g_QtLang.Get((UInt32)resourceID);
  return s ? UString(s) : UString();
}

void MyLoadString(UINT resourceID, UString &dest)
{
  const wchar_t *s = NWindowsQtLang::g_QtLang.Get((UInt32)resourceID);
  if (s)
    dest = s;
  else
    dest.Empty();
}

}

#endif // !_WIN32
