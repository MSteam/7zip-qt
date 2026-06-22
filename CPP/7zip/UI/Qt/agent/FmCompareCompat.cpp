// FmCompareCompat.cpp
// ----------------------------------------------------------------------------
// Milestone B.0 : Linux link-compat for one FileManager support function the
// Agent browse path needs.
//
// CAgentFolder::CompareItems / CompareItems2 (the IFolderCompare implementation
// in Agent/Agent.cpp, used to sort a folder listing by name) call the free
// function:
//
//     int CompareFileNames_ForFolderList(const wchar_t *s1, const wchar_t *s2);
//
// Its definition lives in UI/FileManager/PanelSort.cpp, but that whole TU
// `#include`s Panel.h - the Win32 FileManager panel widget (HWND/Control/...) -
// which does NOT compile on Linux. The function itself, however, is pure
// portable string comparison (natural-order name compare; depends only on
// MyCharUpper from the engine's MyString). So we provide ONLY this function
// here, copied VERBATIM from PanelSort.cpp (7-Zip, public domain), rather than
// pulling in the Win32-coupled TU. This is link-additive: no engine source is
// modified.
// ----------------------------------------------------------------------------

#include "../../Agent/StdAfx.h"

#include "AgentLinuxCompat.h"

#include "../../../../Common/MyString.h"

// --- verbatim from CPP/7zip/UI/FileManager/PanelSort.cpp ---------------------
int CompareFileNames_ForFolderList(const wchar_t *s1, const wchar_t *s2)
{
  for (;;)
  {
    wchar_t c1 = *s1;
    wchar_t c2 = *s2;
    if ((c1 >= '0' && c1 <= '9') &&
        (c2 >= '0' && c2 <= '9'))
    {
      for (; *s1 == '0'; s1++);
      for (; *s2 == '0'; s2++);
      size_t len1 = 0;
      size_t len2 = 0;
      for (; (s1[len1] >= '0' && s1[len1] <= '9'); len1++);
      for (; (s2[len2] >= '0' && s2[len2] <= '9'); len2++);
      if (len1 < len2) return -1;
      if (len1 > len2) return 1;
      for (; len1 > 0; s1++, s2++, len1--)
      {
        if (*s1 == *s2) continue;
        return (*s1 < *s2) ? -1 : 1;
      }
      c1 = *s1;
      c2 = *s2;
    }
    s1++;
    s2++;
    if (c1 != c2)
    {
      // Probably we need to change the order for special characters like in Explorer.
      wchar_t u1 = MyCharUpper(c1);
      wchar_t u2 = MyCharUpper(c2);
      if (u1 < u2) return -1;
      if (u1 > u2) return 1;
    }
    if (c1 == 0) return 0;
  }
}
