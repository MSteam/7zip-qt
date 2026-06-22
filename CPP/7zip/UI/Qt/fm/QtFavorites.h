// QtFavorites.h
// ----------------------------------------------------------------------------
// B.7c : QSettings-backed 10-slot store mirroring FileManager/AppState.h's
// CFastFolders (the "FolderShortcuts" FastFolders model) + the Diff command read
// (RegistryUtils.cpp value "Diff" under HKCU\Software\7-Zip\FM).
//
// CFastFolders is a UStringVector of bookmark paths persisted as a single
// multi-string list under value "FolderShortcuts" (ViewSettings.cpp:31). Here it
// is a 10-slot list stored as a QStringList under key "FolderShortcuts" in group
// [Favorites], in the SAME ~/.config/7-Zip/7-Zip.* QSettings file the extract/
// compress dialogs use (org "7-Zip", app "7-Zip", IniFormat). SetString(i,s) /
// GetString(i) mirror CFastFolders 1:1.
//
// The Diff command lives under group [Tools] key "DiffCommand" (mirror of the
// registry value "Diff"); an unset/empty value falls back to a sensible Linux
// default ("meld") per the B.7c brief.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_FAVORITES_H
#define ZIP7_INC_QT_FM_FAVORITES_H

#include "../../../../Common/MyString.h"

namespace QtFavorites {

// CFastFolders model (10 slots).
static const int kNumSlots = 10;

// CFastFolders::GetString(i) — slot i or empty.
UString GetString(int index);
// CFastFolders::SetString(i, s) — store s at slot i (persists immediately).
void SetString(int index, const UString &s);
// Convenience for "&Add folder to Favorites": store `path` in the first empty
// slot (or slot 0 if all full). Returns the slot index used.
int AddNext(const UString &path);

// Diff command ([Tools] DiffCommand). Empty stored -> default ("meld").
UString GetDiffCommand();

} // namespace QtFavorites

#endif
