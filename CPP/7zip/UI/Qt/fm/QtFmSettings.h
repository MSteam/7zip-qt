// QtFmSettings.h
// ----------------------------------------------------------------------------
// B.8 : QSettings-backed reimplementation of the FileManager's CFmSettings
// (RegistryUtils.h:20-34) + the Editor/WorkDir/Delete settings the Options
// dialog edits. Tracks the original value set 1:1 for the Linux-relevant
// fields; the Windows-shell-only fields (associations, shell context menu,
// ShowSystemMenu, LargePages, MemUse, Lang) are DEFERRED to the packaging
// phase and are NOT stored here.
//
// Mapping of the upstream value names (FileManager/RegistryUtils.cpp:27-35,
// 61-65; ZipRegistry.h:159-186) -> QSettings:
//
//   CFmSettings bools  HKCU\Software\7-Zip\FM\{ShowDots,FullRow,ShowGrid,
//     SingleClick,AlternativeSelection,ShowRealFileIcons}  -> group [Options]
//     (key names byte-mirror the originals). Defaults: all false
//     (RegistryUtils.cpp:136-149).
//   Editor             HKCU\Software\7-Zip\FM\Editor   -> group [FM] key "Editor"
//     (mirrors kCU_FMPath="FM" + kEditor="Editor"). Empty => $VISUAL/$EDITOR.
//   Diff               HKCU\Software\7-Zip\FM\Diff     -> group [Tools] key
//     "DiffCommand", OWNED by QtFavorites (QtFavorites.cpp:22-24). We read/write
//     the SAME key so the Options Editor tab and QtFavorites::GetDiffCommand()
//     stay consistent (single key, two surfaces).
//   WorkDir (NWorkDir::CInfo) WorkDirType/WorkDirPath/TempRemovableOnly
//     -> group [Options] keys WorkDirUseSystemTemp/WorkDirPath. Default:
//     Mode=kSystem (use-system-temp = true), empty path (ZipRegistry.h SetDefault).
//   DeleteToTrash      NEW B.8        -> group [Options] DeleteToTrash, default
//     true (the data-safe default: move to the XDG trash, recoverable).
//
// AlternatingColors is a Qt-side toggle (QtPanel hard-enabled it at
// QtPanel.cpp:216); exposed as a view tweak, default true.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_SETTINGS_H
#define ZIP7_INC_QT_FM_SETTINGS_H

#include "../../../../Common/MyString.h"
#include "../../../PropID.h"        // G.4a : PROPID (column-visibility persistence)

#include <QtCore/QByteArray>        // G.4a : QHeaderView::saveState() blob
#include <QtCore/QPair>
#include <QtCore/QVector>

namespace QtFmSettings {

struct CInfo
{
  // --- CFmSettings Linux-relevant subset (RegistryUtils.h:22-30). All bools
  //     default false, exactly like the upstream Load() (RegistryUtils.cpp). ---
  bool ShowDots = false;
  // G.9d : the port has ALWAYS shown per-format file icons (B.6), so to preserve
  // the existing behavior this field defaults to TRUE (ON) here, diverging from the
  // upstream RegistryUtils.cpp:137 default (false). The task spec is explicit:
  // "Default ON (current behavior)". When the user turns it OFF, files show the
  // generic icon. The model's _showRealIcons default (true) matches this.
  bool ShowRealFileIcons = true;
  bool FullRow = false;
  bool ShowGrid = false;
  bool SingleClick = false;
  bool AlternativeSelection = false;

  // --- Qt-side view tweak (QtPanel.cpp:216 hard-enabled it). Default on. ---
  bool AlternatingColors = true;

  // --- Folders tab -> overlays the NWorkDir shim (default kSystem = /tmp). ---
  // G.9c : the full three-way working-folder mode (FoldersPage kWorkModeButtons ->
  // NWorkDir::NMode {kSystem=0, kCurrent=1, kSpecified=2}). kCurrent puts the
  // in-place-rewrite temp file in the ARCHIVE's OWN directory (GetWorkDir, WorkDir.cpp).
  // Persisted as [Options] WorkDirMode; WorkDirUseSystemTemp is kept written for
  // backward-compat and as the migration source when WorkDirMode is absent (an
  // older session only wrote the bool). WorkDirUseSystemTemp mirrors (Mode==kSystem).
  int  WorkDirMode = 0;             // 0=kSystem, 1=kCurrent, 2=kSpecified
  bool WorkDirUseSystemTemp = true; // == (WorkDirMode == 0); kept for compat
  UString WorkDirPath;              // used only when WorkDirMode == kSpecified

  // --- Delete tab. Default true (XDG trash, recoverable). ---
  bool DeleteToTrash = true;

  // --- Editor tab. ---
  UString EditorPath;              // [FM] "Editor" — empty => $VISUAL/$EDITOR
  UString DiffCommand;             // [Tools] "DiffCommand" — empty => "meld"

  // --- P.2 Language. [Options] "Lang" — mirrors HKCU\Software\7-Zip\Lang
  //     (RegistryUtils.cpp kLangValueName). Empty => English; otherwise a
  //     Lang/*.txt basename (".txt" appended if extensionless), loaded at startup
  //     via QtLang_LoadFile. "-" = forced English (upstream sentinel). ---
  UString LangName;

  void Load();
  void Save() const;
};

// Convenience single-value getters used by the live delete / open paths, so
// those call sites don't have to Load() a whole CInfo.
bool    GetDeleteToTrash();         // [Options] DeleteToTrash, default true
UString GetEditorCommand();         // [FM] Editor; fallback $VISUAL then $EDITOR

// P.2 : the install/exe-relative Lang dir (GetLangDirPrefix equivalent). On Linux
// this is <exe-dir>/Lang/  (falls back to $7ZIP_LANG_DIR if set, for tests).
UString GetLangDirPrefix();

// P.2 : resolve a [Options] Lang basename to a full Lang/*.txt path. Empty or "-"
// => empty (English). Appends ".txt" if the name has no extension; an absolute
// path or a name with a '/' is used as-is (test hook).
UString ResolveLangPath(const UString &langName);

// ----------------------------------------------------------------------------
// G.4a : per-list-type view-settings persistence — the QSettings analogue of
// ViewSettings.cpp's CListViewInfo (PER ARCHIVE/LIST TYPE column layout + sort),
// CListMode (PER PANEL view mode), and SavePanelPath/ReadPanelPath (PER PANEL last
// path). The "type id" is CPanel::GetFolderTypeID() (QtFolderModel::folderTypeId):
// "fs" is used for an empty/"FSFolder" type, otherwise the kpidType BSTR (e.g.
// "7-Zip.zip"), so each archive FORMAT keeps its own column layout, exactly as the
// original keys CListViewInfo by _typeIDString.
//
//   [ListView/<type>] "State"   -> QHeaderView::saveState() blob (section sizes,
//     visual order, hidden sections, sort indicator) — the idiomatic Qt capture of
//     CListViewInfo's {Width, IsVisible, order} + {SortID, Ascending} serialization.
//   [ListView/<type>] "Cols"    -> the model-level column-visibility set, a flat
//     "<propid>:<0|1>;..." list. saveState() encodes the header's hidden sections,
//     but the model rebuilds its columns (and re-applies the GetColumnVisible
//     defaults) on every folder change; this set re-seeds the model's IsVisible
//     flags for the type so a hidden column stays hidden across a rebuild
//     (the _savedVisible seam).
//   [Panels] "ListMode<i>"      -> the panel i view mode (0=Large 1=Small 2=List
//     3=Details), mirroring CListMode::Panels[i].
//   [Panels] "Path<i>"          -> the panel i last FS path (SavePanelPath/Read).
// ----------------------------------------------------------------------------

// Normalize a folderTypeId() to a stable settings key: empty/"FSFolder" -> "fs",
// else the type string. (An archive sub-folder reports the same archive type at any
// depth, so all levels of one format share its column layout, like the original.)
UString ListTypeKey(const UString &folderTypeId);

// CListViewInfo : the header geometry blob (QHeaderView::saveState/restoreState),
// keyed by the list-type. ReadListViewState returns an empty array if none stored.
void       SaveListViewState(const UString &typeKey, const QByteArray &state);
QByteArray ReadListViewState(const UString &typeKey);

// CListViewInfo (column IsVisible subset) : the model-level visible-columns set for
// the type, as (PROPID, visible) pairs. Empty vector => nothing stored (use the
// GetColumnVisible defaults).
void SaveColumnVisible(const UString &typeKey,
    const QVector<QPair<PROPID, bool>> &cols);
QVector<QPair<PROPID, bool>> ReadColumnVisible(const UString &typeKey);

// CListMode : the per-panel view mode (0..3). ReadListMode returns `def` (Details=3)
// if none stored.
void SaveListMode(int panelIndex, int mode);
int  ReadListMode(int panelIndex, int def = 3);

// SavePanelPath/ReadPanelPath : the per-panel last FS path. ReadPanelPath returns
// false (and leaves `path` untouched) if none stored.
void SavePanelPath(int panelIndex, const UString &path);
bool ReadPanelPath(int panelIndex, UString &path);

// G.4g : the one/two-panel layout preference — the QSettings analogue of
// AppState's NumPanels (App.h NumPanels, written by CApp::Save / read by
// CApp::Read). Stored under [Panels] "NumPanels". Valid values are 1 and 2;
// ReadNumPanels returns `def` (2, the upstream default) when nothing valid is
// stored, and clamps any out-of-range stored value to that default.
void SaveNumPanels(int numPanels);
int  ReadNumPanels(int def = 2);

// G.4d : CFolderHistory persistence — the QSettings analogue of
// SaveFolderHistory/ReadFolderHistory (ViewSettings.cpp:292-295), the app-level
// list of recently-visited folders (AppState.h CFolderHistory). Stored under the
// [FM] group key "FolderHistory" (byte-mirrors kFolderHistoryValueName under
// REG_PATH_FM = HKCU\Software\7-Zip\FM). The list is held MOST-RECENT-FIRST and
// capped at 100 (CFolderHistory::Normalize kMaxSize) by the caller before saving;
// these calls just serialize/deserialize it verbatim.
void         SaveFolderHistory(const UStringVector &folders);
UStringVector ReadFolderHistory();

// P.2 / G.1j : the upstream ReloadLang() equivalent — read [Options] Lang, resolve,
// and QtLang_LoadFile it. Called once at app init (main_fm / main_7zqt).
//   - A non-empty stored Lang is AUTHORITATIVE (the "-" sentinel = forced English).
//   - When NO Lang is stored (empty), fall back to OS-locale auto-detect
//     (OpenDefaultLang() equivalent): derive candidate basenames from
//     QLocale::system() (uiLanguages()/name(), e.g. "de_DE"->de, "zh_CN"->zh-cn,
//     "pt_BR"->pt-br), country-variant before bare language, and load the first
//     <langdir>/<base>.txt that exists and opens.
// A failed/absent Open leaves the English fallback active. Returns true if a txt
// was loaded.
bool StartupLoadLang();

} // namespace QtFmSettings

#endif
