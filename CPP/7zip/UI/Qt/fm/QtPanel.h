// QtPanel.h
// ----------------------------------------------------------------------------
// Milestone B.3 : ONE file-manager panel widget — the Qt analogue of a single
// CPanel (Panel.h / PanelFolderChange.cpp). It generalizes the B.2 QtFsBrowser
// from a whole QMainWindow into a reusable QWidget so CApp's two-panel layout
// (App.h: two CPanel in a splitter, focused-panel tracking) can hold two of them.
//
// Each panel is INDEPENDENT and owns:
//   * its own QtFolderModel (over an IFolderFolder: CFSFolder for the filesystem,
//     or a CAgent archive root) + a folders-first QtFolderSortProxy,
//   * an editable address bar (kpidPath; Enter navigates to the typed path) and
//     an "Up" button (CPanel::OpenParentFolder / the seamless archive exit),
//   * a MULTI-selection QTreeView (operations act on the selected rows),
//   * the seamless filesystem -> archive transition (double-click an archive file
//     opens it as a folder; Up out of the archive root returns to its FS dir) —
//     the exact behaviour QtFsBrowser implemented, lifted in here verbatim.
//
// It EMITS focusChanged()/selectionChanged()/pathChanged() so the shell
// (QtFileManagerWindow) can track the focused panel (App.h _lastFocusedPanel) and
// keep the status bar in sync, and exposes currentFolder()/currentPath()/
// selectedSourceRows()/selectedFullPaths()/isInArchive() so the wired operations
// (Add/Extract/Test/CRC) can read the focused panel's selection.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_PANEL_H
#define ZIP7_INC_QT_FM_PANEL_H

#include <QtWidgets/QWidget>
#include <QtCore/QVector>
#include <QtCore/QPair>           // G.4b : (PROPID, visible) column-visibility seam
#include <QtCore/QPoint>          // contextMenuRequested(QtPanel *, const QPoint &)
#include <QtCore/QString>         // G.3b/G.3e : QString-typed open-error/virus signals
#include <QtCore/Qt>

#include <functional>

#include "../../../../Common/MyCom.h"
#include "../../../../Common/MyString.h"
#include "../../../PropID.h"          // B.7b : PROPID / kpidName / kpidNoProperty
#include "../../FileManager/IFolder.h"

#include "QtFolderModel.h"
#include "QtFmSettings.h"          // B.8 : applySettings(CInfo)

class QTreeView;
class QLineEdit;
class QToolButton;
class QMimeData;
class QStackedWidget;
class QAbstractItemView;
class QTimer;
class QFileSystemWatcher;   // G.4e : Auto Refresh (FS directory-change watcher)
class QtFolderSortProxy;
class QtPanelListView;
class QtPanel;
class QtOverwritePrompt;
class QtPasswordPrompt;

// B.4b : the drop-dispatch callback the shell (QtFileManagerWindow) installs on
// each panel. Called on the GUI thread from QtPanelView::dropEvent with the
// destination panel, the dropped mime (text/uri-list), and Qt's resolved drop
// action. The shell decides internal-vs-external dispatch and runs the B.4a
// QtFsCopyWorker. Returns true if the drop was accepted/handled.
using QtPanelDropHandler =
    std::function<bool(QtPanel *destPanel, const QMimeData *mime, Qt::DropAction action)>;

// B.5c : the temp-dir factory the shell (QtFileManagerWindow) installs on each
// panel so an archive drag-OUT can extract the selection into a session temp
// dir that the WINDOW owns (and removes on close, mirroring the original FM's
// remove-temp-dirs-on-close). The factory creates+registers a temp dir and
// returns it (trailing separator), and hands back the GUI-thread overwrite
// prompt + the GUI-thread password prompt (Encrypted-FM: extracting encrypted
// DATA out of an archive) + the headless flag the extraction worker needs.
// Returns an empty FString on failure. The CTempDir RAII the Win32 original uses
// is _WIN32-only, so the window builds the dir from MyGetTempPath/CreateComplexDir
// instead.
using QtPanelTempDirFactory =
    std::function<FString(QtOverwritePrompt **outPrompt,
        QtPasswordPrompt **outPwPrompt, bool *outHeadless)>;

class QtPanel Z7_final : public QWidget
{
  Q_OBJECT
public:
  explicit QtPanel(QWidget *parent = nullptr);

  // Bind this panel's model to an already-bound root IFolderFolder.
  //   isFsRoot : true if `rootFolder` is a filesystem CFSFolder (enables the
  //              seamless FS->archive open on leaf activation).
  void setRootFolder(IFolderFolder *rootFolder, bool isFsRoot);

  // G.4a : the panel's index (0=left, 1=right). The view-settings persistence layer
  // keys the per-panel view MODE (CListMode::Panels[i]) and last PATH
  // (SavePanelPath/ReadPanelPath) on it. The window sets it once before binding a
  // start folder. Default -1 = persistence disabled (a browser/test binary that
  // never assigns one keeps the old reset-on-restart behaviour).
  void setPanelIndex(int index) { _panelIndex = index; }
  int panelIndex() const { return _panelIndex; }

  // G.4a : on startup, restore the persisted view MODE (CListMode) for this panel.
  // Called by the window after setPanelIndex(), before/after the first folder bind.
  // No-op if persistence is disabled (panelIndex < 0).
  void restorePersistedViewMode();

  // --- queries the shell / operations use --------------------------------
  IFolderFolder *currentFolder() const { return _model->currentFolder(); }
  UString currentPath() const { return _model->currentPath(); }     // kpidPath
  // G.6a : the FULL folder prefix shown in the address bar — the Qt mirror of
  // CPanel::_currentFolderPrefix / LoadFullPath (PanelFolderChange.cpp:356-369):
  // each archive level's host path (the parent-stack frames' hostPath) followed by
  // the current in-archive path (currentPath()). On the filesystem this is just the
  // FS path (currentPath()); inside an archive it includes the host archive file's
  // FS path so the drop-into-archive confirmation identifies which archive is being
  // modified (PanelDrag.cpp:2618 uses _currentFolderPrefix).
  UString currentFolderPrefix() const;
  bool isInArchive() const { return _inArchive; }
  // B.5b : true only for an in-archive panel whose archive can be modified in
  // place (CAgent::CanUpdate && !IsThere_ReadOnlyArc, captured at open time).
  // The FM gates in-place Delete/Add on this; read-only formats stay refused.
  bool isUpdatableArchive() const { return _inArchive && _isUpdatableArchive; }
  bool isEmptySelection() const;

  // Source-model rows currently selected (mapped through the sort proxy).
  QVector<int> selectedSourceRows() const;

  // G.4i : the FOCUSED source-model row (the view's currentIndex, mapped through
  // the proxy), or -1 if none. Mirrors CPanel::GetFocusedItem -> GetRealItemIndex,
  // used by the status bar's per-focused-item size/date fields. Independent of the
  // selection set (a row can be focused without being selected).
  int focusedSourceRow() const;

  // Absolute filesystem paths of the selected items. Only meaningful when this
  // panel is in the FILESYSTEM view (not inside an archive): currentPath()+name
  // for each selected row. Empty when in an archive (operations that need real
  // files guard on !isInArchive()).
  UStringVector selectedFullPaths() const;

  // The current FS directory path (with trailing separator), as a destination
  // for Extract-to-other-panel. Empty when this panel is inside an archive.
  UString currentFsDirPath() const;

  // The single selected item's name (or the first), for default archive names
  // and single-item operations. Empty if nothing is selected.
  UString firstSelectedName() const;

  // G.3e : run the IsVirus_Message name-spoof guard over the CURRENTLY SELECTED
  // items before an EXTERNAL launch driven by the menu commands (Open Outside /
  // View / Edit -> doOpen), mirroring how CPanel::OpenItem / OpenItemInArchive call
  // IsVirus_Message at their entry for ANY tryExternal open (PanelItemOpen.cpp
  // :955-957 / :1470-1472), not only the double-click path. Returns true (and emits
  // openBlockedAsVirus for the first offending item) if any selected name is a spoof
  // -> the caller must NOT launch. The double-click path keeps its own guard in
  // onLeafActivated; this covers the menu/toolbar Open-external chokepoint.
  bool checkSelectionForVirus();

  QtFolderModel *model() const { return _model; }

  // Open the FS item named `name` in this (FS) panel as if double-clicked: an
  // archive opens as a folder, anything else opens with its associated program.
  // Used to honor a file passed on the command line (a file-manager "Open with" /
  // MIME association). Returns false if not on an FS folder or the name is absent.
  bool openFsItemByName(const UString &name);

  // G.4b : the visible-columns set keyed by column PROPID, the seam the NEXT task
  // (persistence) saves/restores. visibleColumnPropIDs() returns every column's
  // (PROPID, visible) state in column order (kpidName first); setColumnVisible()
  // toggles a column by PROPID (Name stays locked-visible) exactly as the header
  // context menu does, applying the same sort-column-reset rule. These let the
  // persistence layer round-trip column visibility without reaching into the model.
  QVector<QPair<PROPID, bool>> visibleColumnPropIDs() const;
  void setColumnVisible(PROPID propID, bool visible);

  // Re-list the current folder (View -> Refresh).
  void refresh();

  // G.4e : Auto Refresh (CPanel::AutoRefresh_Mode + OnTimer, PanelItems.cpp:1435).
  // The Win32 FM polls IFolderWasChanged each timer tick and re-lists when the
  // current directory changed; the Linux analogue watches the current FS directory
  // with QFileSystemWatcher (inotify) and re-lists on its directoryChanged signal.
  // setAutoRefresh() flips the per-panel flag (default ON, matching the original
  // CApp ctor AutoRefresh_Mode(true), App.h:92) and (re)points or tears down the
  // watcher; isAutoRefresh() reports the flag.
  // Auto-refresh is meaningful only for an FS folder — an archive folder is static,
  // so the watcher is dropped while _inArchive (the original's IFolderWasChanged is
  // a no-op there too), though the flag itself is remembered across the transition.
  void setAutoRefresh(bool on);
  bool isAutoRefresh() const { return _autoRefresh; }

  // G.4c : Flat View (recursive flat listing). toggleFlatMode() is the Qt mirror of
  // CPanel::ChangeFlatMode (Panel.cpp:894-902): flip _flatMode, remember it as this
  // CONTEXT's default (_flatModeForArc inside an archive, _flatModeForDisk on the
  // filesystem — the Panel.h:334-336 trio), push it onto the current folder, and
  // re-list (the model resets, adding/dropping the kpidPrefix path column). flatMode()
  // is the live state of the CURRENT folder; flatModeSupported() is whether the
  // current folder can go flat at all (it grays the menu otherwise — faithful to the
  // original only offering flat mode where IFolderSetFlatMode exists).
  void toggleFlatMode();
  bool flatMode() const;
  bool flatModeSupported() const;

  // B.8 : push the Options view tweaks (FullRow / AlternatingColors / SingleClick
  // / ShowGrid) onto BOTH the Details tree and the icon/list view — the Qt
  // analogue of g_App.SetListSettings() (OptionsDialog.cpp:85). The window calls
  // this on both panels at startup and after the Options dialog is accepted.
  void applySettings(const QtFmSettings::CInfo &s);

  // B.7c : navigate this panel to an existing FS directory (the shared body of
  // address-bar navigation). Used by Favorites/OpenBookmark. Returns true on a
  // successful bind; restores the real path and returns false on a bad entry.
  bool navigateToFsPath(const UString &path);

  // G.4d : the ANCESTOR folders of the current FS path, root-first (e.g. for
  // /home/user/dir -> "/", "/home", "/home/user", "/home/user/dir"), each with a
  // trailing separator. The Qt mirror of CPanel::OnComboBoxCommand's CBN_DROPDOWN
  // ancestor walk (PanelFolderChange.cpp:632-763), with ONLY the faithful, non-
  // Windows parts kept: the Windows drive-roots / Documents / Computer / Network
  // entries are dropped (no Linux substitute invented). Empty when inside an
  // archive (the address bar is inert there, mirroring an archive virtual path
  // not being an FS path). Used by the address-bar dropdown.
  UStringVector addressAncestors() const;

  // G.4d : the provider the shell (QtFileManagerWindow) installs so the address-bar
  // dropdown can list the APP-LEVEL recent-folders history (AppState FolderHistory),
  // most-recent-first. Null = no history surfaced (a browser/test binary). Not owned.
  using QtPanelHistoryProvider = std::function<UStringVector()>;
  void setHistoryProvider(QtPanelHistoryProvider provider) { _historyProvider = std::move(provider); }

  // B.5b test hook (offscreen): select ONLY the given source-model row (mapped
  // through the sort proxy), clearing any other selection. No live mouse needed.
  void selectSourceRowForTest(int sourceRow);

  // G.5a test hook (offscreen): select the given source-model rows (mapped through
  // the sort proxy), clearing any other selection — for the multi-select Properties
  // aggregate. No live mouse needed.
  void selectSourceRowsForTest(const QVector<int> &sourceRows);

  // --- B.4b drag & drop --------------------------------------------------
  // Install the shell's drop-dispatch callback (see QtPanelDropHandler).
  void setDropHandler(QtPanelDropHandler handler) { _dropHandler = std::move(handler); }

  // B.5c : install the shell's temp-dir factory (see QtPanelTempDirFactory),
  // used by an archive drag-OUT to extract the selection into a window-owned
  // temp dir.
  void setTempDirFactory(QtPanelTempDirFactory factory) { _tempDirFactory = std::move(factory); }

  // Encrypted-FM : the shell hands the panel the shared GUI-thread password prompt
  // so opening a HEADER-encrypted archive (encrypted file names) can prompt for the
  // password on both the synchronous fast-path and the threaded open path. Null =
  // no prompt wired (headless / a browser binary): only a pre-known password is
  // used and a header-encrypted archive fails to open (the old behaviour).
  void setPasswordPrompt(QtPasswordPrompt *prompt) { _passwordPrompt = prompt; }

  // P.1 : the shell tells the panel whether it runs non-interactively, so the
  // threaded archive-open worker can suppress its modal dialog's blocking.
  void setHeadless(bool headless) { _headless = headless; }
  // P.1 test hook : force the worker (threaded) open path regardless of archive
  // size, so the off-GUI-thread open is exercised even on tiny fixtures.
  void setForceThreadOpen(bool force) { _forceThreadOpen = force; }

  // The view this panel owns (so the shell can reach it for selection ops).
  // Always the Details QTreeView (header access depends on it); use activeView()
  // for the currently-visible view in icon/list modes.
  QTreeView *view() const { return _view; }

  // --- B.7b view modes (Large / Small / List / Details) ------------------
  // CPanel::SetListViewMode (Panel.cpp:871). index: 0=Large 1=Small 2=List
  // 3=Details. The four modes share the SAME _proxy and ONE QItemSelectionModel
  // (so selectedSourceRows() is correct regardless of which view is visible),
  // swapped via a QStackedWidget. Default = Details.
  void setViewMode(int mode);
  int viewMode() const { return (int)_viewMode; }
  // The currently-visible item view (QTreeView for Details, QListView else).
  QAbstractItemView *activeView() const;

  // --- B.7b arrange-by (sort) --------------------------------------------
  // CPanel::SortItemsWithPropID (PanelSort.cpp:256). Drives the shared proxy.
  // propID: kpidName / kpidExtension / kpidMTime / kpidSize. unsorted=true ==
  // kpidNoProperty (source/load order). Mirrors the toggle + default-descending
  // -for-Size/MTime rule.
  void arrangeBy(int propID, bool unsorted);

  // --- B.7b select / deselect by mask & by type --------------------------
  // CPanel::SelectSpec (PanelSelect.cpp:154): for every source row whose name
  // matches the wildcard `mask`, set selection to `selectMode` (Select if true,
  // Deselect if false); non-matching rows are left untouched.
  void selectByMask(const UString &mask, bool selectMode);
  // CPanel::SelectByType (PanelSelect.cpp:169): derive the "type" from the
  // focused/current row (folder => all folders; extensionless file => all
  // extensionless files; else "*"+ext) and (de)select all matching rows.
  void selectByType(bool selectMode);

  // Build the drag-source QMimeData (text/uri-list).
  //   FS panel    : the selected items' absolute FS paths (currentPath()+name).
  //   ARCHIVE panel (B.5c) : EAGERLY extract the selected entries into a window-
  //     owned temp dir (the faithful Linux mirror of the Win32 FM's
  //     CopyFromPanelTo_Folder extract-to-temp, collapsed to happen up front),
  //     then hand back file:// URIs of the extracted top-level entries.
  // Returns nullptr when nothing is selected or extraction produced nothing.
  // NOT const: the archive branch creates temp dirs and runs a modal worker.
  // Caller (QDrag) owns the returned mime.
  QMimeData *buildDragMimeData();

  // B.7a : extract the selected archive entries into a window-owned temp dir and
  // return the absolute temp paths of the extracted top-level entries (the path
  // form of extractSelectionToTempMime). Used by Open-Outside/View/Edit on an
  // archive item: open the temp file with xdg-open/$EDITOR (View = read-only; the
  // Edit writeback path is handled by doEditInArchive, G.2a). Empty on failure /
  // no selection.
  UStringVector extractSelectionToTempPaths();

  // True if this panel can accept a uri-list drop: a FS view, OR an updatable
  // archive panel (B.5b add-into-archive on drop). Read-only archives reject.
  bool acceptsDrop() const { return !_inArchive || _isUpdatableArchive; }

  // Called by QtPanelView::dropEvent — forwards to the installed drop handler.
  bool handleDrop(const QMimeData *mime, Qt::DropAction action);

  // G.6c : the FS destination directory for a drop, honoring the sub-folder under
  // the drop cursor. The Qt mirror of CDropTarget::GetTargetPath (PanelDrag.cpp
  // :2186): GetFsPath() + m_DropHighlighted_SubFolderName (PanelDrag.cpp:2194). If a
  // drop-target sub-folder was set (an FS folder row was hit-tested under the cursor),
  // the destination is currentFsDirPath()+folder+sep; otherwise just currentFsDirPath().
  // Empty inside an archive (the address bar / FS path is inert there).
  UString dropTargetFsDirPath() const;

  // G.6c : on drag-over, hit-test the row under `viewportPos` of `view` and, if it is
  // an FS folder row (not "..", not a file, not inside an archive), mark it as the
  // drop-target sub-folder and highlight it (LVIS_DROPHILITED analog: select the row
  // for the visual cue). Otherwise clear any previous highlight. Mirrors
  // CDropTarget::PositionCursor's ListView_HitTest + IsItem_Folder gate + the FS-only
  // restriction (PanelDrag.cpp:1981-2001). No-op (clears) when not an FS folder.
  void updateDropTarget(QAbstractItemView *view, const QPoint &viewportPos);

  // G.6c : drop-target hit-test by drop point at COMMIT time (dropEvent). Same logic
  // as updateDropTarget but without touching the highlight (the drop is ending).
  void setDropTargetFromDropPos(QAbstractItemView *view, const QPoint &viewportPos);

  // G.6c : clear the drop-target sub-folder + remove the drag-over highlight. Mirrors
  // CDropTarget::RemoveSelection (PanelDrag.cpp:1892). Called on drag-leave / after a
  // drop / when the cursor is not over an FS folder.
  void clearDropTarget();

  // G.6c test hook (offscreen): force the drop-target sub-folder to the given SOURCE
  // row (must be an FS folder row), so the subfolder-drop path is exercised without a
  // live mouse. Cleared by clearDropTarget(). Returns true if the row is a valid FS
  // folder and was set.
  bool setDropTargetRowForTest(int sourceRow);

  // G.6e : clear this panel's item selection — the Qt mirror of CPanel::KillSelection
  // (PanelSelect.cpp), called by the shell after a successful drag completes with no
  // messages and not cancelled (PanelDrag.cpp:1800-1801).
  void killSelection();

  // G.4k : focus this panel's active item view (the OnTab target — the original
  // moves focus to the OTHER panel's _listView via SetFocusToList, App.cpp:45).
  // Used by the window's focusOtherPanel() (Tab) and after a cross-panel mirror.
  void focusActiveView();
  // G.4k : focus + select-all this panel's editable address field (the
  // SetFocusToPath target — the original focuses the panel's _headerComboBox and
  // shows its dropdown, App.cpp:55-56). Read-only when inside an archive, so the
  // field still takes focus but text stays uneditable, mirroring the original's
  // path-bar being inert for archive paths.
  void focusAddressField();

  // G.4k : the focused row's source index + whether it is a folder, for the
  // window's OnSetSubFolder (Alt+Right/Left mirrors the focused sub-folder/parent
  // into the OTHER panel — CApp::OnSetSubFolder reads srcPanel._listView's focused
  // item, App.cpp:874-878). focusedSubFolderName() returns the focused item's name
  // when it IS a folder (and not "..": kParentIndex is handled by the parent path),
  // else an empty string. focusedRowIsParentUp() is true when the focused row is the
  // synthetic ".." up-entry.
  UString focusedSubFolderName() const;
  bool focusedRowIsParentUp() const;

Q_SIGNALS:
  // The panel gained focus (focus-in on the view, or a click/selection in it).
  // The shell uses this to track _lastFocusedPanel (App.h).
  void focused(QtPanel *self);
  // The selection set changed (so the status bar can recompute count/size).
  void selectionChanged(QtPanel *self);
  // The current folder / path changed (navigation).
  void pathChanged(QtPanel *self);
  // A right-click in the items view asked for the per-file context menu
  // (CPanel::OnContextMenu, PanelMenu.cpp:1081). `globalPos` is screen-space so
  // the shell can exec() the QMenu there. Before emitting, the panel has already
  // taken focus and (mirroring the original) selected the item under the cursor
  // if it was not part of the current selection; an empty-space click leaves an
  // empty selection (the background/no-selection menu variant).
  void contextMenuRequested(QtPanel *self, const QPoint &globalPos);
  // A leaf (non-folder) item was activated (double-click / Open) that is NOT an
  // archive to enter: an FS file that is not an archive, or any item inside an
  // open archive. Mirrors CPanel::OpenItem's "regular file -> open externally"
  // branch (PanelItemOpen.cpp); the shell opens the panel's current selection
  // with the OS-associated program (xdg-open) via doOpen(OpenOutside).
  void openExternallyRequested(QtPanel *self, int row);

  // G.3b : an attempt to open the activated item AS AN ARCHIVE failed with a REAL
  // error (NOT the benign "this file is not an archive" = S_FALSE, which falls
  // through to openExternallyRequested). Mirrors CPanel::OpenAsArc_Msg
  // (PanelItemOpen.cpp:546-559): the shell shows the SINGLE faithful error dialog —
  // IDS_CANT_OPEN_ENCRYPTED_ARCHIVE (encrypted) / IDS_CANT_OPEN_ARCHIVE (S_FALSE but
  // encrypted) / the decoded HResultToMessage(res) (a hard error). `virtualPath` is
  // the full path/name shown in the message; `encrypted` picks the message variant;
  // `res` is the open HRESULT (S_FALSE only reaches here when `encrypted`). The item
  // is NOT handed to xdg-open. (Qt-type args so the signal has no UString metatype.)
  void openArchiveError(QtPanel *self, const QString &virtualPath,
      int res, bool encrypted);

  // G.3e : the activated item was BLOCKED from external launch because its NAME is a
  // spoof (a U+202E RLO char, or a run of 5+ spaces). Mirrors CPanel::IsVirus_Message
  // (PanelItemOpen.cpp:867): the shell shows the IDS_VIRUS warning. `sanitisedName`
  // is the cleaned display string (RLO -> "[RLO]", each 5-space run -> one space) and
  // `isSpaceError` controls the message's "(...)" paren strip, exactly as the original.
  void openBlockedAsVirus(QtPanel *self, const QString &originalName,
      const QString &sanitisedName, bool isSpaceError);

  // G.4k : Tab pressed in the items view — move focus to the OTHER panel
  // (CPanel::OnKeyDown VK_TAB -> _panelCallback->OnTab, PanelKey.cpp:41-44 /
  // CPanelCallbackImp::OnTab, App.cpp:42-47). The shell focuses otherPanel().
  void tabToOtherPanel(QtPanel *self);
  // G.4k : Alt+F1 / Alt+F2 — focus the LEFT(0) / RIGHT(1) panel's path field
  // (CPanel::OnKeyDown Alt+F1/F2 -> SetFocusToPath, PanelKey.cpp:70-75). `pathIndex`
  // is 0 for F1 (left), 1 for F2 (right), exactly as the original passes it.
  void setFocusToPathRequested(QtPanel *self, int pathIndex);
  // G.4k : Alt+Up — open the SAME folder in the OTHER panel (CPanel::OnKeyDown
  // VK_UP+alt -> OnSetSameFolder, PanelKey.cpp:202-203 / CApp::OnSetSameFolder).
  void setSameFolderRequested(QtPanel *self);
  // G.4k : Alt+Right / Alt+Left — open the focused sub-folder / parent in the OTHER
  // panel (CPanel::OnKeyDown VK_RIGHT/VK_LEFT+alt -> OnSetSubFolder, PanelKey.cpp
  // :208-219 / CApp::OnSetSubFolder). The original's OnSetSubFolder uses the source
  // panel's FOCUSED item (a folder => descend; kParentIndex => ascend); both arrow
  // directions call the SAME OnSetSubFolder, so we mirror that with one signal.
  void setSubFolderRequested(QtPanel *self);

public Q_SLOTS:
  void onUp();                 // CPanel::OpenParentFolder (+ seamless archive exit)
  void onAddressEntered();     // address bar Enter -> navigate to typed FS path
  // G.4d : the address-bar dropdown affordance (the arrow button) was clicked —
  // build + pop a menu of this path's ANCESTOR folders (indented breadcrumb) plus
  // the app-level recent-folders history, and navigate to the chosen entry. The Qt
  // analogue of the header combo's CBN_DROPDOWN (PanelFolderChange.cpp:627).
  void onAddressDropdown();

private Q_SLOTS:
  void onDoubleClicked(const QModelIndex &proxyIndex);
  // CPanel::OnContextMenu (PanelMenu.cpp:1081). Connected to each view's
  // customContextMenuRequested(localPos). Focuses this panel, selects the
  // clicked item if not already selected, and emits contextMenuRequested().
  void onCustomContextMenu(const QPoint &localPos);
  // G.4b : right-click on the Details QTreeView HEADER. Mirrors CPanel::OnRightClick
  // on the list header -> ShowColumnsContextMenu (PanelItems.cpp:1362): a checkbox
  // popup of every property column; toggling shows/hides the column via the model's
  // IsVisible + QHeaderView::setSectionHidden. The Name column action is locked
  // (disabled, always checked); hiding the active sort column resets the sort to
  // Name (ShowColumnsContextMenu:1417-1421).
  void onHeaderContextMenu(const QPoint &localPos);
  // G.4b : the model rebuilt its columns (folder change) or a column's visibility
  // toggled — re-apply QHeaderView::setSectionHidden so the view's hidden sections
  // match the model's IsVisible flags. Connected to columnVisibilityChanged().
  void applyColumnVisibility();
  void onLeafActivated(int row);
  void onViewSelectionChanged();
  void onModelReset();
  // G.4e : the watched FS directory changed on disk (QFileSystemWatcher's inotify
  // event — the Linux analogue of the Win32 directory-change notification that
  // drove IFolderWasChanged). Re-list the panel, preserving focus/selection where
  // the same names still exist. Mirrors CPanel::OnTimer -> OnReload(true).
  void onDirectoryChanged(const QString &path);
  // G.4l : the model committed an inline rename (setData -> itemRenamed). Re-list
  // the folder (archive re-bind via refresh(), or FS re-scan).
  void onItemRenamed();
  // G.4a : a header change (section resized/moved or the sort indicator) — persist
  // this list-type's header state. Mirrors CPanel::SaveListViewInfo on a column
  // change. Connected to QHeaderView sectionResized/sectionMoved/
  // sortIndicatorChanged; coalesced through _saveViewTimer so a drag doesn't write
  // QSettings per pixel.
  void scheduleSaveViewSettings();
  void saveViewSettingsNow();

protected:
  bool eventFilter(QObject *obj, QEvent *ev) override;

private:
  // Seamless FS -> archive open. Returns the open outcome (mirrors CPanel::OpenItem
  // PanelItemOpen.cpp): S_OK = opened as a folder; S_FALSE/other = not an archive
  // (caller opens it externally); E_ABORT = user cancelled (e.g. the password
  // prompt) -> stay put, do NOT open externally.
  // G.3b : `outEncrypted` (optional) reports whether the file was header-encrypted
  // (so the caller can pick IDS_CANT_OPEN_ENCRYPTED_ARCHIVE on a failed open),
  // mirroring COpenResult::Encrypted.
  HRESULT tryOpenAsArchive(int row, bool *outEncrypted = nullptr);
  // G.2b : open the archive at `row` (an item INSIDE the currently-open archive)
  // seamlessly AS A NESTED SUB-FOLDER. Mirrors CPanel::OpenItemInArchive's
  // tryAsArchive branch (PanelItemOpen.cpp:1503): QI the current CAgentFolder for
  // IInArchiveGetStream, GetStream the sub-stream, OpenArchiveStreamAsFolder it,
  // and on S_OK push a parent frame (keeping the outer agent(s) alive) + rebind to
  // the nested root. Returns S_OK (entered), S_FALSE (not an archive -> caller
  // extracts+opens externally), or E_ABORT (password cancelled -> stay put).
  // G.3b : `outEncrypted` (optional) reports whether the sub-stream was
  // header-encrypted, for the failed-open error-message variant.
  HRESULT tryOpenNestedArchive(int row, bool *outEncrypted = nullptr);
  // G.2b : the top-of-stack CAgent (the agent owning the archive level the panel
  // is currently showing) as IUnknown, for the refresh re-bind (the caller QIs it
  // to IInFolderArchive). Null when not in an archive. Returned as IUnknown so the
  // header needs no IFolderArchive.h dependency.
  CMyComPtr<IUnknown> topAgent() const;
  void refreshAddress();

  // G.4e : re-point the Auto Refresh watcher to the panel's CURRENT state. Called
  // after every folder change (the onModelReset chokepoint) and from setAutoRefresh.
  // When auto-refresh is ON and the panel shows an FS folder, the watcher watches
  // exactly the current FS directory (dropping any previously-watched path); when
  // OFF, or inside an archive, the watcher is cleared. The Qt mirror of the original
  // re-targeting its directory-change source on each RefreshListCtrl.
  void updateWatcher();

  // G.4a : after the model rebound to a folder, restore the persisted view settings
  // for its LIST TYPE — the Qt mirror of CPanel::InitColumns reading
  // _listViewInfo.Read(_typeIDString). When the type changed since the last bind:
  // re-seed the model's column-visibility set from QSettings (ReadColumnVisible) and
  // restore the header geometry (QHeaderView::restoreState). The persisted header
  // state carries the saved sort indicator + section sizes/order/hidden set, so this
  // is also where the sort/columns return after a restart. No-op when persistence is
  // disabled (panelIndex < 0) or the type is unchanged (avoids clobbering live edits).
  void restoreViewSettingsForType();

  // G.4j : after a folder change (Up, or a re-sort), set the focus + selection to
  // the source row whose kpidName == `name` and scroll it into view (mirrors
  // RefreshListCtrl honoring CSelectedState::FocusedName, and
  // SortItemsWithPropID's EnsureVisible(GetFocusedItem)). No-op if `name` is empty
  // or no row matches. `select` also adds it to the selection (Up restores the
  // focus highlight); pass false to only move the focus rectangle + scroll.
  void focusRowByName(const UString &name, bool select);
  // G.4j : the leaf name of the CURRENT folder (the last path component of
  // currentPath()), used to capture which child to re-focus after going Up.
  UString currentFolderLeafName() const;
  // G.4j : scroll the currently-focused row back into view after a re-sort
  // (SortItemsWithPropID's EnsureVisible(GetFocusedItem), PanelSort.cpp:277).
  void scrollToFocusedRow();

  // G.4b : the PROPID the Details view is currently sorted by via its header
  // (sortIndicatorSection), used to decide whether hiding a column de-selects the
  // active sort column and must reset the sort to Name (ShowColumnsContextMenu).
  PROPID activeSortColumnPropID() const;

  // B.7b : after any model reset the QTreeView rebuilds its selection model,
  // orphaning the QListView's; re-share the tree's selection model with the
  // list view and (re)connect the single selectionChanged. Called from the
  // ctor, setRootFolder, and onModelReset.
  void rebindSelectionModel();

  // G.4c : on a context transition, pick this context's remembered flat default
  // (_flatModeForArc inside an archive, else _flatModeForDisk), store it as the
  // live _flatMode, and push it onto the now-bound folder (re-listing). Mirrors the
  // original re-seeding _flatMode from the per-context default at the open/exit
  // points (PanelItemOpen.cpp:508 / PanelFolderChange.cpp:1013). No reset is forced
  // if the folder is already in that mode (the model's setFlatMode is idempotent).
  void applyContextFlatMode();

  // B.5c : the archive drag-OUT body of buildDragMimeData(). Extracts the
  // selected archive entries (selectedSourceRows()) into a window-owned temp
  // dir via IFolderOperations::CopyTo on the panel's CAgentFolder (the same
  // path QtFsCopyWorker drives for FS copy), enumerates the extracted top-level
  // entries, and returns a text/uri-list mime of their file:// URIs. nullptr if
  // the selection is empty, the factory is unset, or extraction produced
  // nothing. Move-OUT (extract + delete-from-archive) is deferred (the Win32
  // original never deleted from an archive on drag either).
  QMimeData *extractSelectionToTempMime();

  QtFolderModel *_model;
  QtFolderSortProxy *_proxy;
  QTreeView *_view;             // Details (header access; view() returns this)
  QtPanelListView *_listView;  // B.7b : Large/Small/List icon view
  QStackedWidget *_viewStack;  // B.7b : swaps Details <-> icon/list view
  QLineEdit *_address;
  QToolButton *_upButton;
  // G.4d : the address-bar dropdown affordance (the down-arrow at the field's right
  // edge — the Qt mirror of the header combo's drop button). Clicking it pops the
  // ancestor + recent-folders menu (onAddressDropdown).
  QToolButton *_addressDropButton = nullptr;
  // G.4d : the shell's recent-folders history provider (set via setHistoryProvider),
  // so the dropdown can list the app-level AppState FolderHistory. Null in a browser
  // / test binary. Not owned.
  QtPanelHistoryProvider _historyProvider;

  // B.7b : the four view modes (radio-exclusive, default Details). Only the
  // visible widget + the QListView's QListView::ViewMode/iconSize differ; model,
  // proxy and selection model are shared (CPanel::SetListViewMode swaps only the
  // Win32 LVS_TYPEMASK bits).
  enum class ViewMode { LargeIcons = 0, SmallIcons = 1, List = 2, Details = 3 };
  ViewMode _viewMode = ViewMode::Details;

  // B.7b : the active arrange-by sort key + direction, mirroring CPanel's
  // _sortID/_ascending (Panel.cpp defaults: kpidName, ascending).
  PROPID _sortID = kpidName;
  bool _ascending = true;

  // --- seamless FS->archive transition state (lifted from QtFsBrowser) ----
  // G.2b : the PARENT STACK — the Qt mirror of CPanel::_parentFolders
  // (PanelItemOpen.cpp:500). The single-level FS<->one-archive case (B.2) is the
  // BOTTOM frame: entering FS->archive pushes frame 0 with returnFolder = the FS
  // folder. Entering an archive INSIDE an archive (G.2b) pushes another frame that
  // holds the NEW (inner) CAgent AND keeps the OUTER CAgent alive via the frame's
  // own agentHolder (the inner archive's sub-stream is a view onto the outer one —
  // every outer agent in the stack stays alive while a nested archive is browsed).
  // onUp pops the top frame and rebinds to the parent frame's folder (or the FS at
  // the bottom). refresh()/the in-place-op re-bind use the TOP-of-stack agent.
  struct CParentFrame
  {
    CMyComPtr<IUnknown> agentHolder;      // the CAgent owning THIS archive level
    CMyComPtr<IFolderFolder> returnFolder; // folder to restore when this level pops
    bool returnIsFs;                       // true at the bottom (returnFolder is FS)
    bool updatable;                        // B.5b : in-place modify allowed at this level
    // G.6a : the path component this archive level contributes to the full folder
    // prefix — the Qt analog of CFolderLink's ParentFolderPath+RelPath
    // (PanelFolderChange.cpp:362-364). For the BOTTOM frame this is the FS path of the
    // host archive file (e.g. /dir/box.zip); for a nested frame it is the entered
    // sub-archive name. Used by currentFolderPrefix() to mirror CPanel::LoadFullPath so
    // the drop-into-archive confirmation shows the same archive path the original does.
    UString hostPath;
    CParentFrame(): returnIsFs(false), updatable(false) {}
  };
  QVector<CParentFrame> _parentStack;
  bool _inArchive;
  bool _isUpdatableArchive;                 // B.5b : in-place modify allowed (top level)

  // G.4c : Flat View per-context remembered defaults (CPanel::_flatModeForArc /
  // _flatModeForDisk, Panel.h:335-336). _flatMode is the LIVE state of the bound
  // folder; on a context transition (FS->archive, or Up back to FS) the panel
  // re-applies the matching default — exactly as the original sets
  // _flatMode = _flatModeForArc on archive open (PanelItemOpen.cpp:508) and
  // _flatMode = _flatModeForDisk on the way out (PanelFolderChange.cpp:1013).
  bool _flatMode = false;
  bool _flatModeForArc = false;
  bool _flatModeForDisk = false;

  // G.4f : ShowDots ".." pseudo-row support. _showDots holds the Options flag
  // (CFmSettings::ShowDots) so applySettings() can re-list live and so each folder
  // change can recompute whether ".." should appear. updateHasParentForDots() (called
  // from onModelReset after every bind) tells the model whether Up is possible at the
  // current folder — the !IsRootFolder() gate: an FS non-root dir, or anywhere inside
  // an archive (incl. the archive root, where Up exits via the parent-stack). The FS
  // filesystem root has no parent, so ".." is suppressed there. _updatingHasParent
  // guards the re-entrant model reset setHasParent() triggers.
  bool _showDots = false;
  bool _updatingHasParent = false;
  void updateHasParentForDots();

  // B.8 : single-click-to-open state (CFmSettings::SingleClick). When on, a
  // single click activates an item (vs the default double-click). Held so
  // applySettings() can connect/disconnect the click->open wiring idempotently.
  bool _singleClick = false;
  QMetaObject::Connection _treeSingleClickConn;
  QMetaObject::Connection _listSingleClickConn;

  // G.6c : the drop-target sub-folder under the drag cursor — the Qt mirror of
  // CPanel::m_DropHighlighted_SubFolderName (Panel.h). Empty = the current dir is the
  // drop destination; non-empty = an FS folder row was hit-tested under the cursor, so
  // dropTargetFsDirPath() appends it (a drop lands INSIDE that folder). Only ever set
  // for an FS folder row (PanelDrag.cpp:1981-1996 IsFsOrPureDrivesFolder + IsItem_Folder).
  UString _dropTargetSubFolder;
  // G.6c : the proxy row currently highlighted as the drop target (LVIS_DROPHILITED
  // analog), -1 = none. Held so clearDropTarget() can deselect exactly that row
  // (CDropTarget::m_DropHighlighted_SelectionIndex, PanelDrag.cpp).
  int _dropHighlightProxyRow = -1;

  // G.6c : the body shared by updateDropTarget()/setDropTargetFromDropPos()/the test
  // hook — resolve the SOURCE row under a hit, gate it to an FS folder, and (when
  // `highlight`) set the drag-over highlight. Returns the resolved FS-folder source
  // row or -1.
  int resolveDropTargetRow(QAbstractItemView *view, const QPoint &viewportPos);

  // B.4b : the shell's drop-dispatch callback (set via setDropHandler).
  QtPanelDropHandler _dropHandler;
  // B.5c : the shell's temp-dir factory (set via setTempDirFactory).
  QtPanelTempDirFactory _tempDirFactory;

  // Encrypted-FM : the shell's shared GUI-thread password prompt (set via
  // setPasswordPrompt), used by the FS->archive open path for a header-encrypted
  // archive. Null in a browser binary / headless run. Not owned.
  QtPasswordPrompt *_passwordPrompt = nullptr;

  // P.1 : non-interactive flag (mirrors the window's _headless). When set, the
  // threaded archive-open worker runs with DisableUserQuestions so its modal
  // progress dialog auto-closes instead of blocking. Set by the window.
  bool _headless = false;
  // P.1 test hook : force the open through the worker thread even for a tiny
  // archive (so the threading itself is exercised under test). Off in the GUI.
  bool _forceThreadOpen = false;

  // G.4a : view-settings persistence (per list-type column layout/sort + per-panel
  // view mode/path). _panelIndex keys the per-panel CListMode/PanelPath; -1 disables
  // persistence (browser/test binaries that never set it keep the old behaviour).
  int _panelIndex = -1;
  // The list-type key the header state is currently restored for (folderTypeId() ->
  // ListTypeKey). restoreViewSettingsForType() re-restores only when this changes,
  // and the save path writes under it. Empty until the first bind.
  UString _currentTypeKey;
  // Coalesces header sectionResized/sectionMoved/sortIndicatorChanged bursts into a
  // single QSettings write (CPanel::SaveListViewInfo on a column change, debounced).
  QTimer *_saveViewTimer = nullptr;
  // True while restoreViewSettingsForType() is programmatically restoring the header
  // (restoreState + setSectionHidden + sortByColumn) — suppresses the save hooks so
  // a restore doesn't immediately re-save (and clobber) the value it just read.
  bool _restoringView = false;

  // G.4e : Auto Refresh state. _autoRefresh is the per-panel flag (CPanel's
  // AutoRefresh_Mode), default ON to match the original (CApp ctor
  // AutoRefresh_Mode(true), App.h:92; not persisted upstream — always on at start).
  // _fsWatcher (created lazily when first enabled) watches the current FS directory
  // and emits directoryChanged -> onDirectoryChanged. _watchedDir is the path
  // currently registered with the watcher (empty = none), so updateWatcher() can
  // drop the old path before adding the new one without re-adding an already-watched dir.
  bool _autoRefresh = true;
  QFileSystemWatcher *_fsWatcher = nullptr;
  QString _watchedDir;

  // G.6d : right-button drag support. The Win32 FM sets m_IsRightButton when a drag
  // BEGINS with MK_RBUTTON|MK_MBUTTON held (DragEnter, PanelDrag.cpp:2401-2402) and,
  // on drop, pops the Copy/Move/Add action menu instead of inferring from modifiers
  // (Drop -> Drag_OnContextMenu, PanelDrag.cpp:2533). Qt's view startDrag() fires only
  // for the LEFT button, so we initiate the right-button drag ourselves: on a right
  // (or middle) press in the viewport we record the press, and once the cursor moves
  // past the start-drag distance we build the panel's drag mime, STAMP the right-drag
  // marker (QtFsDnd::MarkRightButtonDrag) and QDrag::exec it. The drop handler reads
  // the marker to know it must show the menu. _rbuttonPressPos is the press point
  // (viewport-local; (-1,-1) = no right-button press pending); _rbuttonDragView is the
  // view the press landed in (to map positions). startRightButtonDrag() runs the exec.
  QPoint _rbuttonPressPos = QPoint(-1, -1);
  QAbstractItemView *_rbuttonDragView = nullptr;
  void startRightButtonDrag();
};

#endif
