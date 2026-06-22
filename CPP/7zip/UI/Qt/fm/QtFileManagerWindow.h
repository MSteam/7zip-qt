// QtFileManagerWindow.h
// ----------------------------------------------------------------------------
// Milestone B.3 : the two-panel File Manager shell — the Qt analogue of CApp +
// the FM.cpp main window (App.h / FM.cpp). It mirrors:
//
//   * CApp's TWO CPanel side by side in a splitter (App.h: g_App.Panels[2],
//     SplitterPos), here a QSplitter(Horizontal) holding two QtPanel widgets;
//   * the FOCUSED-panel tracking (App.h _lastFocusedPanel / GetFocusedPanel):
//     operations use the focused panel as source and otherPanel() as the
//     copy/move destination (App.h GetAnotherPanel);
//   * the rebar's TWO toolbars (App.cpp g_ArchiveButtons {Add,Extract,Test} +
//     g_StandardButtons {Copy,Move,Delete,Properties/Info}) built from the
//     ORIGINAL bitmaps via FmIcons (the magenta-masked QIcons), with optional
//     text labels (ShowButtonsLables) and a Large/Small toggle (LargeButtons);
//   * the full IDM_MENU menubar (resource.rc): File / Edit / View / Favorites /
//     Tools / Help, item text + accelerators verbatim;
//   * a status bar showing the focused panel's path + selection count/size.
//
// Wired now: Open/Up/address navigation, Add (-> QtCompressGUI/UpdateGUI), Extract
// (-> QtExtractGUI), Test, the CRC submenu (-> QtHashGUI), Select All/Deselect/
// Invert, View Details/List/Large/Small, the Toolbars toggles, Exit, About.
// Also wired (B.5b/G.2/G.5): Copy To / Move To (F5/F6) / Delete / Rename / Create
// Folder / Create File / Properties (incl. multi-select) / Comment / Edit-writeback.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_WINDOW_H
#define ZIP7_INC_QT_FM_WINDOW_H

#include <QtWidgets/QMainWindow>
#include <QtCore/QPoint>          // onPanelContextMenu(QtPanel *, const QPoint &)
#include <QtCore/QVector>         // G.2a : the open-edit watch list
#include <QtCore/QList>           // G.4g : the captured two-panel splitter sizes
#include <QtCore/QString>         // G.2a : the watched temp-file path key

#include "../../../../Common/MyCom.h"      // G.2a : CMyComPtr<IFolderFolder> (edit target)
#include "../../../../Common/MyString.h"

#include "../../FileManager/IFolder.h"     // G.2a : IFolderFolder (the captured edit folder)

#include "QtFmSettings.h"   // B.8 : applySettingsToPanels(const CInfo&)

class QtPanel;
class QSplitter;
class QToolBar;
class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QFileSystemWatcher;   // G.2a : watches an edited-in-archive temp file

class QtFileManagerWindow Z7_final : public QMainWindow
{
  Q_OBJECT
public:
  // leftStart / rightStart : the starting directory paths for the two panels
  // (filesystem). headless : suppress the operation flows' modal dialogs.
  explicit QtFileManagerWindow(const UString &leftStart, const UString &rightStart,
      bool headless, QWidget *parent = nullptr);
  // B.5c : removes the drag-OUT extract-to-temp dirs (session lifetime, mirrors
  // the original FM's remove-temp-dirs-on-close).
  ~QtFileManagerWindow() Z7_override;

  // Test-hook accessors (used by the offscreen scripted self-check).
  QtPanel *leftPanel() const { return _left; }
  QtPanel *rightPanel() const { return _right; }
  QtPanel *focusedPanel() const { return _focused; }
  QtPanel *otherPanel() const;            // App.h GetAnotherPanel

  // === G.4k : dual-pane keyboard cross-panel commands (CApp / CPanelCallbackImp,
  //     App.cpp) — the targets of QtPanel's panel-local accelerators (PanelKey.cpp).
  //     Public so the offscreen --key self-check can drive them without a live key.

  // Tab -> OnTab (App.cpp:42-47): move focus to the OTHER panel's active view.
  void focusOtherPanel();
  // Alt+F1/F2 -> SetFocusToPath(index) (App.cpp:49-57): focus the LEFT(0) / RIGHT(1)
  // panel's path field. With a single logical layout, index maps directly to the
  // left/right panel (the original's NumPanels==1 redirect to LastFocusedPanel does
  // not apply — both panels are always shown).
  void setFocusToPath(int index);
  // Alt+Up -> OnSetSameFolder (App.cpp:858-865): open the focused panel's SAME
  // folder in the OTHER panel. FS-only in the port (navigateToFsPath); a no-op when
  // the focused panel is inside an archive (the original BindToPathAndRefresh would
  // re-open the archive by path, which the port reaches by FS navigation + entry).
  void onSetSameFolder();
  // Alt+Right/Left -> OnSetSubFolder (App.cpp:867-...): open the focused panel's
  // FOCUSED sub-folder in the OTHER panel. FS-only in the port; a no-op unless the
  // focused row is a folder and both panels are on the filesystem.
  void onSetSubFolder();

  // === G.4g : one/two-panel layout toggle (F9 / IDM_VIEW_TWO_PANELS) ===========
  // CApp::SwitchOnOffOnePanel (App.cpp:360): flips NumPanels between 2 and 1. In
  // the port BOTH QtPanel objects always stay alive; going to single-panel HIDES
  // (collapses in the QSplitter) the NON-focused panel rather than destroying it,
  // so otherPanel() — and thus the Copy/Move/Extract "other panel destination"
  // logic — keeps resolving against the hidden panel's last folder. Persisted as
  // NumPanels and restored on startup. Public so the offscreen --one-panel hook
  // can drive it.
  void switchOnOffOnePanel();
  // True when only the focused panel is shown (NumPanels == 1).
  bool isSinglePanel() const { return _numPanels == 1; }

  // === G.4h : Open Root Folder ('\' / IDM_OPEN_ROOT_FOLDER) ====================
  // CPanel::OpenRootFolder -> SetToRootFolder shows CRootFolder, whose sole
  // non-Windows item "Computer" BindToFolder()s a CFSFolder rooted at "/"
  // (RootFolder.cpp:204-209 / FSFolder.h:160 InitToRoot = Init(FSTRING_PATH_SEPARATOR)).
  // So the faithful Linux destination is the filesystem root "/". Navigate the FOCUSED
  // panel there (reusing navigateToFsPath); from inside an archive this leaves to "/"
  // like any FS navigation (drops the archive chain). No mounts/volumes list is
  // invented — the original's non-Windows CRootFolder shows only "/". Public so the
  // menu action AND the offscreen --open-root hook can drive it.
  void openRootFolder();

  // Run one toolbar/menu action programmatically (for the scripted path).
  void doAdd();
  void doExtract();
  void doTest();
  void doHash(const char *methodName);    // methodName == nullptr -> "*"

  // --- B.4a FS operations (test-hooks; also the menu/toolbar action targets) ---
  // copyMode=false: Copy To (moveMode=0); copyMode=true: Move To (moveMode=1).
  // For the scripted path, destOverride (if non-empty) overrides the other-panel
  // destination and confirmName (if non-empty) is the new name for create/rename.
  void doCopyMove(bool moveMode, const UString &destOverride = UString());

  // --- B.4b drag & drop -------------------------------------------------------
  // The drop-dispatch the shell installs on each panel (QtPanelDropHandler):
  // reads the dropped uri-list, decides move-vs-copy (PanelDrag semantics via
  // QtFsDnd::MapDropAction), and dispatches INTERNAL (one of our panels is the
  // source) vs EXTERNAL (paths from another app) — both reusing the B.4a
  // QtFsCopyWorker. Returns true if the drop was handled. Public so the offscreen
  // scripted drop self-check can invoke it directly (no live mouse).
  bool dropOnto(QtPanel *destPanel, const class QMimeData *mime, Qt::DropAction action);

  // G.6d : the right-button-drag action menu (NDragMenu, PanelDrag.cpp:340-381). A
  // drag begun with the right (or middle) button stamps QtFsDnd::MarkRightButtonDrag
  // on its mime; dropOnto() detects it and, instead of inferring move-vs-copy from the
  // Qt DropAction, pops the Copy / Move / Add-to-archive / Cancel menu (the mirror of
  // CDropTarget::Drop -> Drag_OnContextMenu, PanelDrag.cpp:2533-2552) and runs the
  // chosen command.
  enum class DragMenuCmd { None, Cancel, Copy, Move, AddToArc, CopyToArc };

  // Scripted-drop hook (offscreen): build a uri-list mime for `srcPaths`, then run
  // dropOnto() targeting `destPanel` with `action`. Proves the drop->operation
  // wiring without a mouse. Returns true if handled.
  // G.6c : `targetSubFolderRow` (>= 0) forces the dest panel's drop-target sub-folder
  // to that SOURCE row before dispatch (the no-mouse analog of the drag-over hit-test),
  // so a drop lands INSIDE that folder; -1 = drop onto the current dir (the default).
  bool scriptedDrop(QtPanel *destPanel, const UStringVector &srcPaths,
      Qt::DropAction action, int targetSubFolderRow = -1);

  // G.6d : scripted RIGHT-button-drag hook (offscreen). Builds the uri-list mime for
  // `srcPaths`, stamps the right-drag marker, and FORCES the menu result to `cmd`
  // (the no-mouse analog of the user picking a menu entry — a live QMenu::exec cannot
  // run headlessly), then dispatches exactly as a real right-drag drop would. Proves
  // the menu-action dispatch (Copy vs Move vs Add) is reachable and correct. Returns
  // true if the chosen command ran.
  bool scriptedRightDrop(QtPanel *destPanel, const UStringVector &srcPaths,
      DragMenuCmd cmd);
  // B.8 : permanentOverride forces the permanent IFolderOperations::Delete path
  // (the scripted --delete-perm hook) without a real Shift keypress. When false
  // and trash-mode is on and Shift is not held, FS items go to the XDG trash.
  void doDelete(bool permanentOverride = false);
  void doRename(const UString &newNameOverride = UString());

  // B.8 : Options dialog (Tools->Options). Loads QtFmSettings, shows the dialog
  // (skipped when headless), saves on accept, and pushes the view tweaks onto
  // both panels. Public so the scripted path can reach it if needed.
  void doOptions();
  // B.8 : push the Options view tweaks onto both panels (g_App.SetListSettings).
  void applySettingsToPanels(const QtFmSettings::CInfo &s);

  // B.5b test hooks (offscreen scripted in-archive ops): set the focused panel
  // and select a single source row in a panel without a live mouse.
  void focusPanelForTest(QtPanel *p);
  void selectRowForTest(QtPanel *p, int sourceRow);
  // G.5a : select multiple source rows (the multi-select Properties aggregate).
  void selectRowsForTest(QtPanel *p, const QVector<int> &sourceRows);

  // G.9b test hooks : drive + observe the live menu/toolbar retranslate without a
  // dialog. retranslateUiForTest() re-runs the retranslate (post a Lang load), and
  // menuActionTextForTest(menuIndex, actionIndex) returns the current text of the
  // top-level menu menuIndex's actionIndex-th NON-separator action (a POSITION-based
  // lookup, so it survives the menu/action TEXT changing under a language switch).
  // A test reads a label before/after a lang change and proves it changed in place.
  // Empty if the indices are out of range. (File menu = 0, File->Open = action 0.)
  void retranslateUiForTest() { retranslateUi(); }
  QString menuActionTextForTest(int menuIndex, int actionIndex) const;
  // The Archive toolbar's first action (Add) text — proves ReloadToolbars retranslated.
  QString archiveToolBarAddTextForTest() const;

  // G.4d test hooks : read the recorded app-level folder history (most-recent-first),
  // and drive CPanel::FoldersHistory's "navigate to the picked entry" outcome
  // headlessly (the dialog itself needs a live event loop). historyNavigateForTest()
  // navigates the focused panel to history entry `index` (BindToPathAndRefresh),
  // returning false if the index is out of range.
  UStringVector folderHistoryForTest() const { return _folderHistory; }
  bool historyNavigateForTest(int index);

  // G.4i test hooks : force a status-bar recompute and read back the four fields
  // (the count/total-size line, the focused item's size, and its modified date),
  // so a headless run can assert CPanel::Refresh_StatusBar's per-focused-item
  // fields appear. refreshStatusBarForTest() re-runs updateStatusBar() first.
  void refreshStatusBarForTest();
  QString statusSelText() const;       // part 0+1 : "N / total ..." + total size
  QString statusFocSizeText() const;   // part 2 : focused item's size
  QString statusFocDateText() const;   // part 3 : focused item's modified date
  void doCreateFolder(const UString &nameOverride = UString());
  void doCreateFile(const UString &nameOverride = UString());

  // --- B.7a item actions (test-hooks; also the menu/toolbar action targets) ---
  // Properties (Alt+Enter): build the 2-col property sheet for the focused row.
  // Headless dumps "PROP: name = value" to stdout instead of exec()'ing.
  void doProperties();
  // G.5b headless reachability test : drive the dialog's Ctrl+C copy + value
  // viewer over the current selection and print markers (--props-interact).
  void doPropertiesInteractTest(int valueViewerRow);
  // Comment (Ctrl+Z): seed from GetProperty(kpidComment), edit, then
  // IFolderOperations::SetProperty(kpidComment). textOverride drives the headless
  // path (no dialog); commentGet=true just prints the current comment.
  void doComment(const UString &textOverride = UString(), bool commentGet = false);
  // Open-Outside(0) / View(1) / Edit(2): FS item -> xdg-open ($EDITOR for Edit);
  // archive item -> extract-to-temp then open the temp path (View read-only; Edit
  // on an updatable archive writes back via doEditInArchive, G.2a). Headless prints
  // the resolved command.
  enum OpenKind { OpenOutside = 0, View = 1, Edit = 2 };
  void doOpen(int kind);

  // G.2a : headless edit-writeback self-check (the GUI watcher can't be driven
  // without a real editor). Selects <entry> in the focused archive panel, extracts
  // it to a temp file, OVERWRITES that temp with `content` (or env
  // SEVENZQT_EDIT_CONTENT, default "EDITED-BY-G2A\n"), then runs the SAME
  // CopyFromFile writeback path the watcher triggers — directly, bypassing the
  // QFileSystemWatcher. Returns true on a successful write-back.
  bool arcEditWriteBackForTest(int sourceRow, const UString &content = UString());

  // --- B.7b view-mode / arrange-by / select-by-mask/type (test-hooks) -------
  // Select/Deselect by mask (CPanel::SelectSpec): a "*"-seeded mask dialog,
  // then (de)select the focused panel's matching rows. maskOverride drives the
  // headless path (no dialog). Public so main_fm can script it.
  void doSelectByMask(bool selectMode, const UString &maskOverride = UString());
  // Arrange-by (CPanel::SortItemsWithPropID): drive the focused panel's proxy.
  void onArrange(int propID, bool unsorted);

  // --- B.7c Split / Combine / Link / Diff / Favorites (test-hooks) -----------
  // Split (CApp::Split): split the focused FS file into <name>.001/.002/...
  // volOverride drives the headless path (the volume-size string, ParseVolumeSizes
  // -parsed); empty + headless = no-op.
  void doSplit(const UString &volOverride = UString());
  // Combine (CApp::Combine): reassemble the focused <name>.001 part series into
  // <name>. destOverride drives the headless dest dir; empty -> other panel / src.
  void doCombine(const UString &destOverride = UString());
  // Link (CApp::Link / CLinkDialog::OnButton_Link): symlink(2)/link(2). The
  // overrides drive headless: from = link path, to = target, kind = LinkKind
  // (0=symbolic, 1=hard); kind<0 => use the dialog.
  void doLink(const UString &fromOverride = UString(),
      const UString &toOverride = UString(), int kindOverride = -1);
  // G.5f : Edit -> Copy core (CPanel::EditCopy). CRLF-join the focused panel's selected
  // item names and put them on the system clipboard. Public so the menu slot AND the
  // offscreen --edit-copy hook reach the SAME path.
  void editCopySelectedNames();
  // LINK-TARGET test hook (offscreen): for the focused panel's FS item named `name`,
  // build the QtLinkDialog with the OnInit prefill (FilePath = the item), run
  // fillFromState(), and return the read-back current symlink target (QtLinkDialog::
  // CurrentTarget, via readlink(2)). Empty when the item is not an existing symlink.
  UString linkTargetForTest(QtPanel *panel, const UString &name);
  // Diff (CApp::DiffFiles): pick path1/path2, read the [Tools] DiffCommand, launch
  // via QProcess. Headless prints "DIFF_CMD: <cmd> <p1> <p2>".
  void doDiff();
  // Favorites (CPanel::SetBookmark / OpenBookmark): store the focused panel's dir
  // in the first free slot; navigate the focused panel to slot `slot`.
  void favAdd();
  void favGo(int slot);

  // G.10b : the About body text (plain, '\n'-separated). Mirrors AboutDialog.cpp
  // OnInit's surfaced fields: the version line "7-Zip <MY_VERSION_CPU>", the build
  // date MY_DATE, and the homepage URL. Public so the offscreen --about hook can
  // assert the real version/date are shown (instead of exec()'ing the dialog).
  QString aboutText() const;
  static QString aboutHomepageUrl();   // https://www.7-zip.org/ (kHomePageURL)

  // G.10c : Delete Temporary Files (IDM_TEMP_DIR). Purge the FM's own temp working
  // area — the per-window drag/extract dirs createDragTempDir() minted (registered
  // in _dragTempDirs). Returns the number of temp dirs removed. `confirm=false`
  // skips the message box (the headless --delete-temp hook drives this).
  int deleteTempFiles(bool confirm);
  // G.10c test hook : mint one drag temp dir (the same createDragTempDir() a
  // drag-OUT mints) so the offscreen --delete-temp hook has something to purge.
  // Returns the created dir path (empty on failure).
  UString mintTempDirForTest();

private Q_SLOTS:
  void onPanelFocused(QtPanel *p);
  void onPanelSelectionChanged(QtPanel *p);
  void onPanelPathChanged(QtPanel *p);

  // G.4k : the panel-local dual-pane key accelerators (CPanel::OnKeyDown ->
  // _panelCallback, PanelKey.cpp). A panel emits these; the shell runs the
  // cross-panel command, always treating the SIGNALLING panel as the source
  // (CPanelCallbackImp carries the source panel's _index).
  void onPanelTabToOther(QtPanel *p);                  // Tab -> OnTab
  void onPanelSetFocusToPath(QtPanel *p, int index);   // Alt+F1/F2 -> SetFocusToPath
  void onPanelSetSameFolder(QtPanel *p);               // Alt+Up -> OnSetSameFolder
  void onPanelSetSubFolder(QtPanel *p);                // Alt+Right/Left -> OnSetSubFolder
  // A panel asked to open the activated leaf externally (CPanel::OpenItem's
  // "regular file" branch): an FS non-archive file, or any item inside an open
  // archive. Focus the panel and run the plain associated-app open (xdg-open)
  // via doOpen(OpenOutside) on the panel's current selection.
  void onOpenExternally(QtPanel *p, int row);

  // G.3b : a panel's open-as-archive failed with a REAL error (not the benign
  // "not an archive" = S_FALSE). Show the SINGLE faithful error MessageBox
  // (CPanel::OpenAsArc_Msg, PanelItemOpen.cpp:546): IDS_CANT_OPEN_ENCRYPTED_ARCHIVE
  // / IDS_CANT_OPEN_ARCHIVE for the encrypted S_FALSE case, else the decoded
  // HResultToMessage(res). The item is NOT handed to xdg-open. Headless prints a
  // marker (OPEN_ARC_ERROR: ...) instead of exec()'ing.
  void onPanelOpenArchiveError(QtPanel *p, const QString &virtualPath,
      int res, bool encrypted);

  // G.3e : a panel blocked an external launch because the item NAME is a spoof
  // (U+202E RLO / 5+ spaces). Show the IDS_VIRUS warning (CPanel::IsVirus_Message,
  // PanelItemOpen.cpp:921-945). Headless prints a marker (OPEN_VIRUS_BLOCKED: ...).
  void onPanelOpenBlockedAsVirus(QtPanel *p, const QString &originalName,
      const QString &sanitisedName, bool isSpaceError);

  // --- File / toolbar actions ---
  void onOpen();
  void onUp();
  void onSelectAll();
  void onDeselectAll();
  void onInvertSelection();
  void onSelectByType();        // B.7b : CPanel::SelectByType(true)
  void onDeselectByType();      // B.7b : CPanel::SelectByType(false)
  // G.5f : Edit -> Copy (IDM_EDIT_COPY). CRLF-join the focused panel's selected item
  // NAMES (CPanel::EditCopy, PanelMenu.cpp:432-453) and put them on the clipboard
  // (ClipboardSetText -> QClipboard::setText). The only live Edit clipboard verb
  // upstream (Cut/Paste are no-ops even there).
  void onEditCopy();
  void onRefresh();
  void onAbout();
  // G.10c : Tools -> Delete Temporary Files (IDM_TEMP_DIR). Purge the FM's own
  // temp working dirs with a confirmation prompt.
  void onDeleteTemp();
  // G.10d : Help -> Contents (F1). The bundled CHM is Windows-only; the Linux
  // analog opens the 7-zip.org docs URL via QDesktopServices::openUrl.
  void onHelpContents();

  // --- B.4a FS operation action slots (menu / toolbar) ---
  void onCopyTo();
  void onMoveTo();
  void onDelete();
  void onRename();
  void onCreateFolder();
  void onCreateFile();

  // --- B.7a item-action slots (menu / toolbar) ---
  void onProperties();
  void onComment();
  void onOpenOutside();
  void onView();
  void onEdit();

  // G.2a : the QFileSystemWatcher fired — the editor saved an edited-in-archive
  // temp file. Mirrors the Win32 MyThreadFunction "temp changed" branch
  // (PanelItemOpen.cpp:1245): prompt IDS_WANT_UPDATE_MODIFIED_FILE and, on Yes,
  // CopyFromFile the temp back into the archive.
  void onEditedFileChanged(const QString &tempPath);

  // --- B.7c Split / Combine / Link / Diff / Favorites (menu) ---
  void onSplit();
  void onCombine();
  void onLink();
  void onDiff();
  void onOptions();              // B.8 : Tools->Options
  void onBenchmark();            // G.10a : Tools->Benchmark (IDM_BENCHMARK, normal)
  void onBenchmarkTotal();       // G.10a : Tools->Benchmark Total (IDM_BENCHMARK2)
  void onFavAdd();
  void rebuildFavoritesMenu();   // aboutToShow: repopulate the Favorites submenu

  // G.4d : Folders History (Alt+F12 / IDM_FOLDERS_HISTORY). Pops a picker dialog
  // (the CListViewDialog analogue) listing the app-level recent-folders history
  // (most-recent-first); OK / double-click navigates the FOCUSED panel to the
  // selection, Del removes an entry. Mirrors CPanel::FoldersHistory
  // (PanelFolderChange.cpp:866).
  void onFoldersHistory();

  // Per-file context menu (CPanel::OnContextMenu, PanelMenu.cpp:1081). Builds a
  // QMenu from the EXISTING file/toolbar QActions in resource.rc File-menu order,
  // enabling/disabling per the focused panel's selection (CFileMenu::Load,
  // MyLoadMenu.cpp:588), and exec()s it at the click position.
  void onPanelContextMenu(QtPanel *panel, const QPoint &globalPos);

  // --- View ---
  void onViewDetails();
  void onViewList();
  void onViewLargeIcons();
  void onViewSmallIcons();
  // G.4c : View -> Flat View toggle on the focused panel (CPanel::ChangeFlatMode).
  void onFlatView();
  // G.4e : View -> Auto Refresh toggle on the focused panel (CPanel::AutoRefresh_Mode
  // / OnTimer). Enables/disables the FS directory-change watcher for that panel.
  void onAutoRefresh(bool on);
  // G.4c : re-sync the Flat View item's checked + enabled state to the focused
  // panel before the View menu shows (CPanel::OnMenuActivating). Connected to the
  // View QMenu's aboutToShow. Also re-syncs the "2 Panels" checkmark to NumPanels
  // == 2 (CPanel::OnMenuActivating -> CheckItemByID(IDM_VIEW_TWO_PANELS, ...)).
  void onViewMenuAboutToShow();
  // G.4g : View -> 2 Panels (F9). CApp::SwitchOnOffOnePanel via switchOnOffOnePanel().
  void onTwoPanels();
  void onToggleArchiveToolbar(bool on);
  void onToggleStandardToolbar(bool on);
  void onToggleLargeButtons(bool on);
  void onToggleButtonLabels(bool on);

protected:
  // G.2a : on FM close, flush any modified-but-unwritten in-archive edit (prompt
  // IDS_WANT_UPDATE_MODIFIED_FILE + CopyFromFile) before the temp dirs are removed.
  void closeEvent(class QCloseEvent *e) Z7_override;

private:
  void buildMenuBar();
  void buildToolBars();        // App.cpp ReloadToolbars
  void rebuildToolBars();      // re-apply Large/labels (App.cpp SaveToolbarChanges)
  // G.9b : retranslate the live menu + toolbars after a language change in Options
  // (mirrors OptionsDialog.cpp:68-75 MyLoadMenu(true) + g_App.ReloadToolbars() +
  // ReloadLangItems()). Clears + rebuilds the menubar (so every FmLang label is
  // re-resolved through the just-loaded CLang) and re-sets each toolbar action's
  // text; re-syncs the view-mode checkmark to the focused panel. Reachable headless.
  void retranslateUi();
  void retranslateToolBars();  // re-set the toolbar action TEXT (ReloadToolbars)
  void buildStatusBar();
  void wireActions();

  QAction *addCrcAction(class QMenu *menu, const QString &text, const char *method);

  void setFocusedPanel(QtPanel *p);
  void updateStatusBar();

  // The focused panel's selected items as a censor-ready path list (FS only).
  bool collectSelectedFsPaths(UStringVector &out) const;

  // G.2d : in-archive CRC/checksum (PanelCrc.cpp's !Is_IO_FS_Folder branch). Runs
  // CAgentFolder::CopyTo in stream-mode with a CHashBundle on the threaded worker,
  // then shows the digests in QtHashResultsDialog (the same window the FS path uses).
  void doHashInArchive(const char *methodName);

  // --- B.4a FS-operation helpers ---
  // Guard: the modify ops need IFolderOperations. An FS folder always qualifies;
  // an in-archive folder only when it is updatable (B.5b in-place modify), else
  // refuse with a brief message. Also runs the G.5d read-only-target check.
  bool checkFsOpAllowed(const char *opName);
  void refreshBothPanels();

  // B.5b : add FS items into an updatable archive panel via IFolderOperations::
  // CopyFrom (CPanel::CopyFromNoAsk). Returns true if the op ran.
  bool addPathsToArchive(QtPanel *destPanel, const UStringVector &srcPaths,
      bool moveMode);

  // The shared "Add to Archive" launch behind both the Add button (doAdd) and
  // compress-on-drop (compressDroppedFiles). Picks a default archive name in
  // `destDir` (nameHint = the single item's leaf, else the folder leaf) and runs
  // `a -ad <arc> <files...>` through RunFmCommand -> QtCompressGUI (showDialog).
  void compressFilesCore(const UStringVector &files, const UString &destDir,
      const UString &nameHint);

  // G.6b : compress-on-drop. An EXTERNAL uri-list dropped onto an FS panel offers
  // the Add-to-Archive flow (CPanel::CompressDropFiles, createNewArchive==true):
  // the dropped paths become the items to compress, the new archive named from the
  // first dropped item and created in the dest panel's FS dir. Returns true if the
  // compress flow was launched (false only when it could not run, e.g. no dest dir).
  bool compressDroppedFiles(QtPanel *destPanel, const UStringVector &srcPaths);

  // G.6d : build + exec the right-button-drag action menu for `destPanel`, returning
  // the chosen command. The offered set mirrors CDropTarget::Drop's flagsMask
  // (PanelDrag.cpp:2535-2543): an archive target offers Copy-to-archive; an FS target
  // offers Add-to-archive plus (for an FS folder) Copy + Move; the menu always carries
  // a Cancel (g_Pairs). Item text uses FmLang(IDS_COPY/IDS_MOVE/IDS_CONTEXT_COMPRESS)
  // with the IDS_CONTEXT_ARCHIVE suffix on Copy-to-archive (Drag_OnContextMenu). When
  // _dragMenuCmdOverride is set (the scripted hook) the menu is NOT shown and the
  // override is returned instead — a live exec() cannot run headlessly.
  DragMenuCmd showDragMenu(QtPanel *destPanel);
  // G.6d : run the menu-chosen command. Copy/Move -> the FS copy/move drop path (an
  // internal panel source reuses CopyTo, else CopyPathsInto); AddToArc -> the
  // compress-on-drop flow; CopyToArc -> add-into-archive (with the same confirm);
  // Cancel/None -> no-op. Returns true if the command performed an operation.
  bool performDragMenuCmd(QtPanel *destPanel, const UStringVector &srcPaths,
      DragMenuCmd cmd, const UString &destDir);
  // G.6d : the right-button drop path of dropOnto(), split out so the scripted hook
  // can force the menu result. `forcedCmd != None` skips showDragMenu() and uses it.
  bool dropRightButton(QtPanel *destPanel, const UStringVector &srcPaths,
      const UString &destDir, DragMenuCmd forcedCmd);

  // G.6d : the scripted-hook menu-result override (DragMenuCmd::None = no override =
  // show the real menu). Set only by scriptedRightDrop() for the duration of one drop.
  DragMenuCmd _dragMenuCmdOverride = DragMenuCmd::None;

  // B.4b : if `paths` matches a same-process panel's current selection, return
  // that source panel (internal drop); else nullptr (external drop).
  QtPanel *sourcePanelForPaths(const UStringVector &paths) const;

  // G.6e : surface a worker HRESULT from a drop. Mirrors PanelDrag's
  // MessageBox_Error_HRESULT after a failed drag (PanelDrag.cpp:1793-1798): show an
  // error MessageBox for any non-S_OK/non-E_ABORT result; S_OK and E_ABORT (user
  // cancel) are silent. Headless prints a DROP_ERROR marker. Returns true if `res`
  // was an error worth reporting (so the caller can skip the success-path KillSelection).
  bool reportDropError(HRESULT res);

  // === G.2a : Edit (F4) of an in-archive item -> write-back into the archive ===
  // CPanel::EditItem (PanelItemOpen.cpp:820) -> OpenItemInArchive(editMode) extracts
  // the item to a temp, launches the editor, and Thread_Create(MyThreadFunction)
  // watches the temp for changes; on change it prompts IDS_WANT_UPDATE_MODIFIED_FILE
  // and runs CThreadCopyFrom (CopyFromFile). The Linux analog: extract-to-temp,
  // launch the editor, and a QFileSystemWatcher stands in for the Toolhelp32 watch.

  // One tracked open edit. The realIndex is captured at launch but RE-RESOLVED by
  // EntryName against Folder at write-back time (mirrors OnOpenItemChanged's
  // re-scan : the cached index goes stale after any in-place ReOpen()).
  struct CEditWatch
  {
    QtPanel *Panel = nullptr;           // the archive panel the entry lives in
    CMyComPtr<IFolderFolder> Folder;    // the CAgentFolder (its IFolderOperations writes)
    UInt32  RealIndex = 0;              // realIndex at launch (re-resolved on write-back)
    UString EntryName;                  // kpidName, for re-resolution + the prompt {0}
    FString TempPath;                   // the extracted temp file the editor opened
    qint64  OriginalMtime = -1;         // temp mtime at launch (change detection)
    bool    WrittenBack = false;        // already updated (don't prompt again on Up/close)
  };

  // Edit (F4) of a single in-archive item: extract it to a window-owned temp,
  // launch the editor ($VISUAL/$EDITOR via GetEditorCommand(), else xdg-open), and
  // register a QFileSystemWatcher so an editor save triggers the write-back. The
  // faithful CPanel::EditItem -> OpenItemInArchive(editMode) core.
  void doEditInArchive();

  // Begin watching an extracted temp for editor saves. Records a CEditWatch and
  // adds TempPath to _editWatcher. Returns false if it couldn't watch.
  bool startArchiveEditWatch(QtPanel *panel, IFolderFolder *folder,
      UInt32 realIndex, const UString &entryName, const FString &tempPath);

  // The faithful write-back core (CPanel::OnOpenItemChanged -> CThreadCopyFrom):
  // re-resolve the realIndex by w.EntryName, suspend the panel view, run the
  // QtArchiveCopyFromWorker (CopyFromFile), then refresh()/re-bind. Returns the
  // worker HRESULT (S_OK on success).
  HRESULT runEditWriteBack(CEditWatch &w);

  // Mirror OpenParentArchiveFolder (PanelItemOpen.cpp:598): for any tracked-but-
  // unwritten edit whose temp changed and belongs to `panel`, prompt
  // IDS_WANT_UPDATE_MODIFIED_FILE and write back. Called before a panel exits its
  // archive (Up) and on FM close. forceNoPrompt drops the items without prompting.
  void flushPendingEditsForPanel(QtPanel *panel);

  // True if the temp file's mtime differs from w.OriginalMtime (editor saved).
  static bool editTempWasChanged(const CEditWatch &w);

  // G.2a : the open-edit watch list + the single shared watcher (lazily created).
  QVector<CEditWatch>   _openEdits;
  QFileSystemWatcher   *_editWatcher = nullptr;

  // B.5c : portable substitute for the Win32-only CTempDir. Mints a unique temp
  // dir (MyGetTempPath + "7zE" + uniquifier, CreateComplexDir, retry on
  // collision), registers it in _dragTempDirs for removal on FM close, and
  // returns it (trailing separator). Empty FString on failure.
  FString createDragTempDir();

  // --- panels / layout ---
  QtPanel *_left;
  QtPanel *_right;
  QtPanel *_focused;
  QSplitter *_splitter;

  // G.4g : the one/two-panel layout state (App.h NumPanels). 2 = both panels
  // shown; 1 = only the focused panel shown (the OTHER panel is HIDDEN, never
  // destroyed — otherPanel() stays valid). Loaded from QSettings in the ctor,
  // applied after the panels are built, and persisted on every toggle.
  int _numPanels = 2;
  // G.4g : the View -> "2 Panels" checkable item (IDM_VIEW_TWO_PANELS). Re-synced
  // to (NumPanels == 2) on the View menu's aboutToShow (CPanel::OnMenuActivating).
  QAction *_actTwoPanels = nullptr;
  // G.4g : the QSplitter sizes captured the last time BOTH panels were shown, so
  // toggling back to two-panel restores the user's prior split ratio rather than
  // a 50/50 reset.
  QList<int> _twoPanelSizes;
  // G.4g : apply _numPanels to the QSplitter (show/hide the non-focused panel),
  // capturing/restoring the two-panel split. updateMenuSync re-checks the item.
  void applyPanelLayout();

  // --- toolbars (App.cpp rebar) ---
  QToolBar *_archiveToolBar;   // Add / Extract / Test
  QToolBar *_standardToolBar;  // Copy / Move / Delete / Info
  bool _largeButtons;          // App.h LargeButtons
  bool _showButtonLabels;      // App.h ShowButtonsLables

  // --- toolbar/menu actions we re-skin on the Large/Small toggle ---
  QAction *_actAdd;
  QAction *_actExtract;
  QAction *_actTest;
  QAction *_actCopy;
  QAction *_actMove;
  QAction *_actDelete;
  QAction *_actInfo;

  // --- File-menu QActions reused by the per-file context menu (PanelMenu.cpp's
  //     CreateFileMenu mirrors the File popup). Promoted to members so
  //     onPanelContextMenu() can re-add the SAME actions (one handler, one
  //     command set) rather than duplicating them.
  QAction *_actOpen;
  QAction *_actOpenOutside;
  QAction *_actView;
  QAction *_actEdit;
  QAction *_actRename;
  QAction *_actSplit;
  QAction *_actCombine;
  QAction *_actProperties;
  QAction *_actComment;
  QAction *_actDiff;
  QAction *_actCreateFolder;
  QAction *_actCreateFile;
  QAction *_actLink;
  // The File-popup Copy To.../Move To... (IDM_COPY_TO/IDM_MOVE_TO) — reused by the
  // context menu so it shows the original "Copy To..."/"Move To..." labels rather
  // than the toolbar's terse "Copy"/"Move" (same onCopyTo/onMoveTo handlers).
  QAction *_actCopyTo;
  QAction *_actMoveTo;
  QMenu   *_crcMenu;           // the shared CRC submenu (File menu + context menu)

  // --- View icon-mode group (Details/List/Large/Small) ---
  QActionGroup *_viewModeGroup;

  // G.4c : the checkable View -> Flat View item (IDM_VIEW_FLAT_VIEW). Its checked
  // state is synced to the FOCUSED panel's flat state on the View menu's aboutToShow
  // (CPanel::OnMenuActivating -> CheckItemByID(IDM_VIEW_FLAT_VIEW, GetFlatMode()),
  // MyLoadMenu.cpp), and it is grayed when the focused folder can't go flat.
  QAction *_actFlatView;

  // G.4e : the checkable View -> Auto Refresh item (IDM_VIEW_AUTO_REFRESH). Its
  // checked state is synced to the FOCUSED panel's auto-refresh flag on the View
  // menu's aboutToShow (CPanel::OnMenuActivating -> CheckItemByID(
  // IDM_VIEW_AUTO_REFRESH, AutoRefresh_Mode)); toggling it drives that panel's
  // setAutoRefresh(). Default unchecked (the original default is OFF).
  QAction *_actAutoRefresh = nullptr;

  // --- status bar ---
  // Mirrors CPanel::Refresh_StatusBar's parts: _statusSel = part(0) selection count
  // (routed through LangString_N_SELECTED_ITEMS); _statusFocSize = part(2) the
  // FOCUSED item's size; _statusFocDate = part(3) the focused item's modified date.
  // (_statusPath holds the current folder path — the port's own addition; the
  // selection TOTAL size, original part(1), is folded into _statusSel.)
  QLabel *_statusPath;
  QLabel *_statusSel;
  QLabel *_statusFocSize;   // G.4i : focused item's size (Refresh_StatusBar part 2)
  QLabel *_statusFocDate;   // G.4i : focused item's modified date (part 3)

  // --- B.4a : the GUI-thread overwrite prompt the copy/move flow routes to
  //     (mirrors how the extract flow holds a QtOverwritePrompt). Created here so
  //     it has GUI-thread affinity; the worker calls it via BlockingQueuedConnection.
  class QtOverwritePrompt *_overwritePrompt;

  // --- Encrypted-FM : the shared GUI-thread password prompt (CPasswordDialog
  //     analogue). Created here (GUI-thread affinity) and handed to BOTH panels for
  //     the FS->archive OPEN of a header-encrypted archive, and to the archive
  //     extract worker (via the temp-dir factory) for extracting encrypted DATA.
  //     The worker threads invoke it via BlockingQueuedConnection.
  class QtPasswordPrompt *_passwordPrompt;

  bool _headless;

  // B.7c : the dynamic Favorites submenu (rebuilt on aboutToShow), kept as a
  // member so rebuildFavoritesMenu() can repopulate it.
  QMenu *_favMenu;

  // G.4d : the app-level recent-folders history (AppState CFolderHistory). Held
  // MOST-RECENT-FIRST, deduped case-insensitively, capped at kFolderHistoryMax
  // (CFolderHistory::Normalize kMaxSize=100). Loaded from QSettings in the ctor and
  // saved on every record (so it survives restart, like CAppState::Save). A panel
  // navigation funnels through onPanelPathChanged, which records the new FS path here
  // (the Qt analogue of LoadFullPathAndShow's FolderHistory.AddString).
  UStringVector _folderHistory;
  static const unsigned kFolderHistoryMax = 100;   // CFolderHistory::Normalize kMaxSize
  // Push `path` to the head of _folderHistory (AddUniqueStringToHead + Normalize),
  // then persist. No-op for an empty path. Returns true if the list changed.
  void recordFolderHistory(const UString &path);

  // B.5c : the archive drag-OUT extract-to-temp dirs. Session lifetime: removed
  // (RemoveDirWithSubItems) in the destructor, mirroring the original FM's
  // remove-temp-dirs-on-close. NOT removed at drag-end (matching the original's
  // session-temp-dir lifetime; a target keeping handles past FM close is the
  // user's concern, same as upstream).
  FStringVector _dragTempDirs;
};

#endif
