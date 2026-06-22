// QtFileManagerWindow.cpp
// ----------------------------------------------------------------------------
// See QtFileManagerWindow.h.
// ----------------------------------------------------------------------------

#include "QtFileManagerWindow.h"

#include <QtWidgets/QSplitter>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMenu>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>      // G.10b : About -> Homepage button
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QDialog>          // G.4d : Folders History picker (CListViewDialog)
#include <QtWidgets/QListWidget>      // G.4d : the history list
#include <QtWidgets/QDialogButtonBox> // G.4d : OK / Cancel
#include <QtWidgets/QVBoxLayout>      // G.4d : dialog layout
#include <QtWidgets/QTreeView>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>        // G.5f : Edit -> Copy (CPanel::EditCopy)
#include <QtGui/QCursor>           // G.6d : right-drag menu exec at the cursor
#include <QtGui/QActionGroup>
#include <QtGui/QAction>
#include <QtGui/QDesktopServices>   // B.7a : Open-Outside / View (xdg-open)
#include <QtCore/QFileInfo>
#include <QtCore/QMimeData>
#include <QtCore/QUrl>              // B.7a : QUrl::fromLocalFile
#include <QtCore/QProcess>          // B.7a : Edit ($VISUAL/$EDITOR launch)
#include <QtCore/QFileSystemWatcher> // G.2a : watch the edited-in-archive temp file
#include <QtCore/QDateTime>         // G.2a : temp-file mtime change detection
#include <QtGui/QCloseEvent>        // G.2a : flush pending edits on FM close
#include <QtGui/QKeyEvent>          // G.4d : Del in the Folders History list
#include <QtCore/QEvent>            // G.4d : history dialog event filter

#include "../../../../Common/MyString.h"
#include "../../../../Common/StringConvert.h"
#include "../../../../Windows/FileDir.h"     // B.5c : temp-dir create / delete
#include "../../../../Windows/FileFind.h"    // B.5c : CEnumerator (recursive remove)
#include "../../../../Windows/FileName.h"
#include "../../../../Windows/FileIO.h"      // G.2a : COutFile (overwrite temp in --arc-edit)

#include <unistd.h>   // B.5c : getpid() ; B.7c : symlink(2)
#include <cerrno>     // B.7c : errno (link/symlink failure)
#include <cstring>    // B.7c : strerror

#include "../../FileManager/FSFolder.h"

#include "../../../PropID.h"                  // B.7a : kpidComment ; G.5d : kpidReadOnly
#include "../../../../Windows/PropVariant.h"  // B.7a : NCOM::CPropVariant
#include "../../../../Windows/Defs.h"         // G.5d : VARIANT_BOOLToBool (read-only query)

#include "FmIcons.h"
#include "QtPanel.h"
#include "QtPropertiesDialog.h"   // B.7a : the 2-col property sheet
#include "QtFmActions.h"
#include "QtFsOperations.h"
#include "QtFsDnd.h"            // B.4b : drop dispatch / move-vs-copy decision
#include "../QtExtractPrompts.h"
#include "../QtHashGUI.h"           // G.2d : Qt_AddHashBundleRes / CPropNameValPairs
#include "../QtHashResultsDialog.h" // G.2d : the same results window the FS hash path shows
#include "../../Common/HashCalc.h"  // G.2d : CHashBundle

#include "QtSplitCombine.h"     // B.7c : Split/Combine workers + CVolSeqName
#include "QtSplitDialog.h"      // B.7c
#include "QtLinkDialog.h"       // B.7c
#include "QtFavorites.h"        // B.7c : 10-slot FastFolders + Diff command
#include "QtBenchmarkDialog.h"  // G.10a : Tools->Benchmark dialog + k_NumBenchIterations_Default
#include "QtFmSettings.h"       // B.8 : QSettings-backed Options/Editor/Trash
#include "QtOptionsDialog.h"    // B.8 : the Options dialog (QTabWidget)
#include "../QtLang.h"          // P.2 : FmLang(id, english) — CLang translation or fallback
#include "../QtProgressThreadVirt.h"  // G.3b : HResultToMessage (decoded hard open-error text)
#include "../../FileManager/resource.h"  // P.2 : the ORIGINAL IDM_*/IDS_* numeric ids
#include "../../Explorer/resource.h"      // G.6d : IDS_CONTEXT_COMPRESS / IDS_CONTEXT_ARCHIVE (drag menu)
#include "../../FileManager/PropertyNameRes.h"  // P.2 : IDS_PROP_* (arrange-by kIDLangPairs)
#include "../../FileManager/AboutDialogRes.h"    // P.2 : IDD_ABOUT (About caption)
#include "../../GUI/ExtractRes.h"   // G.3b : IDS_CANT_OPEN_ARCHIVE / _ENCRYPTED_ARCHIVE
#include "../../../MyVersion.h"      // G.10b : MY_VERSION_CPU / MY_DATE (real About metadata)
#include "../../Common/LoadCodecs.h" // G.10b : CCodecs / GetCodecsErrorMessage (codec-load error)

#include <QtCore/QFile>           // B.8 : QFile::moveToTrash
#include <QtGui/QGuiApplication>  // B.8 : keyboardModifiers (Shift detect)

using namespace NWindows;

// B.8 : the work-dir overlay setter lives in sevenzip_agent's WorkDirSettings.cpp
// (which must NOT link Qt). The FM reads [Options] WorkDir* from QSettings and
// hands it to the engine through this C-ABI, so CreateTempFile's NWorkDir::CInfo
// ::Load() routes the in-place-archive temp file to the chosen folder.
// G.9c : the second arg is now the full NWorkDir::NMode (0=kSystem, 1=kCurrent,
// 2=kSpecified), not just a System-vs-not bool.
extern "C" void Qt_SetWorkDir(const wchar_t *path, int mode);

// ---------------------------------------------------------------------------

// Forward decl: the IFolderFolder -> IFolderOperations mapper is defined further
// down (with the B.4a operation helpers); doHashInArchive (G.2d), defined above
// it, needs it.
static bool GetFolderOperations(class QtPanel *panel, CMyComPtr<IFolderOperations> &ops);

static QString UStr_toQ(const UString &s)
{
  return QString::fromWCharArray(s.Ptr(), (int)s.Len());
}

// G.3b/G.3e : QString -> UString (the open-error / virus message helpers run before
// the file's later Q_toU2 definition, so a forward-usable local is declared here).
static UString Q_toU_local(const QString &s)
{
  const std::wstring w = s.toStdWString();
  return UString(w.c_str());
}

// Build a CFSFolder rooted at a directory (same shape as the panel/browser).
static bool BindStartFolder(const UString &dir, CMyComPtr<IFolderFolder> &folder)
{
  if (dir.IsEmpty())
    return false;
  FString path = us2fs(dir);
  NFile::NName::NormalizeDirPathPrefix(path);
  NFsFolder::CFSFolder *fsSpec = new NFsFolder::CFSFolder;
  CMyComPtr<IFolderFolder> f = fsSpec;
  if (fsSpec->Init(path) != S_OK)
    return false;
  folder = f;
  return true;
}

// ---------------------------------------------------------------------------

QtFileManagerWindow::QtFileManagerWindow(const UString &leftStart,
    const UString &rightStart, bool headless, QWidget *parent)
  : QMainWindow(parent)
  , _left(nullptr), _right(nullptr), _focused(nullptr), _splitter(nullptr)
  , _archiveToolBar(nullptr), _standardToolBar(nullptr)
  , _largeButtons(true)        // App default: large buttons
  , _showButtonLabels(true)    // App default: labels on
  , _actAdd(nullptr), _actExtract(nullptr), _actTest(nullptr)
  , _actCopy(nullptr), _actMove(nullptr), _actDelete(nullptr), _actInfo(nullptr)
  , _actOpen(nullptr), _actOpenOutside(nullptr), _actView(nullptr), _actEdit(nullptr)
  , _actRename(nullptr), _actSplit(nullptr), _actCombine(nullptr)
  , _actProperties(nullptr), _actComment(nullptr), _actDiff(nullptr)
  , _actCreateFolder(nullptr), _actCreateFile(nullptr), _actLink(nullptr)
  , _actCopyTo(nullptr), _actMoveTo(nullptr)
  , _crcMenu(nullptr)
  , _viewModeGroup(nullptr)
  , _actFlatView(nullptr)
  , _statusPath(nullptr), _statusSel(nullptr)
  , _overwritePrompt(nullptr)
  , _passwordPrompt(nullptr)
  , _headless(headless)
  , _favMenu(nullptr)
{
  // GUI-thread overwrite prompt for the copy/move flow (parented to `this` so it
  // has GUI-thread affinity; the worker invokes it via BlockingQueuedConnection).
  _overwritePrompt = new QtOverwritePrompt(this);
  // Encrypted-FM : the sibling GUI-thread password prompt (parented to `this` for
  // GUI-thread affinity). Shared by the FS->archive OPEN path (header-encrypted
  // archives) and the archive extract worker (encrypted DATA); both worker threads
  // reach it via BlockingQueuedConnection, the GUI thread via DirectConnection.
  _passwordPrompt = new QtPasswordPrompt(this);
  setWindowTitle(QStringLiteral("7-Zip File Manager"));
  setWindowIcon(FmIcons::windowIcon());            // resource.rc IDI_ICON (FM.ico)
  resize(1000, 640);

  // --- two panels in a horizontal splitter (App.h: Panels[2]) -------------
  _left = new QtPanel(this);
  _right = new QtPanel(this);
  _splitter = new QSplitter(Qt::Horizontal, this);
  _splitter->addWidget(_left);
  _splitter->addWidget(_right);
  _splitter->setSizes(QList<int>() << 500 << 500);
  setCentralWidget(_splitter);

  // G.4a : assign each panel its index BEFORE binding a folder, so the per-list-type
  // view-settings restore (onModelReset -> restoreViewSettingsForType, keyed on the
  // panel + folder type) and the per-panel view-mode/path persistence are active for
  // the very first bind (CListMode::Panels[i] / SavePanelPath(i)).
  _left->setPanelIndex(0);
  _right->setPanelIndex(1);

  // Bind each panel to its starting filesystem directory (CFSFolder root). When no
  // explicit launch dir was given for a panel, fall back to its persisted last path
  // (ReadPanelPath) — the original's ReadPanelPath restore on FM startup. An explicit
  // --left/--right or positional dir arg (leftStart/rightStart) always overrides.
  CMyComPtr<IFolderFolder> leftFolder, rightFolder;
  UString leftDir = leftStart, rightDir = rightStart;
  if (leftDir.IsEmpty())
    QtFmSettings::ReadPanelPath(0, leftDir);
  if (rightDir.IsEmpty())
    QtFmSettings::ReadPanelPath(1, rightDir);
  // When NO explicit dir was given AND nothing was persisted, fall back to the
  // current working directory (the long-standing default). This keeps the
  // ReadPanelPath restore reachable: an empty leftStart now means "no explicit
  // launch dir", so the persisted path wins over cwd, exactly as the task asks
  // (explicit --left/positional dir already filled leftStart and overrides both).
  if (leftDir.IsEmpty() || rightDir.IsEmpty())
  {
    FString cwd;
    NFile::NDir::GetCurrentDir(cwd);
    const UString cwdU = fs2us(cwd);
    if (leftDir.IsEmpty())  leftDir  = cwdU;
    if (rightDir.IsEmpty()) rightDir = cwdU;
  }
  if (BindStartFolder(leftDir, leftFolder))
    _left->setRootFolder(leftFolder, /*isFsRoot*/ true);
  if (BindStartFolder(rightDir, rightFolder))
    _right->setRootFolder(rightFolder, /*isFsRoot*/ true);

  // G.4a : restore the persisted per-panel view MODE (CListMode) after the bind.
  _left->restorePersistedViewMode();
  _right->restorePersistedViewMode();

  for (QtPanel *p : { _left, _right })
  {
    // P.1 : tell the panel whether we run non-interactively so the threaded
    // archive-open worker can suppress its modal dialog's blocking (headless).
    p->setHeadless(_headless);
    // Encrypted-FM : hand the panel the shared password prompt so the FS->archive
    // OPEN of a header-encrypted archive can ask for the password.
    p->setPasswordPrompt(_passwordPrompt);
    connect(p, &QtPanel::focused, this, &QtFileManagerWindow::onPanelFocused);
    connect(p, &QtPanel::selectionChanged, this, &QtFileManagerWindow::onPanelSelectionChanged);
    connect(p, &QtPanel::pathChanged, this, &QtFileManagerWindow::onPanelPathChanged);
    // Per-file right-click context menu (CPanel::OnContextMenu).
    connect(p, &QtPanel::contextMenuRequested, this, &QtFileManagerWindow::onPanelContextMenu);
    // CPanel::OpenItem's "regular file" branch: a double-clicked/Opened leaf that
    // is not an archive to enter (an FS non-archive file, or any in-archive item)
    // is opened with its associated program (xdg-open).
    connect(p, &QtPanel::openExternallyRequested, this, &QtFileManagerWindow::onOpenExternally);
    // G.3b : a corrupt / unopenable archive double-clicked -> faithful error dialog
    // (CPanel::OpenAsArc_Msg) instead of silently handing it to xdg-open.
    connect(p, &QtPanel::openArchiveError, this, &QtFileManagerWindow::onPanelOpenArchiveError);
    // G.3e : a name-spoofed item (RLO / 5+ spaces) blocked from external launch
    // (CPanel::IsVirus_Message) -> the IDS_VIRUS warning.
    connect(p, &QtPanel::openBlockedAsVirus, this, &QtFileManagerWindow::onPanelOpenBlockedAsVirus);
    // G.4k : the panel-local dual-pane key accelerators (CPanel::OnKeyDown ->
    // _panelCallback, PanelKey.cpp): Tab / Alt+F1/F2 / Alt+Up / Alt+Right/Left.
    connect(p, &QtPanel::tabToOtherPanel, this, &QtFileManagerWindow::onPanelTabToOther);
    connect(p, &QtPanel::setFocusToPathRequested, this, &QtFileManagerWindow::onPanelSetFocusToPath);
    connect(p, &QtPanel::setSameFolderRequested, this, &QtFileManagerWindow::onPanelSetSameFolder);
    connect(p, &QtPanel::setSubFolderRequested, this, &QtFileManagerWindow::onPanelSetSubFolder);
    // B.4b : install the shell's drop-dispatch on each panel's view.
    p->setDropHandler([this](QtPanel *dest, const QMimeData *mime, Qt::DropAction action) {
      return dropOnto(dest, mime, action);
    });
    // B.5c : install the temp-dir factory for archive drag-OUT (extract-to-temp).
    // The window owns the temp dir (created here, removed on close) and supplies
    // the GUI-thread overwrite prompt + the headless flag the worker needs.
    p->setTempDirFactory([this](QtOverwritePrompt **outPrompt,
        QtPasswordPrompt **outPwPrompt, bool *outHeadless) -> FString {
      if (outPrompt) *outPrompt = _overwritePrompt;
      // Encrypted-FM : also hand back the password prompt so the archive extract
      // worker can ask for the password on encrypted DATA (View/Edit/drag-OUT).
      if (outPwPrompt) *outPwPrompt = _passwordPrompt;
      if (outHeadless) *outHeadless = _headless;
      return createDragTempDir();
    });
    // G.4d : the address-bar dropdown lists the app-level recent-folders history
    // (AppState FolderHistory). Hand each panel a provider that reads it live.
    p->setHistoryProvider([this]() -> UStringVector { return _folderHistory; });
  }

  // G.4d : load the persisted recent-folders history (CAppState::Read ->
  // FolderHistory.Read). Most-recent-first, already deduped/capped from the prior
  // session's saves.
  _folderHistory = QtFmSettings::ReadFolderHistory();

  // G.4g : load the persisted one/two-panel layout preference (App.h NumPanels,
  // CApp::Read). Loaded BEFORE buildMenuBar() so the "2 Panels" item's initial
  // checkmark is correct; the actual show/hide is applied AFTER setFocusedPanel()
  // below (so single-panel collapses the right, non-focused panel).
  _numPanels = QtFmSettings::ReadNumPanels(2);

  buildMenuBar();
  buildToolBars();
  buildStatusBar();

  setFocusedPanel(_left);

  // G.4g : apply the persisted one/two-panel layout now that the focused panel is
  // set (single-panel collapses the non-focused panel; two-panel shows both).
  applyPanelLayout();

  // B.8 : apply the persisted Options view tweaks to both panels at startup
  // (g_App.SetListSettings() is called on FM init in the original), and hand the
  // persisted work dir to the engine (sevenzip_agent) so the in-place-archive
  // rewrite temp file lands in the chosen folder.
  {
    QtFmSettings::CInfo s;
    s.Load();
    applySettingsToPanels(s);
    // G.9e : push the PERSISTED work-dir overlay into the agent at STARTUP, so a
    // custom WorkDir saved in a prior session is honored from first launch (before
    // the user re-opens Options). G.9c : pass the full three-way mode.
    Qt_SetWorkDir(s.WorkDirPath.Ptr(), s.WorkDirMode);
  }
}

// B.5c : recursive directory removal. The engine's NDir::RemoveDirWithSubItems
// is #ifdef _WIN32-only (FileDir.cpp:606-692, NOT compiled on Linux — confirmed
// absent from the engine archive), and CTempDir (its RAII wrapper) is likewise
// _WIN32-only. So mirror RemoveDirWithSubItems' body here with the portable
// primitives that DO build on Linux: NFind::CEnumerator to walk, recurse into
// real subdirs, DeleteFileAlways for files, RemoveDir for the (now-empty) dir.
static bool RemoveDirTreeQt(const FString &path)
{
  FString prefix = path;
  NFile::NName::NormalizeDirPathPrefix(prefix); // trailing separator
  {
    NFile::NFind::CEnumerator e;
    e.SetDirPrefix(prefix);
    NFile::NFind::CDirEntry de;
    bool found;
    while (e.Next(de, found) && found)
    {
      if (de.IsDots())
        continue;
      const FString child = prefix + de.Name;
      if (e.DirEntry_IsDir(de, /*followLink*/ false))
        RemoveDirTreeQt(child);
      else
        NFile::NDir::DeleteFileAlways(child);
    }
  }
  return NFile::NDir::RemoveDir(path);
}

QtFileManagerWindow::~QtFileManagerWindow()
{
  // B.5c : remove the archive drag-OUT extract-to-temp dirs (session lifetime,
  // mirroring the original FM's remove-temp-dirs-on-close).
  // SEVENZQT_KEEP_TEMP (test-only): skip cleanup so the offscreen harness can
  // cmp the extracted bytes after the process exits. Does NOT affect production.
  if (getenv("SEVENZQT_KEEP_TEMP"))
    return;
  FOR_VECTOR (i, _dragTempDirs)
    RemoveDirTreeQt(_dragTempDirs[i]);
}

// G.2a : OpenParentArchiveFolder's WANT_UPDATE check, applied at FM close: any
// in-archive edit whose temp was modified and not yet written back gets the
// prompt + CopyFromFile here, BEFORE the destructor removes the temp dirs.
void QtFileManagerWindow::closeEvent(QCloseEvent *e)
{
  if (_left)  flushPendingEditsForPanel(_left);
  if (_right) flushPendingEditsForPanel(_right);
  QMainWindow::closeEvent(e);
}

QtPanel *QtFileManagerWindow::otherPanel() const
{
  // App.h GetAnotherPanel: the panel that is NOT focused.
  return (_focused == _left) ? _right : _left;
}

// === G.4k : dual-pane keyboard cross-panel commands ==========================
// The panel-local accelerators in QtPanel::eventFilter (Tab / Alt+F1/F2 / Alt+Up /
// Alt+Right/Left) mirror CPanel::OnKeyDown -> _panelCallback (PanelKey.cpp). Here
// the shell runs the cross-panel half (CApp / CPanelCallbackImp, App.cpp), with
// the focused panel as source and otherPanel() as destination (App.h
// GetAnotherPanel / OnSetSameFolder/OnSetSubFolder's 1 - srcPanelIndex).

void QtFileManagerWindow::focusOtherPanel()
{
  // CPanelCallbackImp::OnTab (App.cpp:42-47): Panels[1-_index].SetFocusToList().
  QtPanel *other = otherPanel();
  if (!other)
    return;
  setFocusedPanel(other);   // App.h SetFocusedPanel (focus tracking)
  other->focusActiveView(); // SetFocusToList
}

void QtFileManagerWindow::setFocusToPath(int index)
{
  // CPanelCallbackImp::SetFocusToPath (App.cpp:49-57): focus Panels[index]'s path
  // field. index 0 = left, 1 = right (PanelKey.cpp:74: F1 -> 0, F2 -> 1).
  QtPanel *target = (index == 0) ? _left : _right;
  if (!target)
    return;
  setFocusedPanel(target);
  target->focusAddressField();
}

void QtFileManagerWindow::onSetSameFolder()
{
  // CApp::OnSetSameFolder (App.cpp:858-865): destPanel.BindToPathAndRefresh(
  // srcPanel._currentFolderPrefix). Port: FS-only via navigateToFsPath; an
  // in-archive source is a no-op (the original re-opens by path, which the port
  // reaches by FS navigation + double-click into the archive).
  if (!_focused)
    return;
  // G.4g : faithful to App.cpp:860 — no cross-panel command in single-panel mode
  // (the other panel is hidden; the original returns early on NumPanels <= 1).
  if (_numPanels <= 1)
    return;
  QtPanel *dest = otherPanel();
  if (!dest || dest == _focused)
    return;
  if (_focused->isInArchive())
    return;
  const UString srcPath = _focused->currentFsDirPath();   // trailing separator
  if (srcPath.IsEmpty())
    return;
  if (dest->navigateToFsPath(srcPath))
  {
    setFocusedPanel(dest);
    dest->focusActiveView();
  }
}

void QtFileManagerWindow::onSetSubFolder()
{
  // CApp::OnSetSubFolder (App.cpp:867-...): bind destPanel to srcPanel's FOCUSED
  // sub-folder (a folder => descend; kParentIndex => ascend). Port: FS-only; the
  // model has no ".." list row, so only the descend-into-focused-folder case
  // applies (focusedRowIsParentUp() is always false — see QtPanel.cpp).
  if (!_focused)
    return;
  // G.4g : faithful to App.cpp:869 — no cross-panel command in single-panel mode.
  if (_numPanels <= 1)
    return;
  QtPanel *dest = otherPanel();
  if (!dest || dest == _focused)
    return;
  if (_focused->isInArchive())
    return;
  const UString sub = _focused->focusedSubFolderName();
  if (sub.IsEmpty())
    return;   // no focused folder (IsItem_Folder guard, App.cpp:878)
  UString target = _focused->currentFsDirPath();   // trailing separator
  if (target.IsEmpty())
    return;
  target += sub;
  if (dest->navigateToFsPath(target))
  {
    setFocusedPanel(dest);
    dest->focusActiveView();
  }
}

// G.4k : the panel-signal slots. CPanelCallbackImp carries the source panel's
// _index, so the SIGNALLING panel is always the source — focus it first, then run
// the cross-panel command (which reads _focused as source / otherPanel() as dest).
void QtFileManagerWindow::onPanelTabToOther(QtPanel *p)
{
  setFocusedPanel(p);
  focusOtherPanel();
}
void QtFileManagerWindow::onPanelSetFocusToPath(QtPanel *p, int index)
{
  setFocusedPanel(p);
  setFocusToPath(index);
}
void QtFileManagerWindow::onPanelSetSameFolder(QtPanel *p)
{
  setFocusedPanel(p);
  onSetSameFolder();
}
void QtFileManagerWindow::onPanelSetSubFolder(QtPanel *p)
{
  setFocusedPanel(p);
  onSetSubFolder();
}

// === G.4g : one/two-panel layout toggle (F9 / IDM_VIEW_TWO_PANELS) ===========
// CApp::SwitchOnOffOnePanel (App.cpp:360) flips NumPanels between 2 and 1, then
// hides/shows the NON-focused panel (Panels[1 - LastFocusedPanel].Show(...)). In
// the Qt port both QtPanel objects always stay alive; applyPanelLayout() collapses
// the non-focused panel in the QSplitter (hide the widget) when NumPanels == 1 and
// re-shows it (restoring the prior split ratio) when NumPanels == 2. otherPanel()
// is unchanged ((_focused == _left) ? _right : _left), so it still returns the
// hidden panel — the Copy/Move/Extract "other panel destination" logic keeps
// resolving against that hidden panel's last folder, exactly as the task requires.

void QtFileManagerWindow::applyPanelLayout()
{
  if (!_splitter || !_left || !_right)
    return;
  // The non-focused panel is the one to collapse in single-panel mode (the
  // original hides Panels[1 - LastFocusedPanel]). Fall back to the right panel
  // when nothing is focused yet (ctor-time apply, before setFocusedPanel).
  QtPanel *keep = _focused ? _focused : _left;
  QtPanel *other = (keep == _left) ? _right : _left;

  if (_numPanels == 1)
  {
    // Remember the current two-panel ratio (only if both are currently visible, so
    // a re-entrant apply doesn't capture a collapsed 0-width as the saved ratio).
    if (_left->isVisible() && _right->isVisible())
      _twoPanelSizes = _splitter->sizes();
    other->hide();   // collapse: hidden, NOT destroyed (otherPanel() stays valid)
    keep->show();
  }
  else
  {
    _left->show();
    _right->show();
    // Restore the prior split ratio when we have one; else an even split.
    if (_twoPanelSizes.size() == 2 && (_twoPanelSizes[0] + _twoPanelSizes[1]) > 0)
      _splitter->setSizes(_twoPanelSizes);
    else
      _splitter->setSizes(QList<int>() << 500 << 500);
  }
  if (_actTwoPanels)
    _actTwoPanels->setChecked(_numPanels == 2);
}

void QtFileManagerWindow::switchOnOffOnePanel()
{
  // CApp::SwitchOnOffOnePanel : NumPanels 2 <-> 1.
  _numPanels = (_numPanels == 1) ? 2 : 1;
  applyPanelLayout();
  // App.h NumPanels is persisted by CApp::Save; persist on every toggle so it
  // survives restart (and a crash), matching the per-change-save pattern the port
  // already uses for the panel paths / view mode / folder history.
  QtFmSettings::SaveNumPanels(_numPanels);
}

void QtFileManagerWindow::onTwoPanels()
{
  // The QAction is checkable; a click flips its checked state BEFORE this slot.
  // switchOnOffOnePanel() recomputes _numPanels from its own state and re-syncs the
  // checkmark via applyPanelLayout(), so the action's transient check is overridden
  // to the authoritative value (CApp doesn't trust the menu's check either).
  switchOnOffOnePanel();
}

// === B.5c : portable substitute for the Win32-only CTempDir ==================
// CTempDir (FileDir.h:131-144 / FileDir.cpp:1000-1029) is #ifdef _WIN32-only, so
// it does not compile on this Linux port. Build the same behavior from the
// portable primitives: MyGetTempPath (=> /tmp/ on Linux) + the "7zE" prefix
// (PanelDrag.cpp:76 kTempDirPrefix) + a uniquifier, CreateComplexDir, retry on
// collision. The created dir is registered for removal on FM close.
FString QtFileManagerWindow::createDragTempDir()
{
  FString base;
  if (!NFile::NDir::MyGetTempPath(base))
    return FString();
  const unsigned pid = (unsigned)getpid();
  static unsigned s_counter = 0;
  for (unsigned attempt = 0; attempt < 100; attempt++)
  {
    AString postfix;
    postfix.Add_UInt32(pid);
    postfix.Add_Char('_');
    postfix.Add_UInt32(s_counter++);
    FString dir = base;
    dir += FString("7zE");
    dir += us2fs(GetUnicodeString(postfix));
    if (NFile::NDir::CreateComplexDir(dir))
    {
      NFile::NName::NormalizeDirPathPrefix(dir);
      _dragTempDirs.Add(dir);
      return dir;
    }
  }
  return FString();
}

// === focus tracking (App.h _lastFocusedPanel) ===============================

void QtFileManagerWindow::onPanelFocused(QtPanel *p) { setFocusedPanel(p); }
void QtFileManagerWindow::onPanelSelectionChanged(QtPanel *p)
{
  if (p == _focused) updateStatusBar();
}
void QtFileManagerWindow::onPanelPathChanged(QtPanel *p)
{
  if (p == _focused) updateStatusBar();
  // G.4a : persist the panel's last FILESYSTEM path (SavePanelPath). We store only an
  // FS path (an in-archive virtual path is not a valid FS start dir to restore to,
  // and the original only re-binds an FS-resolvable path on startup). The index is
  // the panel's own (_left=0 / _right=1, assigned at construction).
  if (p && !p->isInArchive() && p->panelIndex() >= 0)
  {
    const UString fsPath = p->currentFsDirPath();   // empty when in an archive
    if (!fsPath.IsEmpty())
      QtFmSettings::SavePanelPath(p->panelIndex(), fsPath);
  }
  // G.4d : record the visited FS folder into the app-level history — the Qt analogue
  // of LoadFullPathAndShow's FolderHistory.AddString(_currentFolderPrefix), which runs
  // after EVERY navigation (address bar / enter / Up / Favorites all reach here via
  // pathChanged). We record from EITHER panel (the history is app-level, AppState).
  // An in-archive virtual path has no FS dir path and is skipped.
  if (p && !p->isInArchive())
    recordFolderHistory(p->currentFsDirPath());
}

// G.4d : CFolderHistory::AddString — AddUniqueStringToHead (dedupe case-insensitively,
// move-to-head) + Normalize (cap at kFolderHistoryMax), then persist (CAppState::Save
// writes FolderHistory each session; we save on every record so it survives a crash).
void QtFileManagerWindow::recordFolderHistory(const UString &path)
{
  if (path.IsEmpty())
    return;
  // AddUniqueStringToHead (App.cpp:981): drop any case-insensitive duplicate, then
  // insert at the head.
  for (unsigned i = 0; i < _folderHistory.Size();)
  {
    if (path.IsEqualTo_NoCase(_folderHistory[i]))
      _folderHistory.Delete(i);
    else
      i++;
  }
  _folderHistory.Insert(0, path);
  // Normalize (App.cpp:992): cap the size.
  if (_folderHistory.Size() > kFolderHistoryMax)
    _folderHistory.DeleteFrom(kFolderHistoryMax);
  QtFmSettings::SaveFolderHistory(_folderHistory);
}
// CPanel::OpenItem's "regular file" branch (PanelItemOpen.cpp): a leaf that is
// not an archive to enter is opened with its OS-associated program. Focus the
// requesting panel (so doOpen acts on the right selection — on a double-click the
// activated row is already the single selection) and run the plain associated-app
// open (OpenOutside; NOT Edit). doOpen handles both the FS path (xdg-open on the
// absolute path) and the in-archive case (extract-to-temp then xdg-open).
void QtFileManagerWindow::onOpenExternally(QtPanel *p, int row)
{
  // Open the ACTIVATED item with its associated app (CPanel::OpenItem -> the
  // ShellExecute / xdg-open branch). Select that exact row first so doOpen acts on
  // it regardless of the ambient selection (a real double-click already selects it;
  // this also makes the path deterministic for the programmatic/headless trigger).
  if (!p)
    return;
  setFocusedPanel(p);
  p->selectSourceRowForTest(row);
  doOpen((int)OpenKind::OpenOutside);
}

// G.3b : a panel's open-as-archive failed with a REAL error (not the benign S_FALSE
// "not an archive"). Faithful mirror of CPanel::OpenAsArc_Msg (PanelItemOpen.cpp
// :546-559): show ONE error MessageBox and DO NOT xdg-open the file. The message is
// IDS_CANT_OPEN_ENCRYPTED_ARCHIVE / IDS_CANT_OPEN_ARCHIVE (the encrypted S_FALSE case,
// with the {0} = virtualFilePath substitution MyFormatNew does) when res == S_FALSE,
// else HResultToMessage(res) for a hard error HRESULT (the original's `else` branch).
void QtFileManagerWindow::onPanelOpenArchiveError(QtPanel *p,
    const QString &virtualPath, int res, bool encrypted)
{
  if (!p)
    return;
  setFocusedPanel(p);

  UString message;
  if ((HRESULT)res == S_FALSE)
  {
    // MyFormatNew(IDS_CANT_OPEN_*_ARCHIVE, virtualFilePath): the {0} placeholder is
    // the file path. FmLang gives the (possibly translated) template; we substitute
    // {0} exactly as MyFormatNew does.
    const unsigned id = encrypted ? IDS_CANT_OPEN_ENCRYPTED_ARCHIVE : IDS_CANT_OPEN_ARCHIVE;
    const QString english = encrypted
        ? QStringLiteral("Cannot open encrypted archive '{0}'. Wrong password?")
        : QStringLiteral("Cannot open file '{0}' as archive");
    const QString q = FmLang(id, english).replace(QStringLiteral("{0}"), virtualPath);
    message = Q_toU_local(q);
  }
  else
  {
    // A hard error HRESULT: the decoded system/HResult text, exactly as the original
    // (HResultToMessage(res) -> NError::MyFormatMessage). The ported helper from
    // QtProgressThreadVirt mirrors that body 1:1.
    message = HResultToMessage((HRESULT)res);
  }

  if (_headless)
  {
    const QByteArray u8 = UStr_toQ(message).toUtf8();
    printf("OPEN_ARC_ERROR: res=0x%08X encrypted=%d %s\n",
        (unsigned)res, encrypted ? 1 : 0, u8.constData());
    fflush(stdout);
    return;
  }
  QMessageBox::critical(this, QStringLiteral("7-Zip"), UStr_toQ(message));
}

// G.3e : a panel blocked an external launch because the item NAME is a spoof. Faithful
// mirror of the message-building tail of CPanel::IsVirus_Message (PanelItemOpen.cpp
// :921-945): start from IDS_VIRUS; when it is NOT the long-spaces case (an RLO-only
// spoof), strip the parenthetical "(the file name contains long spaces in name)"
// clause; then append the sanitised name and the original name, each on its own line.
void QtFileManagerWindow::onPanelOpenBlockedAsVirus(QtPanel *p,
    const QString &originalName, const QString &sanitisedName, bool isSpaceError)
{
  if (!p)
    return;
  setFocusedPanel(p);

  UString s = Q_toU_local(FmLang(IDS_VIRUS,
      QStringLiteral("The file looks like a virus (the file name contains long spaces in name).")));

  // PanelItemOpen.cpp:923-936 : for an RLO-only spoof (not a long-spaces one), remove
  // the "(...)" clause (and a leading " ." artefact) from the message.
  if (!isSpaceError)
  {
    const int pos1 = s.Find(L'(');
    if (pos1 >= 0)
    {
      const int pos2 = s.Find(L')', (unsigned)pos1 + 1);
      if (pos2 >= 0)
      {
        s.Delete((unsigned)pos1, (unsigned)pos2 + 1 - (unsigned)pos1);
        if (pos1 > 0 && s[pos1 - 1] == ' ' && s[pos1] == '.')
          s.Delete(pos1 - 1);
      }
    }
  }

  // PanelItemOpen.cpp:938-943 : newline-flatten both names, then append them.
  UString name3 = Q_toU_local(originalName);
  name3.Replace(L'\n', L'_');
  UString name2 = Q_toU_local(sanitisedName);
  name2.Replace(L'\n', L'_');
  s.Add_LF(); s += name2;
  s.Add_LF(); s += name3;

  if (_headless)
  {
    const QByteArray u8 = UStr_toQ(s).toUtf8();
    printf("OPEN_VIRUS_BLOCKED: spaceError=%d %s\n", isSpaceError ? 1 : 0, u8.constData());
    fflush(stdout);
    return;
  }
  QMessageBox::critical(this, QStringLiteral("7-Zip"), UStr_toQ(s));
}

// === per-file context menu ===================================================
// CPanel::OnContextMenu (PanelMenu.cpp:1081) builds, via CreateFileMenu
// (PanelMenu.cpp:919) + CFileMenu::Load (MyLoadMenu.cpp:588), the same item set
// as the File popup, then enables/disables per the operated selection. Here we
// REUSE the existing File/toolbar QActions (one command set, one set of
// handlers) and add them in resource.rc File-menu order. The Windows-only shell
// (kSystemStartMenuID) and 7-Zip Explorer plugin (kSevenZipStartMenuID /
// IContextMenu) branches have no Linux analogue and are intentionally omitted;
// the FM's own Add/Extract/Test (the g_App archive toolbar) stand in for the
// plugin's compress/extract verbs, shown only outside an archive (CreateFileMenu
// builds the 7-Zip menu only when !IsArcFolder()).
void QtFileManagerWindow::onPanelContextMenu(QtPanel *panel, const QPoint &globalPos)
{
  setFocusedPanel(panel);

  // CFileMenu enable inputs (MyLoadMenu.cpp:633-676):
  const QVector<int> rows = panel->selectedSourceRows();
  const unsigned numItems = (unsigned)rows.size();
  const bool inArchive = panel->isInArchive();
  const bool isFsFolder = !inArchive;
  // allAreFiles == (firstDirIndex == -1): no folder among the operated items.
  bool allAreFiles = true;
  QtFolderModel *model = panel->model();
  for (int r : rows)
    if (model->isFolder(r)) { allAreFiles = false; break; }
  // isOneFsFile drives Split/Combine (MyLoadMenu.cpp:633).
  const bool isOneFsFile = (isFsFolder && numItems == 1 && allAreFiles);
  // readOnly: an FS folder is writable; an in-archive panel that cannot be
  // modified in place is the Linux analogue of IsThereReadOnlyFolder()==true
  // (it disables Rename/MoveTo/Delete/Comment/CreateFolder/CreateFile, exactly
  // CFileMenu's readOnly switch at MyLoadMenu.cpp:636-648).
  const bool readOnly = inArchive && !panel->isUpdatableArchive();
  const bool hasSel = (numItems != 0);

  QMenu menu(this);

  // --- File-popup order (resource.rc:37-78), minus the Windows-only items -----
  // Open / View / Edit (Open Inside has no separate FM action; the menubar wires
  // it to onOpen, so Open covers it).
  menu.addAction(_actOpen);
  menu.addAction(_actOpenOutside);
  menu.addAction(_actView);
  menu.addAction(_actEdit);
  menu.addSeparator();
  // Add / Extract / Test : the FM's archive-toolbar verbs (the Linux stand-in for
  // the 7-Zip plugin's compress/extract). CreateFileMenu only builds these when
  // !IsArcFolder(), so they are hidden inside an archive.
  if (!inArchive)
  {
    menu.addAction(_actAdd);
    menu.addAction(_actExtract);
    menu.addAction(_actTest);
    menu.addSeparator();
  }
  menu.addAction(_actRename);
  menu.addAction(_actCopyTo);        // Copy To...  (File-popup label; same onCopyTo)
  menu.addAction(_actMoveTo);        // Move To...  (File-popup label; same onMoveTo)
  menu.addAction(_actDelete);        // Delete   (toolbar action; same onDelete)
  menu.addSeparator();
  menu.addAction(_actSplit);
  menu.addAction(_actCombine);
  menu.addSeparator();
  menu.addAction(_actProperties);
  menu.addAction(_actComment);
  menu.addMenu(_crcMenu);            // shared CRC submenu (QtHashGUI)
  menu.addAction(_actDiff);
  menu.addSeparator();
  menu.addAction(_actCreateFolder);
  menu.addAction(_actCreateFile);
  menu.addSeparator();
  menu.addAction(_actLink);

  // --- enable/disable (CFileMenu::Load, MyLoadMenu.cpp:633-679) ---------------
  // Open/View/Edit and the file ops need at least one operated item.
  _actOpen->setEnabled(hasSel);
  _actOpenOutside->setEnabled(hasSel);
  _actView->setEnabled(hasSel && allAreFiles);
  _actEdit->setEnabled(hasSel && allAreFiles);
  if (!inArchive)
  {
    _actAdd->setEnabled(hasSel);
    // Extract/Test apply to selected archive files in the FS view.
    _actExtract->setEnabled(hasSel && allAreFiles);
    _actTest->setEnabled(hasSel && allAreFiles);
  }
  // Split / Combine : enabled only for exactly one FS file (MyLoadMenu.cpp:634).
  _actSplit->setEnabled(isOneFsFile);
  _actCombine->setEnabled(isOneFsFile);
  // Diff : two selected files (CApp::DiffFiles). Disabled inside a read-only arc
  // (MyLoadMenu.cpp:669 isHashFolder/readOnly cases gray it; here we gate on the
  // FS-folder requirement + a non-empty selection, matching the FM's onDiff).
  _actDiff->setEnabled(isFsFolder && hasSel);
  // Properties is always available (it reads the focused row; the menubar Info
  // toolbar shares onProperties). Comment is a write -> readOnly disables it.
  _actProperties->setEnabled(true);
  _actComment->setEnabled(hasSel && !readOnly);
  // CRC: at least one operated item.
  _crcMenu->setEnabled(hasSel);
  // Rename / Move / Delete : writes -> disabled when readOnly (MyLoadMenu.cpp:640).
  _actRename->setEnabled(hasSel && !readOnly);
  _actCopyTo->setEnabled(hasSel);               // Copy To is a read (always ok)
  _actMoveTo->setEnabled(hasSel && !readOnly);
  _actDelete->setEnabled(hasSel && !readOnly);
  // Create Folder / Create File : writes into the current folder -> readOnly off.
  _actCreateFolder->setEnabled(!readOnly);
  _actCreateFile->setEnabled(!readOnly);
  // Link : exactly one item (MyLoadMenu.cpp:675) and an FS folder (the FM's
  // onLink works on real paths; an in-archive panel has none).
  _actLink->setEnabled(isFsFolder && numItems == 1);

  menu.exec(globalPos);

  // These QActions are SHARED with the always-enabled menubar File/toolbar (the
  // menubar does not re-gate on aboutToShow; its handlers guard internally and
  // show an info message). The per-selection disabling above is a context-menu
  // affordance only, so restore the shared actions afterwards to avoid leaking a
  // disabled state into the menubar. (_actProperties was left enabled.)
  for (QAction *a : { _actOpen, _actOpenOutside, _actView, _actEdit,
                      _actAdd, _actExtract, _actTest,
                      _actRename, _actCopyTo, _actMoveTo, _actDelete,
                      _actSplit, _actCombine, _actComment, _actDiff,
                      _actCreateFolder, _actCreateFile, _actLink })
    a->setEnabled(true);
  _crcMenu->setEnabled(true);
}

void QtFileManagerWindow::setFocusedPanel(QtPanel *p)
{
  _focused = p;
  updateStatusBar();
}

// === menu bar : verbatim from resource.rc IDM_MENU ===========================

void QtFileManagerWindow::buildMenuBar()
{
  QMenuBar *mb = menuBar();

  // ---- &File -------------------------------------------------------------
  // P.2 : each label is FmLang(<ORIGINAL IDM_*/IDS_* id>, <inline English = the
  // .rc text>). The IDM_* command id IS the langID (MyLoadMenu.cpp:220,249), so a
  // community Lang/*.txt entry like "540  &Öffnen" overrides &Open. Qt shortcuts
  // stay as setShortcut (the txt text is display-only; '\t' accel is stripped).
  QMenu *file = mb->addMenu(FmLang(IDM_FILE, QStringLiteral("&File")));
  QAction *aOpen = file->addAction(FmLang(IDM_OPEN, QStringLiteral("&Open")));
  _actOpen = aOpen;
  aOpen->setShortcut(QKeySequence(Qt::Key_Return));
  connect(aOpen, &QAction::triggered, this, &QtFileManagerWindow::onOpen);
  QAction *aOpenInside = file->addAction(FmLang(IDM_OPEN_INSIDE, QStringLiteral("Open &Inside")));
  aOpenInside->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown));
  connect(aOpenInside, &QAction::triggered, this, &QtFileManagerWindow::onOpen);
  QAction *aOpenOutside = file->addAction(FmLang(IDM_OPEN_OUTSIDE, QStringLiteral("Open O&utside"))); // B.7a
  _actOpenOutside = aOpenOutside;
  aOpenOutside->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return));
  connect(aOpenOutside, &QAction::triggered, this, &QtFileManagerWindow::onOpenOutside);
  QAction *aView = file->addAction(FmLang(IDM_FILE_VIEW, QStringLiteral("&View")));
  _actView = aView;
  aView->setShortcut(QKeySequence(Qt::Key_F3));
  connect(aView, &QAction::triggered, this, &QtFileManagerWindow::onView); // B.7a
  QAction *aEdit = file->addAction(FmLang(IDM_FILE_EDIT, QStringLiteral("&Edit")));
  _actEdit = aEdit;
  aEdit->setShortcut(QKeySequence(Qt::Key_F4));
  connect(aEdit, &QAction::triggered, this, &QtFileManagerWindow::onEdit); // B.7a
  file->addSeparator();
  QAction *aRename = file->addAction(FmLang(IDM_RENAME, QStringLiteral("Rena&me")));
  _actRename = aRename;
  aRename->setShortcut(QKeySequence(Qt::Key_F2));
  connect(aRename, &QAction::triggered, this, &QtFileManagerWindow::onRename); // B.4a
  _actCopyTo = file->addAction(FmLang(IDM_COPY_TO, QStringLiteral("&Copy To...")));
  _actCopyTo->setShortcut(QKeySequence(Qt::Key_F5));
  connect(_actCopyTo, &QAction::triggered, this, &QtFileManagerWindow::onCopyTo); // B.4a
  _actMoveTo = file->addAction(FmLang(IDM_MOVE_TO, QStringLiteral("&Move To...")));
  _actMoveTo->setShortcut(QKeySequence(Qt::Key_F6));
  connect(_actMoveTo, &QAction::triggered, this, &QtFileManagerWindow::onMoveTo); // B.4a
  QAction *aDelete = file->addAction(FmLang(IDM_DELETE, QStringLiteral("&Delete")));
  aDelete->setShortcut(QKeySequence(Qt::Key_Delete));
  connect(aDelete, &QAction::triggered, this, &QtFileManagerWindow::onDelete); // B.4a
  file->addSeparator();
  QAction *aSplit = file->addAction(FmLang(IDM_SPLIT, QStringLiteral("&Split file...")));    // B.7c
  _actSplit = aSplit;
  connect(aSplit, &QAction::triggered, this, &QtFileManagerWindow::onSplit);
  QAction *aCombine = file->addAction(FmLang(IDM_COMBINE, QStringLiteral("Com&bine files..."))); // B.7c
  _actCombine = aCombine;
  connect(aCombine, &QAction::triggered, this, &QtFileManagerWindow::onCombine);
  file->addSeparator();
  QAction *aProps = file->addAction(FmLang(IDM_PROPERTIES, QStringLiteral("P&roperties")));
  _actProperties = aProps;
  aProps->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
  connect(aProps, &QAction::triggered, this, &QtFileManagerWindow::onProperties); // B.7a
  QAction *aComment = file->addAction(FmLang(IDM_COMMENT, QStringLiteral("Comme&nt...")));
  _actComment = aComment;
  aComment->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
  connect(aComment, &QAction::triggered, this, &QtFileManagerWindow::onComment); // B.7a
  // CRC submenu (resource.rc) -> QtHashGUI. The CRC items intentionally stay
  // literal: MyLoadMenu.cpp:219 "we don't need lang change for CRC items".
  QMenu *crc = file->addMenu(QStringLiteral("CRC"));
  _crcMenu = crc;
  addCrcAction(crc, QStringLiteral("CRC-32"), "CRC32");
  addCrcAction(crc, QStringLiteral("CRC-64"), "CRC64");
  addCrcAction(crc, QStringLiteral("XXH64"), "XXH64");
  addCrcAction(crc, QStringLiteral("MD5"), "MD5");
  addCrcAction(crc, QStringLiteral("SHA-1"), "SHA1");
  addCrcAction(crc, QStringLiteral("SHA-256"), "SHA256");
  addCrcAction(crc, QStringLiteral("SHA-384"), "SHA384");
  addCrcAction(crc, QStringLiteral("SHA-512"), "SHA512");
  addCrcAction(crc, QStringLiteral("SHA3-256"), "SHA3-256");
  addCrcAction(crc, QStringLiteral("BLAKE2sp"), "BLAKE2SP");
  addCrcAction(crc, QStringLiteral("*"), "*");
  QAction *aDiff = file->addAction(FmLang(IDM_DIFF, QStringLiteral("Di&ff"))); // B.7c
  _actDiff = aDiff;
  connect(aDiff, &QAction::triggered, this, &QtFileManagerWindow::onDiff);
  file->addSeparator();
  QAction *aCreateFolder = file->addAction(FmLang(IDM_CREATE_FOLDER, QStringLiteral("Create Folder")));
  _actCreateFolder = aCreateFolder;
  aCreateFolder->setShortcut(QKeySequence(Qt::Key_F7));
  connect(aCreateFolder, &QAction::triggered, this, &QtFileManagerWindow::onCreateFolder); // B.4a
  QAction *aCreateFile = file->addAction(FmLang(IDM_CREATE_FILE, QStringLiteral("Create File")));
  _actCreateFile = aCreateFile;
  aCreateFile->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
  connect(aCreateFile, &QAction::triggered, this, &QtFileManagerWindow::onCreateFile); // B.4a
  file->addSeparator();
  QAction *aLink = file->addAction(FmLang(IDM_LINK, QStringLiteral("&Link...")));         // B.7c
  _actLink = aLink;
  connect(aLink, &QAction::triggered, this, &QtFileManagerWindow::onLink);
  file->addAction(FmLang(IDM_ALT_STREAMS, QStringLiteral("&Alternate streams")))->setEnabled(false); // TODO B.4
  file->addSeparator();
  // E&xit : File->Exit maps to the IDCLOSE control id, langID 408 (kLangPairs,
  // LangUtils.cpp:71). English from the dialog/menu literal "E&xit".
  QAction *aExit = file->addAction(FmLang(408, QStringLiteral("E&xit")));
  aExit->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F4));
  connect(aExit, &QAction::triggered, this, &QWidget::close);

  // ---- &Edit -------------------------------------------------------------
  QMenu *edit = mb->addMenu(FmLang(IDM_EDIT, QStringLiteral("&Edit")));
  // G.5f : Edit -> Copy (IDM_EDIT_COPY), at the top of the Edit popup (resource.rc:83,
  // where it is commented out upstream because the Windows shell clipboard verb is a
  // no-op there). CPanel::EditCopy is a LIVE feature; we expose it with the standard
  // Ctrl+C accelerator. Label = IDS_COPY ("Copy"); there is no live IDM_EDIT_COPY langID.
  QAction *aEditCopy = edit->addAction(FmLang(IDS_COPY, QStringLiteral("&Copy")));
  aEditCopy->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_C));
  connect(aEditCopy, &QAction::triggered, this, &QtFileManagerWindow::onEditCopy);
  edit->addSeparator();
  QAction *aSelAll = edit->addAction(FmLang(IDM_SELECT_ALL, QStringLiteral("Select &All")));
  aSelAll->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_A));
  connect(aSelAll, &QAction::triggered, this, &QtFileManagerWindow::onSelectAll);
  QAction *aDesAll = edit->addAction(FmLang(IDM_DESELECT_ALL, QStringLiteral("Deselect All")));
  connect(aDesAll, &QAction::triggered, this, &QtFileManagerWindow::onDeselectAll);
  QAction *aInv = edit->addAction(FmLang(IDM_INVERT_SELECTION, QStringLiteral("&Invert Selection")));
  aInv->setShortcut(QKeySequence(Qt::Key_Asterisk));
  connect(aInv, &QAction::triggered, this, &QtFileManagerWindow::onInvertSelection);
  // B.7b : Select/Deselect by mask (CPanel::SelectSpec, IDM_SELECT/IDM_DESELECT).
  QAction *aSel = edit->addAction(FmLang(IDM_SELECT, QStringLiteral("Select...")));
  connect(aSel, &QAction::triggered, this, [this]{ doSelectByMask(true); });
  QAction *aDes = edit->addAction(FmLang(IDM_DESELECT, QStringLiteral("Deselect...")));
  connect(aDes, &QAction::triggered, this, [this]{ doSelectByMask(false); });
  edit->addSeparator();
  // B.7b : Select/Deselect by type (CPanel::SelectByType).
  QAction *aSelT = edit->addAction(FmLang(IDM_SELECT_BY_TYPE, QStringLiteral("Select by Type")));
  connect(aSelT, &QAction::triggered, this, &QtFileManagerWindow::onSelectByType);
  QAction *aDesT = edit->addAction(FmLang(IDM_DESELECT_BY_TYPE, QStringLiteral("Deselect by Type")));
  connect(aDesT, &QAction::triggered, this, &QtFileManagerWindow::onDeselectByType);

  // ---- &View -------------------------------------------------------------
  QMenu *view = mb->addMenu(FmLang(IDM_VIEW, QStringLiteral("&View")));
  _viewModeGroup = new QActionGroup(this);
  _viewModeGroup->setExclusive(true);
  QAction *vLarge = view->addAction(FmLang(IDM_VIEW_LARGE_ICONS, QStringLiteral("Lar&ge Icons")));
  vLarge->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
  vLarge->setCheckable(true); _viewModeGroup->addAction(vLarge);
  connect(vLarge, &QAction::triggered, this, &QtFileManagerWindow::onViewLargeIcons);
  QAction *vSmall = view->addAction(FmLang(IDM_VIEW_SMALL_ICONS, QStringLiteral("S&mall Icons")));
  vSmall->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
  vSmall->setCheckable(true); _viewModeGroup->addAction(vSmall);
  connect(vSmall, &QAction::triggered, this, &QtFileManagerWindow::onViewSmallIcons);
  QAction *vList = view->addAction(FmLang(IDM_VIEW_LIST, QStringLiteral("&List")));
  vList->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
  vList->setCheckable(true); _viewModeGroup->addAction(vList);
  connect(vList, &QAction::triggered, this, &QtFileManagerWindow::onViewList);
  QAction *vDetails = view->addAction(FmLang(IDM_VIEW_DETAILS, QStringLiteral("&Details")));
  vDetails->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_4));
  vDetails->setCheckable(true); vDetails->setChecked(true); // resource.rc default
  _viewModeGroup->addAction(vDetails);
  connect(vDetails, &QAction::triggered, this, &QtFileManagerWindow::onViewDetails);
  view->addSeparator();
  // B.7b : Arrange-by (CPanel::SortItemsWithPropID). Drives the focused panel's
  // shared proxy; the panel keeps the toggle/default-direction state.
  // Arrange-by labels use the property-name langIDs via kIDLangPairs
  // (MyLoadMenu.cpp:67-70): Name->IDS_PROP_NAME, Type->IDS_PROP_FILE_TYPE,
  // Date->IDS_PROP_MTIME, Size->IDS_PROP_SIZE. Unsorted->IDM_VIEW_ARANGE_NO_SORT.
  QAction *aArrName = view->addAction(FmLang(IDS_PROP_NAME, QStringLiteral("Name")));
  aArrName->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F3));
  connect(aArrName, &QAction::triggered, this, [this]{ onArrange(kpidName, false); });
  QAction *aArrType = view->addAction(FmLang(IDS_PROP_FILE_TYPE, QStringLiteral("Type")));
  aArrType->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F4));
  connect(aArrType, &QAction::triggered, this, [this]{ onArrange(kpidExtension, false); });
  QAction *aArrDate = view->addAction(FmLang(IDS_PROP_MTIME, QStringLiteral("Date")));
  aArrDate->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F5));
  connect(aArrDate, &QAction::triggered, this, [this]{ onArrange(kpidMTime, false); });
  QAction *aArrSize = view->addAction(FmLang(IDS_PROP_SIZE, QStringLiteral("Size")));
  aArrSize->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F6));
  connect(aArrSize, &QAction::triggered, this, [this]{ onArrange(kpidSize, false); });
  QAction *aArrNone = view->addAction(FmLang(IDM_VIEW_ARANGE_NO_SORT, QStringLiteral("Unsorted")));
  aArrNone->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F7));
  connect(aArrNone, &QAction::triggered, this, [this]{ onArrange(kpidNoProperty, true); });
  view->addSeparator();
  // G.4c : Flat View (recursive flat listing). A checkable toggle synced to the
  // focused panel's flat state on the View menu's aboutToShow (CPanel::ChangeFlatMode
  // / OnMenuActivating). Drives the focused panel's flat mode via onFlatView().
  _actFlatView = view->addAction(FmLang(IDM_VIEW_FLAT_VIEW, QStringLiteral("Flat View")));
  _actFlatView->setCheckable(true);
  connect(_actFlatView, &QAction::triggered, this, &QtFileManagerWindow::onFlatView);
  // CPanel::OnMenuActivating re-checks IDM_VIEW_FLAT_VIEW each time the View menu
  // opens; mirror that by re-syncing the item to the focused panel here.
  connect(view, &QMenu::aboutToShow, this, &QtFileManagerWindow::onViewMenuAboutToShow);
  // G.4g : View -> 2 Panels (F9 / IDM_VIEW_TWO_PANELS). A working checkable toggle
  // (CApp::SwitchOnOffOnePanel): checked == two-panel. Re-synced on the View menu's
  // aboutToShow (onViewMenuAboutToShow -> CheckItemByID(IDM_VIEW_TWO_PANELS, ...)).
  _actTwoPanels = view->addAction(FmLang(IDM_VIEW_TWO_PANELS, QStringLiteral("&2 Panels")));
  _actTwoPanels->setShortcut(QKeySequence(Qt::Key_F9));
  _actTwoPanels->setCheckable(true);
  _actTwoPanels->setChecked(_numPanels == 2);
  connect(_actTwoPanels, &QAction::triggered, this, &QtFileManagerWindow::onTwoPanels);
  // Toolbars submenu (App.h toggles).
  QMenu *toolbars = view->addMenu(FmLang(IDM_VIEW_TOOLBARS, QStringLiteral("Toolbars")));
  QAction *tArchive = toolbars->addAction(FmLang(IDM_VIEW_ARCHIVE_TOOLBAR, QStringLiteral("Archive Toolbar")));
  tArchive->setCheckable(true); tArchive->setChecked(true);
  connect(tArchive, &QAction::toggled, this, &QtFileManagerWindow::onToggleArchiveToolbar);
  QAction *tStandard = toolbars->addAction(FmLang(IDM_VIEW_STANDARD_TOOLBAR, QStringLiteral("Standard Toolbar")));
  tStandard->setCheckable(true); tStandard->setChecked(true);
  connect(tStandard, &QAction::toggled, this, &QtFileManagerWindow::onToggleStandardToolbar);
  toolbars->addSeparator();
  QAction *tLarge = toolbars->addAction(FmLang(IDM_VIEW_TOOLBARS_LARGE_BUTTONS, QStringLiteral("Large Buttons")));
  tLarge->setCheckable(true); tLarge->setChecked(_largeButtons);
  connect(tLarge, &QAction::toggled, this, &QtFileManagerWindow::onToggleLargeButtons);
  QAction *tLabels = toolbars->addAction(FmLang(IDM_VIEW_TOOLBARS_SHOW_BUTTONS_TEXT, QStringLiteral("Show Buttons Text")));
  tLabels->setCheckable(true); tLabels->setChecked(_showButtonLabels);
  connect(tLabels, &QAction::toggled, this, &QtFileManagerWindow::onToggleButtonLabels);
  // G.4h : Open Root Folder ('\' / IDM_OPEN_ROOT_FOLDER). CPanel::OpenRootFolder ->
  // SetToRootFolder shows CRootFolder; on non-Windows its sole "Computer" item binds a
  // CFSFolder rooted at "/" (RootFolder.cpp:204-209). So the action navigates the
  // focused panel to the filesystem root "/". The '\' accelerator matches resource.rc.
  QAction *vRoot = view->addAction(FmLang(IDM_OPEN_ROOT_FOLDER, QStringLiteral("Open Root Folder")));
  vRoot->setShortcut(QKeySequence(Qt::Key_Backslash));
  connect(vRoot, &QAction::triggered, this, &QtFileManagerWindow::openRootFolder);
  QAction *vUp = view->addAction(FmLang(IDM_OPEN_PARENT_FOLDER, QStringLiteral("Up One Level")));
  vUp->setShortcut(QKeySequence(Qt::Key_Backspace));
  connect(vUp, &QAction::triggered, this, &QtFileManagerWindow::onUp);
  // G.4d : Folders History (Alt+F12) — CPanel::FoldersHistory. Enabled now: opens the
  // recent-folders picker and navigates the focused panel to the chosen entry.
  QAction *vHistory = view->addAction(FmLang(IDM_FOLDERS_HISTORY, QStringLiteral("Folders History...")));
  vHistory->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F12));
  connect(vHistory, &QAction::triggered, this, &QtFileManagerWindow::onFoldersHistory);
  QAction *vRefresh = view->addAction(FmLang(IDM_VIEW_REFRESH, QStringLiteral("&Refresh")));
  vRefresh->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
  connect(vRefresh, &QAction::triggered, this, &QtFileManagerWindow::onRefresh);
  // G.4e : Auto Refresh — a checkable toggle (CPanel::AutoRefresh_Mode / OnTimer,
  // PanelItems.cpp:1435). When ON for an FS-folder panel it watches the current
  // directory (QFileSystemWatcher, the inotify analogue of the Win32 dir-change
  // notification / IFolderWasChanged poll) and re-lists on a change. Default OFF
  // (the original default). onViewMenuAboutToShow re-syncs the checkmark to the
  // focused panel's flag each time the View menu opens (CPanel::OnMenuActivating).
  _actAutoRefresh = view->addAction(FmLang(IDM_VIEW_AUTO_REFRESH, QStringLiteral("Auto Refresh")));
  _actAutoRefresh->setCheckable(true);
  connect(_actAutoRefresh, &QAction::triggered, this, &QtFileManagerWindow::onAutoRefresh);

  // ---- F&avorites --------------------------------------------------------
  // B.7c : dynamic submenu rebuilt on aboutToShow (mirrors MyLoadMenu.cpp's
  // on-demand Bookmarks submenu). Kept as a member so rebuildFavoritesMenu() can
  // repopulate it.
  _favMenu = mb->addMenu(FmLang(IDM_FAVORITES, QStringLiteral("F&avorites")));
  connect(_favMenu, &QMenu::aboutToShow, this, &QtFileManagerWindow::rebuildFavoritesMenu);
  rebuildFavoritesMenu();

  // ---- &Tools ------------------------------------------------------------
  QMenu *tools = mb->addMenu(FmLang(IDM_TOOLS, QStringLiteral("&Tools")));
  QAction *aOptions = tools->addAction(FmLang(IDM_OPTIONS, QStringLiteral("&Options...")));    // B.8
  connect(aOptions, &QAction::triggered, this, &QtFileManagerWindow::onOptions);
  tools->addSeparator();
  // G.10a : Tools->Benchmark (MyLoadMenu.cpp IDM_BENCHMARK -> MyBenchmark(false)).
  QAction *aBench = tools->addAction(FmLang(IDM_BENCHMARK, QStringLiteral("&Benchmark")));
  connect(aBench, &QAction::triggered, this, &QtFileManagerWindow::onBenchmark);
  // The Total-mode item (IDM_BENCHMARK2 -> MyBenchmark(true)) mirrors MyLoadMenu.cpp:626
  // which shows it only in the diff "super mode" (when a Diff command is configured).
  if (!QtFavorites::GetDiffCommand().IsEmpty())
  {
    // IDM_BENCHMARK2 has no caption of its own; the original re-uses IDM_BENCHMARK's
    // text (MyLoadMenu.cpp:236-238). Append " (Total)" to distinguish the two.
    QAction *aBenchTotal = tools->addAction(
        FmLang(IDM_BENCHMARK, QStringLiteral("&Benchmark")) + QStringLiteral(" (Total)"));
    connect(aBenchTotal, &QAction::triggered, this, &QtFileManagerWindow::onBenchmarkTotal);
  }
  tools->addSeparator();
  // G.10c : Tools -> Delete Temporary Files (IDM_TEMP_DIR). The original manages the
  // 7-Zip temp working dir; the port purges its own per-window drag/extract temp dirs.
  // IDM_TEMP_DIR has no caption string in resource.h (no IDS_ pairing) — stays English.
  QAction *aDelTemp = tools->addAction(QStringLiteral("Delete Temporary Files..."));
  connect(aDelTemp, &QAction::triggered, this, &QtFileManagerWindow::onDeleteTemp);

  // ---- &Help -------------------------------------------------------------
  QMenu *help = mb->addMenu(FmLang(IDM_HELP, QStringLiteral("&Help")));
  QAction *aContents = help->addAction(FmLang(IDM_HELP_CONTENTS, QStringLiteral("&Contents...")));
  aContents->setShortcut(QKeySequence(Qt::Key_F1));
  // G.10d : the bundled CHM is Windows-only; open the 7-zip.org docs URL instead.
  connect(aContents, &QAction::triggered, this, &QtFileManagerWindow::onHelpContents);
  help->addSeparator();
  QAction *aAbout = help->addAction(FmLang(IDM_ABOUT, QStringLiteral("&About 7-Zip...")));
  connect(aAbout, &QAction::triggered, this, &QtFileManagerWindow::onAbout);
}

QAction *QtFileManagerWindow::addCrcAction(QMenu *menu, const QString &text,
    const char *method)
{
  QAction *a = menu->addAction(text);
  const QByteArray m(method);
  connect(a, &QAction::triggered, this, [this, m]() {
    doHash(m.constData());
  });
  return a;
}

// === toolbars : App.cpp ReloadToolbars / g_ArchiveButtons / g_StandardButtons =

void QtFileManagerWindow::buildToolBars()
{
  // Two QToolBars on the same row, mirroring the rebar's archive + standard
  // toolbars. Each button's QIcon comes from the ORIGINAL bitmap via FmIcons
  // (magenta-masked), exactly like App.cpp's imageList.AddMasked path.

  // P.2 : button captions use the toolbar IDS_* (resource.rc:250-256). Tooltips
  // ("Add to archive", "Test archive") have NO STRINGTABLE id — they are not in
  // the upstream toolbar text set — so they stay English.
  _archiveToolBar = addToolBar(QStringLiteral("Archive"));
  _archiveToolBar->setObjectName(QStringLiteral("ArchiveToolBar"));
  _actAdd = _archiveToolBar->addAction(FmLang(IDS_ADD, QStringLiteral("Add")));
  _actAdd->setToolTip(QStringLiteral("Add to archive"));
  connect(_actAdd, &QAction::triggered, this, &QtFileManagerWindow::doAdd);
  _actExtract = _archiveToolBar->addAction(FmLang(IDS_EXTRACT, QStringLiteral("Extract")));
  _actExtract->setToolTip(QStringLiteral("Extract"));
  connect(_actExtract, &QAction::triggered, this, &QtFileManagerWindow::doExtract);
  _actTest = _archiveToolBar->addAction(FmLang(IDS_TEST, QStringLiteral("Test")));
  _actTest->setToolTip(QStringLiteral("Test archive"));
  connect(_actTest, &QAction::triggered, this, &QtFileManagerWindow::doTest);

  _standardToolBar = addToolBar(QStringLiteral("Standard"));
  _standardToolBar->setObjectName(QStringLiteral("StandardToolBar"));
  _actCopy = _standardToolBar->addAction(FmLang(IDS_BUTTON_COPY, QStringLiteral("Copy")));
  connect(_actCopy, &QAction::triggered, this, &QtFileManagerWindow::onCopyTo); // B.4a
  _actMove = _standardToolBar->addAction(FmLang(IDS_BUTTON_MOVE, QStringLiteral("Move")));
  connect(_actMove, &QAction::triggered, this, &QtFileManagerWindow::onMoveTo); // B.4a
  _actDelete = _standardToolBar->addAction(FmLang(IDS_BUTTON_DELETE, QStringLiteral("Delete")));
  connect(_actDelete, &QAction::triggered, this, &QtFileManagerWindow::onDelete); // B.4a
  _actInfo = _standardToolBar->addAction(FmLang(IDS_BUTTON_INFO, QStringLiteral("Info")));
  _actInfo->setToolTip(QStringLiteral("Properties"));
  connect(_actInfo, &QAction::triggered, this, &QtFileManagerWindow::onProperties); // B.7a

  rebuildToolBars();
}

void QtFileManagerWindow::rebuildToolBars()
{
  // Re-skin every button icon from the ORIGINAL bitmaps at the current size,
  // and apply the label/large settings (App.cpp SaveToolbarChanges semantics).
  const bool large = _largeButtons;
  _actAdd->setIcon(FmIcons::toolbarIcon(FmIcons::Button::Add, large));
  _actExtract->setIcon(FmIcons::toolbarIcon(FmIcons::Button::Extract, large));
  _actTest->setIcon(FmIcons::toolbarIcon(FmIcons::Button::Test, large));
  _actCopy->setIcon(FmIcons::toolbarIcon(FmIcons::Button::Copy, large));
  _actMove->setIcon(FmIcons::toolbarIcon(FmIcons::Button::Move, large));
  _actDelete->setIcon(FmIcons::toolbarIcon(FmIcons::Button::Delete, large));
  _actInfo->setIcon(FmIcons::toolbarIcon(FmIcons::Button::Info, large));

  const Qt::ToolButtonStyle style = _showButtonLabels
      ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonIconOnly;
  const QSize iconSize = large ? QSize(48, 36) : QSize(24, 24);
  for (QToolBar *tb : { _archiveToolBar, _standardToolBar })
  {
    tb->setToolButtonStyle(style);
    tb->setIconSize(iconSize);
  }
}

// === G.9b : live retranslate after an Options language change ================
// Mirror OptionsDialog.cpp:68-75 (langPage.LangWasChanged branch): MyLoadMenu(true)
// rebuilds the menu so every command's caption is re-pulled from the active Lang
// table; g_App.ReloadToolbars() re-applies the toolbar button text; ReloadLangItems
// refreshes the rest of the live UI. The translation table (CLang) was already
// re-loaded by doOptions()'s StartupLoadLang() before this runs, so re-resolving
// every FmLang(id, english) here yields the NEW language without a restart.

void QtFileManagerWindow::retranslateToolBars()
{
  // ReloadToolbars analogue : re-set each existing toolbar action's TEXT from its
  // IDS_* langID (the icons/connections are unchanged, so we don't rebuild — that
  // would duplicate the QToolBars). English fallbacks match buildToolBars().
  if (_actAdd)     _actAdd->setText(FmLang(IDS_ADD, QStringLiteral("Add")));
  if (_actExtract) _actExtract->setText(FmLang(IDS_EXTRACT, QStringLiteral("Extract")));
  if (_actTest)    _actTest->setText(FmLang(IDS_TEST, QStringLiteral("Test")));
  if (_actCopy)    _actCopy->setText(FmLang(IDS_BUTTON_COPY, QStringLiteral("Copy")));
  if (_actMove)    _actMove->setText(FmLang(IDS_BUTTON_MOVE, QStringLiteral("Move")));
  if (_actDelete)  _actDelete->setText(FmLang(IDS_BUTTON_DELETE, QStringLiteral("Delete")));
  if (_actInfo)    _actInfo->setText(FmLang(IDS_BUTTON_INFO, QStringLiteral("Info")));
}

void QtFileManagerWindow::retranslateUi()
{
  // --- menu : MyLoadMenu(true) analogue ---------------------------------
  // Remember the focused panel's current view mode so the rebuilt view-mode group
  // (which buildMenuBar seeds to the resource default "Details") is re-checked to
  // the live mode after the rebuild (the original's CheckItemByID on menu (re)load).
  const int viewMode = _focused ? _focused->viewMode() : 3;

  // menuBar()->clear() deletes the old QMenus and their QActions; the member action
  // pointers (_actCopyTo, _crcMenu, _favMenu, ...) are all REASSIGNED by the
  // buildMenuBar() call below, so no dangling pointer survives. The old
  // _viewModeGroup (a QActionGroup child of `this`, NOT owned by the menubar) would
  // otherwise leak and keep stale member actions — delete it first; buildMenuBar
  // makes a fresh one.
  menuBar()->clear();
  delete _viewModeGroup;
  _viewModeGroup = nullptr;
  buildMenuBar();

  // Re-check the view-mode action matching the live mode (group order is
  // Large=0, Small=1, List=2, Details=3 — the order they were addAction'd).
  if (_viewModeGroup)
  {
    const QList<QAction *> modeActions = _viewModeGroup->actions();
    if (viewMode >= 0 && viewMode < modeActions.size())
      modeActions.at(viewMode)->setChecked(true);
  }

  // --- toolbars : ReloadToolbars analogue -------------------------------
  retranslateToolBars();

  // --- the rest of the live UI : ReloadLangItems analogue ---------------
  // Refresh the status bar so any FmLang-derived text the dynamic paths produce
  // (e.g. the "{0} object(s) selected" count template, IDS_N_SELECTED_ITEMS) is
  // rebuilt through the new table. The per-item action enable/disable re-evaluates
  // on the next selection/menu event, so nothing else needs forcing here.
  updateStatusBar();
}

// G.9b test accessor : the current text of a top-level menu's action by POSITION.
// Position-based so it is stable across a language switch (the menu/action TEXT
// changes, but its place in the bar/popup does not). menuIndex selects the Nth
// top-level menu (File=0), actionIndex the Nth NON-separator action within it
// (File->Open = 0). Empty if out of range.
QString QtFileManagerWindow::menuActionTextForTest(int menuIndex,
    int actionIndex) const
{
  QMenuBar *mb = menuBar();
  if (!mb)
    return QString();
  const QList<QAction *> topActions = mb->actions();
  if (menuIndex < 0 || menuIndex >= topActions.size())
    return QString();
  QMenu *m = topActions.at(menuIndex)->menu();
  if (!m)
    return QString();
  int seen = 0;
  const QList<QAction *> items = m->actions();
  for (QAction *a : items)
  {
    if (a->isSeparator())
      continue;
    if (seen == actionIndex)
      return a->text();
    seen++;
  }
  return QString();
}

QString QtFileManagerWindow::archiveToolBarAddTextForTest() const
{
  return _actAdd ? _actAdd->text() : QString();
}

// === status bar ==============================================================

void QtFileManagerWindow::buildStatusBar()
{
  // CPanel::Refresh_StatusBar fills four parts left-to-right: selection-count,
  // selection-total-size, FOCUSED-item-size, FOCUSED-item-modified-date. We keep
  // the port's path label on the left (stretching), then the same four fields as
  // permanent (right-anchored) widgets: selection (count + total size folded in),
  // focused size, focused date.
  _statusPath = new QLabel(this);
  _statusSel = new QLabel(this);
  _statusFocSize = new QLabel(this);
  _statusFocDate = new QLabel(this);
  statusBar()->addWidget(_statusPath, 1);
  statusBar()->addPermanentWidget(_statusSel);
  statusBar()->addPermanentWidget(_statusFocSize);
  statusBar()->addPermanentWidget(_statusFocDate);
  updateStatusBar();
}

void QtFileManagerWindow::updateStatusBar()
{
  if (!_focused || !_statusPath)
    return;
  const QString path = UStr_toQ(_focused->currentPath());
  _statusPath->setText(path.isEmpty() ? QStringLiteral("(root)") : path);

  QtFolderModel *m = _focused->model();

  // Selection count + total size. Part 1 (PanelListNotify.cpp:791-794) sums
  // GetItemSize over ALL operated indices (a folder contributes its reported size,
  // typically 0), so we accumulate every selected row's kpidSize.
  const QVector<int> rows = _focused->selectedSourceRows();
  UInt64 totalSize = 0;
  const int objects = rows.size();
  for (int r : rows)
  {
    bool def = false;
    const UInt64 sz = m->itemSize(r, def);
    if (def) totalSize += sz;
  }

  // Part 0 (CPanel::Refresh_StatusBar, PanelListNotify.cpp:772-782): the count line
  // is MyFormatNew(LangString_N_SELECTED_ITEMS, "N / total"), where N = selected
  // count and total = item count in the folder. IDS_N_SELECTED_ITEMS=3002 has the
  // template "{0} object(s) selected"; we substitute the "N / total" string for {0}
  // exactly as MyFormatNew does, so the translated template keeps the localized
  // surrounding words (e.g. "3 / 100 object(s) selected").
  // G.4f : the total is the REAL engine item count (CPanel uses
  // _selectedStatusVector.Size() == numItems), NOT rowCount() — which includes the
  // ".." pseudo-row when ShowDots is on. Excluding ".." keeps the count faithful.
  const int total = m->realItemCount();
  const QString countArg = QStringLiteral("%1 / %2").arg(objects).arg(total);
  QString sel = FmLang(IDS_N_SELECTED_ITEMS, QStringLiteral("{0} object(s) selected"))
      .replace(QStringLiteral("{0}"), countArg);
  // Part 1 (PanelListNotify.cpp:784-797): the selection's total size, grouped, shown
  // only when something is selected (selectSizeString stays empty otherwise). Folded
  // onto the same label after the count.
  if (objects > 0)
    sel += QStringLiteral("   ") + m->sizeStringForBytes(totalSize);
  _statusSel->setText(sel);

  // Parts 2 & 3 (PanelListNotify.cpp:800-828): the FOCUSED item's size and modified
  // date. The original shows them only when GetSelectedCount() > 0 AND the focused
  // row is a real item (not the parent ".." row, which the port has no model row
  // for). A missing MTime leaves the date blank, exactly as the original.
  QString focSize, focDate;
  if (objects > 0)
  {
    const int frow = _focused->focusedSourceRow();
    if (frow >= 0)
    {
      focSize = m->focusedSizeString(frow);
      focDate = m->focusedMTimeString(frow);
    }
  }
  _statusFocSize->setText(focSize);
  _statusFocDate->setText(focDate);
}

// === navigation actions ======================================================

void QtFileManagerWindow::onOpen()
{
  if (!_focused) return;
  // Open == enter the current row (folder) / open an archive (CPanel::OpenItem).
  // The panel's view Enter-key handler already does this; here we trigger it via
  // the selected row.
  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.isEmpty()) return;
  // Use double-click semantics on the first selected row.
  QtFolderModel *m = _focused->model();
  if (m->isFolder(rows.first()))
    m->enterItem(rows.first());
  else
    m->enterItem(rows.first()); // emits leafActivated -> seamless archive open
  updateStatusBar();
}

void QtFileManagerWindow::onUp()
{
  if (!_focused)
    return;
  // G.2a / OpenParentArchiveFolder (PanelItemOpen.cpp:598): before this panel
  // leaves an archive level, prompt to write back any modified-but-unwritten edit
  // (the temp's editor saved while we were inside). Faithful to the original, which
  // runs the WANT_UPDATE check as it pops each archive folder level.
  if (_focused->isInArchive())
    flushPendingEditsForPanel(_focused);
  _focused->onUp();
}

// G.4h : Open Root Folder ('\' / IDM_OPEN_ROOT_FOLDER). CPanel::OpenRootFolder ->
// SetToRootFolder shows CRootFolder; its single non-Windows item "Computer"
// BindToFolder()s a CFSFolder rooted at "/" (RootFolder.cpp:204-209 / FSFolder.h:160
// InitToRoot = Init(FSTRING_PATH_SEPARATOR)). So the faithful Linux destination is the
// filesystem root "/". Navigate the FOCUSED panel there via navigateToFsPath (the same
// BindStartFolder path the address bar / Favorites use). When the focused panel is
// inside an archive, navigating to "/" releases the archive exactly like a normal FS
// move (navigateToFsPath clears _inArchive / _parentStack). We do NOT invent a
// mounts/volumes list — the original's non-Windows CRootFolder shows only "/".
void QtFileManagerWindow::openRootFolder()
{
  if (!_focused)
    return;
  // G.2a : if leaving an archive level, flush any modified-but-unwritten in-archive
  // edit first (the same WANT_UPDATE check onUp() runs as it pops a level).
  if (_focused->isInArchive())
    flushPendingEditsForPanel(_focused);
  _focused->navigateToFsPath(UString(WSTRING_PATH_SEPARATOR));   // CRootFolder "Computer" -> "/"
}

// === Edit: selection actions =================================================

void QtFileManagerWindow::onSelectAll()
{
  if (!_focused) return;
  QTreeView *v = _focused->findChild<QTreeView *>();
  if (v) v->selectAll();
}

void QtFileManagerWindow::onDeselectAll()
{
  if (!_focused) return;
  QTreeView *v = _focused->findChild<QTreeView *>();
  if (v) v->clearSelection();
}

void QtFileManagerWindow::onInvertSelection()
{
  if (!_focused) return;
  QTreeView *v = _focused->findChild<QTreeView *>();
  if (!v) return;
  QAbstractItemModel *pm = v->model();
  if (!pm) return;
  const int rows = pm->rowCount();
  const int cols = pm->columnCount();
  if (rows == 0 || cols == 0) return;
  QItemSelection inv(pm->index(0, 0), pm->index(rows - 1, cols - 1));
  v->selectionModel()->select(inv,
      QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
}

// G.5f : Edit -> Copy (CPanel::EditCopy, PanelMenu.cpp:432-453). CRLF-join ("\xD\n")
// the focused panel's SELECTED item NAMES (GetItemName per selected index) and put the
// result on the system clipboard (ClipboardSetText -> QClipboard::setText). No-op with
// no selection. (Cut/Paste are intentionally NOT added — they are no-ops upstream.)
void QtFileManagerWindow::editCopySelectedNames()
{
  if (!_focused)
    return;
  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.isEmpty())
    return;
  QtFolderModel *model = _focused->model();
  UString s;
  for (int i = 0; i < rows.size(); i++)
  {
    if (i != 0)
      s += "\xD\n";          // CR LF, exactly EditCopy's separator (PanelMenu.cpp:449)
    s += model->itemName(rows[i]);
  }
  QString text = UStr_toQ(s);
  if (_headless)
  {
    // The G.5f test reads this marker back instead of poking the live clipboard.
    printf("EDIT_COPY_CLIPBOARD: %s\n", text.toUtf8().constData());
    fflush(stdout);
  }
  QClipboard *cb = QApplication::clipboard();
  if (cb)
    cb->setText(text);
}
void QtFileManagerWindow::onEditCopy() { editCopySelectedNames(); }

void QtFileManagerWindow::onRefresh()
{
  if (_focused) _focused->refresh();
}

// === View: icon modes (B.7b) =================================================
// CPanel::SetListViewMode (Panel.cpp:871): 0=Large 1=Small 2=List 3=Details.
// The panel owns a QStackedWidget of a QTreeView (Details) + a QListView
// (icon/list), sharing one proxy + one selection model. Mode is per-panel and
// in-memory (persistence is B.8).
void QtFileManagerWindow::onViewLargeIcons() { if (_focused) _focused->setViewMode(0); }
void QtFileManagerWindow::onViewSmallIcons() { if (_focused) _focused->setViewMode(1); }
void QtFileManagerWindow::onViewList()       { if (_focused) _focused->setViewMode(2); }
void QtFileManagerWindow::onViewDetails()    { if (_focused) _focused->setViewMode(3); }

// === View: Flat View (G.4c) ==================================================
// CPanel::ChangeFlatMode (Panel.cpp:894-902): toggle the focused panel's flat mode
// (re-list recursively, showing the kpidPrefix path column). The triggered check
// state Qt sets on the action is advisory only — toggleFlatMode() flips the panel's
// real state and onViewMenuAboutToShow() re-syncs the checkmark next open.
void QtFileManagerWindow::onFlatView()
{
  if (_focused)
    _focused->toggleFlatMode();
}

// CPanel::OnMenuActivating re-checks IDM_VIEW_FLAT_VIEW from GetFlatMode() each time
// the View menu opens; we mirror that here. Also gray the item when the focused
// folder can't go flat (faithful: the original offers flat mode only where the
// IFolderSetFlatMode interface exists).
void QtFileManagerWindow::onViewMenuAboutToShow()
{
  if (_actFlatView)
  {
    const bool supported = _focused && _focused->flatModeSupported();
    _actFlatView->setEnabled(supported);
    // setChecked without firing triggered (no signal from setChecked).
    _actFlatView->setChecked(supported && _focused->flatMode());
  }
  // G.4e : re-sync the Auto Refresh checkmark to the focused panel's flag
  // (CPanel::OnMenuActivating -> CheckItemByID(IDM_VIEW_AUTO_REFRESH, AutoRefresh_Mode)).
  // Gray it inside an archive — an archive folder is static, so auto-refresh has no
  // effect there (the original's IFolderWasChanged is a no-op for an archive too).
  if (_actAutoRefresh)
  {
    const bool fsPanel = _focused && !_focused->isInArchive();
    _actAutoRefresh->setEnabled(fsPanel);
    _actAutoRefresh->setChecked(_focused && _focused->isAutoRefresh());
  }
  // G.4g : re-sync the "2 Panels" checkmark to the layout state
  // (CPanel::OnMenuActivating -> CheckItemByID(IDM_VIEW_TWO_PANELS, NumPanels == 2),
  // MyLoadMenu.cpp:430). setChecked fires no triggered signal, so no toggle loop.
  if (_actTwoPanels)
    _actTwoPanels->setChecked(_numPanels == 2);
}

// G.4e : View -> Auto Refresh toggled. Drive the focused panel's per-panel
// AutoRefresh_Mode (CPanel::AutoRefresh_Mode); the panel (re)points or drops its
// FS directory-change watcher. The check state Qt set on the action is advisory —
// setAutoRefresh() is the real state, and onViewMenuAboutToShow() re-syncs the
// checkmark next open.
void QtFileManagerWindow::onAutoRefresh(bool on)
{
  if (_focused)
    _focused->setAutoRefresh(on);
}

// === Edit: select / deselect by mask & by type (B.7b) ========================
// CPanel::SelectSpec (PanelSelect.cpp:154) : a mask-input dialog (default "*"),
// then (de)select every focused-panel row whose name matches the wildcard.
void QtFileManagerWindow::doSelectByMask(bool selectMode, const UString &maskOverride)
{
  if (!_focused) return;
  UString mask = maskOverride;
  if (mask.IsEmpty())
  {
    if (_headless)
      return;   // no dialog in headless mode unless an override is supplied
    bool ok = false;
    const QString t = QInputDialog::getText(this,
        selectMode ? QStringLiteral("Select") : QStringLiteral("Deselect"),
        QStringLiteral("Mask:"), QLineEdit::Normal, QStringLiteral("*"), &ok);
    if (!ok)
      return;
    const std::wstring w = t.toStdWString();
    mask = UString(w.c_str());
  }
  if (mask.IsEmpty())
    return;
  _focused->selectByMask(mask, selectMode);
}

void QtFileManagerWindow::onSelectByType()   { if (_focused) _focused->selectByType(true); }
void QtFileManagerWindow::onDeselectByType() { if (_focused) _focused->selectByType(false); }

// === View: arrange-by (B.7b) =================================================
// CPanel::SortItemsWithPropID (PanelSort.cpp:256). Drives the focused panel's
// shared proxy; the panel keeps the _sortID/_ascending toggle state.
void QtFileManagerWindow::onArrange(int propID, bool unsorted)
{
  if (_focused) _focused->arrangeBy(propID, unsorted);
}

// === View: toolbar toggles (App.h ShowArchiveToolbar/...) ====================
void QtFileManagerWindow::onToggleArchiveToolbar(bool on)
{
  if (_archiveToolBar) _archiveToolBar->setVisible(on);
}
void QtFileManagerWindow::onToggleStandardToolbar(bool on)
{
  if (_standardToolBar) _standardToolBar->setVisible(on);
}
void QtFileManagerWindow::onToggleLargeButtons(bool on)
{
  _largeButtons = on;
  rebuildToolBars();
}
void QtFileManagerWindow::onToggleButtonLabels(bool on)
{
  _showButtonLabels = on;
  rebuildToolBars();
}

// === About ===================================================================
// G.10b : the global codecs object the engine Agent layer sets up on the first
// archive open (Agent.cpp:78). AboutDialog.cpp surfaces its external-codec load
// error here; we mirror that (NULL-safe, exactly as the original's `if (g_CodecsObj)`).
extern CCodecs *g_CodecsObj;

// G.10b : the 7-Zip homepage (AboutDialog.cpp kHomePageURL). ShellExecute on
// Windows; QDesktopServices::openUrl here.
QString QtFileManagerWindow::aboutHomepageUrl()
{
  return QStringLiteral("https://www.7-zip.org/");
}

// G.10b : the About body — the SAME fields the original AboutDialog surfaces
// (AboutDialog.cpp OnInit): "7-Zip " MY_VERSION_CPU (IDT_ABOUT_VERSION) and the
// build date MY_DATE (IDT_ABOUT_DATE). The Homepage URL follows so the line is
// readable in the plain-text headless dump.
QString QtFileManagerWindow::aboutText() const
{
  QString s = QStringLiteral("7-Zip ") + QString::fromLatin1(MY_VERSION_CPU);
  s += QLatin1Char('\n');
  s += QString::fromLatin1(MY_DATE);
  s += QLatin1Char('\n');
  s += aboutHomepageUrl();
  return s;
}

void QtFileManagerWindow::onAbout()
{
  // AboutDialog.cpp OnInit : if external codecs failed to load, show the codec
  // error message FIRST (NULL-safe; g_CodecsObj is set on the first archive open).
  #ifdef Z7_EXTERNAL_CODECS
  if (g_CodecsObj)
  {
    UString err;
    g_CodecsObj->GetCodecsErrorMessage(err);
    if (!err.IsEmpty())
      QMessageBox::critical(this, QStringLiteral("7-Zip"), UStr_toQ(err));
  }
  #endif

  QMessageBox box(this);
  // P.2 : the About dialog caption (IDD_ABOUT=2900; English "About 7-Zip" from the
  // AboutDialog.rc CAPTION).
  box.setWindowTitle(FmLang(IDD_ABOUT, QStringLiteral("About 7-Zip")));
  box.setIconPixmap(FmIcons::aboutLogo().pixmap(110, 63)); // 7zipLogo.ico
  // G.10b : the REAL version line + build date (was a hardcoded "milestone B.3"
  // string). Matches AboutDialog.cpp's IDT_ABOUT_VERSION / IDT_ABOUT_DATE items.
  box.setText(QStringLiteral("<b>7-Zip ") + QString::fromLatin1(MY_VERSION_CPU)
      + QStringLiteral("</b><br>") + QString::fromLatin1(MY_DATE)
      + QStringLiteral("<br><br>")
      + QStringLiteral("<a href=\"%1\">%1</a>").arg(aboutHomepageUrl()));
  box.setTextFormat(Qt::RichText);
  // AboutDialog.cpp IDB_ABOUT_HOMEPAGE : a button opening kHomePageURL via
  // ShellExecute. Here a "Homepage" button -> QDesktopServices::openUrl (the same
  // openUrl B.7a already uses for xdg-open). The inline <a> link is also clickable.
  QPushButton *home = box.addButton(QStringLiteral("Homepage"), QMessageBox::ActionRole);
  box.addButton(QMessageBox::Ok);
  box.setDefaultButton(QMessageBox::Ok);
  box.exec();
  if (box.clickedButton() == home)
    QDesktopServices::openUrl(QUrl(aboutHomepageUrl()));
}

// === G.10c : Delete Temporary Files (IDM_TEMP_DIR) ===========================
// Purge the FM's own temp working area: the per-window drag/extract dirs
// createDragTempDir() minted (each registered in _dragTempDirs and otherwise only
// removed on FM close). Low-risk — touches ONLY the FM's own temp dirs, never the
// user's files. Returns how many dirs were removed.
int QtFileManagerWindow::deleteTempFiles(bool confirm)
{
  const int n = (int)_dragTempDirs.Size();
  if (confirm)
  {
    const QString msg = (n == 0)
        ? QStringLiteral("There are no temporary files to delete.")
        : QStringLiteral("Delete %1 temporary working folder(s) created by this "
            "session?").arg(n);
    if (n == 0)
    {
      QMessageBox::information(this, QStringLiteral("7-Zip"), msg);
      return 0;
    }
    if (QMessageBox::question(this, QStringLiteral("7-Zip"), msg,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)
        != QMessageBox::Yes)
      return 0;
  }
  FOR_VECTOR (i, _dragTempDirs)
    RemoveDirTreeQt(_dragTempDirs[i]);
  _dragTempDirs.Clear();
  return n;
}

void QtFileManagerWindow::onDeleteTemp()
{
  deleteTempFiles(/*confirm*/ true);
}

// G.10c test hook : mint one drag temp dir (createDragTempDir is private; this
// exposes it so the offscreen --delete-temp hook has a temp dir to purge).
UString QtFileManagerWindow::mintTempDirForTest()
{
  return fs2us(createDragTempDir());
}

// === G.10d : Help -> Contents (F1) ===========================================
// The original opens the bundled CHM (ShowHelpWindow / HtmlHelp). CHM is
// Windows-only, so the Linux analog opens the 7-zip.org documentation URL via
// QDesktopServices::openUrl (the xdg-open path B.7a already uses).
void QtFileManagerWindow::onHelpContents()
{
  QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.7-zip.org/")));
}

// === operation helpers =======================================================

bool QtFileManagerWindow::collectSelectedFsPaths(UStringVector &out) const
{
  out.Clear();
  if (!_focused)
    return false;
  out = _focused->selectedFullPaths();
  return out.Size() != 0;
}

// === Add -> compress (a) =====================================================
void QtFileManagerWindow::doAdd()
{
  if (!_focused) return;
  if (_focused->isInArchive())
  {
    QMessageBox::information(this, QStringLiteral("7-Zip"),
        QStringLiteral("Add: the focused panel is inside an archive."));
    return;
  }
  UStringVector files;
  if (!collectSelectedFsPaths(files))
  {
    QMessageBox::information(this, QStringLiteral("7-Zip"),
        QStringLiteral("Add: select one or more files/folders first."));
    return;
  }

  // The default archive name uses the FOCUSED item's leaf when one item is
  // selected (firstSelectedName); the multi/empty fallback uses the folder leaf.
  // CompressFilesCore folds in the parent-folder fallback uniformly.
  compressFilesCore(files, _focused->currentFsDirPath(),
      files.Size() == 1 ? _focused->firstSelectedName() : UString());
  // Reflect any new archive in the panel.
  _focused->refresh();
}

// === Compress core : the shared "Add to Archive" launch =====================
// The single body behind BOTH the Add toolbar button (doAdd) and the compress-
// on-drop path (compressDroppedFiles). Mirrors CPanel::CompressDropFiles's
// createNewArchive==true branch (PanelDrag.cpp:2830-2875): pick a default archive
// name (CreateArchiveName) in `destDir`, then CompressFiles(..., showDialog=true)
// — here the `a -ad <arc> <files...>` command into RunFmCommand, which opens the
// SAME QtCompressGUI dialog seeded with the files. `nameHint` is the preferred
// base leaf (the single dropped/selected item's name); empty -> derive from the
// destination folder leaf. The archive is ALWAYS created in `destDir`.
void QtFileManagerWindow::compressFilesCore(const UStringVector &files,
    const UString &destDir, const UString &nameHint)
{
  if (files.Size() == 0)
    return;

  // Default archive name: the single item's name (nameHint) when given, else the
  // containing folder's leaf name (CreateArchiveName's parent-folder fallback),
  // + ".7z", placed in destDir.
  UString dir = destDir;
  NWindows::NFile::NName::NormalizeDirPathPrefix(dir);
  UString base = nameHint;
  if (base.IsEmpty())
  {
    UString d = dir;
    while (d.Len() > 1 && (d.Back() == L'/' || d.Back() == L'\\'))
      d.DeleteBack();
    const int slash = d.ReverseFind_PathSepar();
    base = (slash >= 0) ? UString(d.Ptr(slash + 1)) : d;
    if (base.IsEmpty())
      base = L"Archive";
  }
  const UString archiveName = dir + base + UString(L".7z");

  UStringVector cmd;
  cmd.Add(UString(L"a"));
  cmd.Add(UString(L"-ad"));        // show the compress dialog (-ad)
  cmd.Add(archiveName);
  for (unsigned i = 0; i < files.Size(); i++)
    cmd.Add(files[i]);

  RunFmCommand(cmd, _headless, this);
}

// === G.6b : compress-on-drop (external files -> Add to Archive) =============
// CPanel::CompressDropFiles, createNewArchive==true (PanelDrag.cpp:2817-2876),
// reached from CDropTarget::Drop when a NON-7-Zip source is dropped onto a 7-Zip
// FS folder (createNewArchive = !is7zip, PanelDrag.cpp:2561-2577 -> k_AddToArc).
// Port: the EXTERNAL drop branch of dropOnto() routes the dropped paths into the
// SAME compress flow the Add button uses (compressFilesCore -> QtCompressGUI), the
// new archive named from the first dropped item, created in the dest panel's FS
// dir. The on-disk sources are left in place (compress is always copy semantics).
bool QtFileManagerWindow::compressDroppedFiles(QtPanel *destPanel,
    const UStringVector &srcPaths)
{
  if (!destPanel || destPanel->isInArchive() || srcPaths.Size() == 0)
    return false;

  // The new archive is created in the dest panel's folder, NOT a temp dir
  // (CompressDropFiles avoids temp; the dest FS panel is the natural target here).
  const UString destDir = destPanel->dropTargetFsDirPath();
  if (destDir.IsEmpty())
    return false;

  // CreateArchiveName seeds the name from the first dropped item's leaf for a
  // single item (with the extension stripped), else from the parent folder. Mirror
  // that: take the leaf of srcPaths.Front() when one item, else let the core fall
  // back to the destDir folder leaf.
  UString nameHint;
  if (srcPaths.Size() == 1)
  {
    UString leaf = srcPaths.Front();
    const int slash = leaf.ReverseFind_PathSepar();
    if (slash >= 0)
      leaf.DeleteFrontal((unsigned)(slash + 1));
    // Strip a single trailing extension (CreateArchiveName: one '.' only).
    const int dotPos = leaf.Find(L'.');
    if (dotPos > 0 && leaf.Find(L'.', (unsigned)dotPos + 1) < 0)
      leaf.DeleteFrom((unsigned)dotPos);
    nameHint = leaf;
  }

  compressFilesCore(srcPaths, destDir, nameHint);
  destPanel->refresh();   // reflect the new archive in the dest panel
  return true;
}

// === G.6d : right-button-drag action menu ===================================
// CDropTarget::Drop -> Drag_OnContextMenu (PanelDrag.cpp:2533-2552, :2904-2979). A
// drag begun with the right (or middle) button shows a menu letting the user pick the
// action instead of inferring it from the keyboard modifiers. The offered set mirrors
// the original flagsMask (PanelDrag.cpp:2535-2543) built from the TARGET panel, and the
// item text mirrors NDragMenu::g_Pairs (PanelDrag.cpp:369-381): Copy / Move on an FS
// target, Copy-to-archive on an archive target, Add-to-archive on an FS target, plus a
// trailing Cancel. The Copy-to-archive label gets the IDS_CONTEXT_ARCHIVE suffix exactly
// as Drag_OnContextMenu appends it (PanelDrag.cpp:2956-2957).
QtFileManagerWindow::DragMenuCmd QtFileManagerWindow::showDragMenu(QtPanel *destPanel)
{
  // Scripted/headless override: a live QMenu::exec cannot run without a mouse, so the
  // hook forces the chosen command (proving the SAME dispatch the live menu drives).
  if (_dragMenuCmdOverride != DragMenuCmd::None)
    return _dragMenuCmdOverride;
  if (!destPanel)
    return DragMenuCmd::Cancel;

  QMenu menu(this);

  // PanelDrag.cpp:2535-2543 : the offered commands depend on the target panel.
  //   archive target           -> Copy-to-archive (k_Copy_ToArc)
  //   FS target                -> Add-to-archive (k_AddToArc) + (FS folder) Copy + Move
  const bool toArchive = destPanel->isInArchive();
  QAction *aCopy = nullptr, *aMove = nullptr, *aCopyToArc = nullptr, *aAddToArc = nullptr;

  if (toArchive)
  {
    // k_Copy_ToArc | k_MenuFlag_Copy, IDS_COPY_TO  (g_Pairs[2]); the label gets the
    // " <Archive>" suffix (Drag_OnContextMenu :2956-2957).
    QString name = FmLang(IDS_COPY_TO, QStringLiteral("Copy to:"));
    name += QLatin1Char(' ');
    name += FmLang(IDS_CONTEXT_ARCHIVE, QStringLiteral("Archive"));
    aCopyToArc = menu.addAction(name);
  }
  else
  {
    // FS target : Copy + Move (k_Copy_Base; only when IsFsFolderPath, PanelDrag.cpp:2541)
    // then Add-to-archive (k_AddToArc). g_Pairs order is Copy, Move, ..., Add, Cancel.
    aCopy = menu.addAction(FmLang(IDS_COPY, QStringLiteral("Copy here")));
    aMove = menu.addAction(FmLang(IDS_MOVE, QStringLiteral("Move here")));
    aAddToArc = menu.addAction(FmLang(IDS_CONTEXT_COMPRESS,
        QStringLiteral("Add to archive...")));
  }

  // g_Pairs always carries a trailing Cancel after a separator (PanelDrag.cpp:380/2959).
  // IDS_CANCEL is (IDCANCEL + 400) == 402, defined locally in PanelDrag.cpp:366-367.
  menu.addSeparator();
  QAction *aCancel = menu.addAction(FmLang(/*IDS_CANCEL*/ 402, QStringLiteral("Cancel")));

  QAction *chosen = menu.exec(QCursor::pos());
  if (!chosen || chosen == aCancel)
    return DragMenuCmd::Cancel;            // menuResult <= 0 -> k_Cancel (:2976-2977)
  if (chosen == aCopy)       return DragMenuCmd::Copy;
  if (chosen == aMove)       return DragMenuCmd::Move;
  if (chosen == aCopyToArc)  return DragMenuCmd::CopyToArc;
  if (chosen == aAddToArc)   return DragMenuCmd::AddToArc;
  return DragMenuCmd::Cancel;
}

// G.6d : run the menu-chosen command through the SAME paths the modifier-driven drop
// uses, so a right-drag Copy/Move/Add is byte-for-byte the inferred-action drop.
bool QtFileManagerWindow::performDragMenuCmd(QtPanel *destPanel,
    const UStringVector &srcPaths, DragMenuCmd cmd, const UString &destDir)
{
  switch (cmd)
  {
    case DragMenuCmd::Cancel:
    case DragMenuCmd::None:
      return false;   // k_Cancel -> DROPEFFECT_NONE, no operation (PanelDrag.cpp:2639)

    case DragMenuCmd::Copy:
    case DragMenuCmd::Move:
    {
      // FS Copy/Move here : the same internal-vs-external FS copy/move the
      // modifier-driven dropOnto runs, with the action FORCED by the menu choice.
      const bool moveMode = (cmd == DragMenuCmd::Move);
      QtPanel *src = sourcePanelForPaths(srcPaths);
      if (src)
      {
        if (src == destPanel)
          return true;   // self-drop: handled, no-op
        CMyComPtr<IFolderOperations> ops;
        if (!GetFolderOperations(src, ops))
          return false;
        const QVector<int> rows = src->selectedSourceRows();
        if (rows.isEmpty())
          return false;
        UString dest = destDir;
        NWindows::NFile::NName::NormalizeDirPathPrefix(dest);
        QtFsCopyWorker worker;
        worker.FolderOperations = ops;
        worker.DestPath = dest;
        worker.MoveMode = moveMode;
        worker.OverwritePrompt = _overwritePrompt;
        worker.DisableUserQuestions = _headless;
        for (int rrow : rows)
          worker.Indices.Add((UInt32)src->model()->modelRowToRealIndex(rrow));
        const UString title = moveMode ? UString(L"Moving") : UString(L"Copying");
        const HRESULT res = worker.Create(title, this);
        destPanel->refresh();
        if (moveMode)
          src->refresh();
        if (reportDropError(res))
          return false;
        src->killSelection();   // KillSelection on success (PanelDrag.cpp:1800-1801)
        return true;
      }
      // External source (paths from another app / not a panel selection): the
      // temporary-CFSFolder copy/move path.
      const QtFsDnd::DropResult res = QtFsDnd::CopyPathsInto(
          srcPaths, destDir, moveMode, _overwritePrompt, this, _headless);
      destPanel->refresh();
      if (moveMode)
        refreshBothPanels();
      if (!res.Ok)
        reportDropError(res.LastError);
      return res.NumItems != 0;
    }

    case DragMenuCmd::AddToArc:
      // Add to archive : the compress-on-drop flow (CompressDropFiles), exactly the
      // external-drop branch of dropOnto.
      return compressDroppedFiles(destPanel, srcPaths);

    case DragMenuCmd::CopyToArc:
      // Copy to (the open) archive : add-into-archive. The original ALSO confirms via
      // the IDS_CONFIRM_FILE_COPY MessageBox even on the right-drag menu path
      // (PanelDrag.cpp:2610-2626 runs for k_Copy_ToArc regardless of menu_WasShown),
      // so reuse the same confirm the modifier-driven archive drop uses.
      if (!destPanel->isUpdatableArchive())
        return false;
      {
        const QString title = FmLang(IDS_CONFIRM_FILE_COPY,
            QStringLiteral("Confirm File Copy"));
        QString body = FmLang(IDS_COPY_TO, QStringLiteral("Copy to:"));
        body += QLatin1Char('\n');
        body += UStr_toQ(destPanel->currentFolderPrefix());
        body += QLatin1Char('\n');
        body += FmLang(IDS_WANT_TO_COPY_FILES,
            QStringLiteral("Are you sure you want to copy files to archive"));
        body += QStringLiteral(" ?");
        if (_headless)
        {
          printf("DROP_ARC_CONFIRM_TITLE: %s\n", title.toUtf8().constData());
          printf("DROP_ARC_CONFIRM_MSG: %s\n", body.toUtf8().constData());
          fflush(stdout);
        }
        else if (QMessageBox::question(this, title, body,
                     QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                     QMessageBox::No) != QMessageBox::Yes)
          return false;
      }
      return addPathsToArchive(destPanel, srcPaths, /*moveMode*/ false);
  }
  return false;
}

// G.6d : the right-button drop path of dropOnto(). Pop the action menu (or use the
// scripted override), then perform the chosen command. `forcedCmd != None` skips the
// live menu (the headless hook).
bool QtFileManagerWindow::dropRightButton(QtPanel *destPanel,
    const UStringVector &srcPaths, const UString &destDir, DragMenuCmd forcedCmd)
{
  const DragMenuCmd cmd = (forcedCmd != DragMenuCmd::None)
      ? forcedCmd
      : showDragMenu(destPanel);
  return performDragMenuCmd(destPanel, srcPaths, cmd, destDir);
}

// === Extract -> QtExtractGUI (x) =============================================
void QtFileManagerWindow::doExtract()
{
  if (!_focused) return;

  // Source archive(s): if the panel is INSIDE an archive, extract the whole
  // open archive; otherwise extract the selected archive file(s).
  UStringVector archives;
  if (_focused->isInArchive())
  {
    // currentPath() of an archive folder is the in-archive path; the archive
    // file path is what we entered from. The seamless transition records it in
    // the panel; B.3 supports "extract a selected archive file" robustly, so
    // here we ask the user when inside an archive.
    QMessageBox::information(this, QStringLiteral("7-Zip"),
        QStringLiteral("Extract: select the archive file in the filesystem view, "
                       "or use Up to exit the archive and select it."));
    return;
  }

  // Selected files that are archives (any selected file; the engine rejects
  // non-archives). Require a selection.
  if (!collectSelectedFsPaths(archives))
  {
    QMessageBox::information(this, QStringLiteral("7-Zip"),
        QStringLiteral("Extract: select an archive file first."));
    return;
  }

  // Destination = the OTHER panel's FS directory (App.h GetAnotherPanel), if it
  // is a filesystem folder; else fall back to the focused folder.
  UString destDir = otherPanel()->currentFsDirPath();
  if (destDir.IsEmpty())
    destDir = _focused->currentFsDirPath();

  for (unsigned i = 0; i < archives.Size(); i++)
  {
    UStringVector cmd;
    cmd.Add(UString(L"x"));
    UString o(L"-o");
    o += destDir;
    cmd.Add(o);
    cmd.Add(archives[i]);
    RunFmCommand(cmd, _headless, this);
  }
  otherPanel()->refresh();
}

// === Test -> extract in test mode (t) ========================================
void QtFileManagerWindow::doTest()
{
  if (!_focused) return;
  UStringVector archives;
  if (!collectSelectedFsPaths(archives))
  {
    QMessageBox::information(this, QStringLiteral("7-Zip"),
        QStringLiteral("Test: select an archive file first."));
    return;
  }
  for (unsigned i = 0; i < archives.Size(); i++)
  {
    UStringVector cmd;
    cmd.Add(UString(L"t"));
    cmd.Add(archives[i]);
    RunFmCommand(cmd, _headless, this);
  }
}

// === CRC submenu -> QtHashGUI (h) ============================================
// Mirrors CApp::CalculateCrc2 (PanelCrc.cpp): when the source panel is NOT a
// filesystem folder (it is inside an archive), hash the archived STREAMS via the
// folder's CopyTo with streamMode + hashMethods (no extraction to disk) and show
// the same results window; otherwise (a FS panel) take the existing CLI 'h' path.
void QtFileManagerWindow::doHash(const char *methodName)
{
  if (!_focused) return;

  // --- in-archive branch (PanelCrc.cpp's !Is_IO_FS_Folder path) --------------
  if (_focused->isInArchive())
  {
    doHashInArchive(methodName);
    return;
  }

  // --- filesystem branch (unchanged) -----------------------------------------
  UStringVector files;
  if (!collectSelectedFsPaths(files))
  {
    QMessageBox::information(this, QStringLiteral("7-Zip"),
        QStringLiteral("Checksum: select one or more files first."));
    return;
  }
  UStringVector cmd;
  cmd.Add(UString(L"h"));
  if (methodName && methodName[0] && !(methodName[0] == '*' && methodName[1] == 0))
  {
    UString sw(L"-scrc");
    sw += GetUnicodeString(methodName);
    cmd.Add(sw);
  }
  else if (methodName && methodName[0] == '*')
  {
    cmd.Add(UString(L"-scrc*"));
  }
  for (unsigned i = 0; i < files.Size(); i++)
    cmd.Add(files[i]);
  RunFmCommand(cmd, _headless, this);
}

// === in-archive checksum (G.2d) ============================================
// Faithful Qt analogue of CApp::CalculateCrc2's archive (!Is_IO_FS_Folder) branch:
//   CCopyToOptions options;
//   options.streamMode = true; options.showErrorMessages = true;
//   options.hashMethods.Add(methodName); options.NeedRegistryZone = false;
//   srcPanel.CopyTo(options, indices, &messages);
// CPanel::CopyTo then builds the extract callback in stream-mode with a CHashBundle
// (SetMethods + SetHashMethods) and runs CAgentFolder::CopyTo. We run that on the
// QtArchiveHashWorker (the same threaded modal-progress core the archive extract /
// add / delete use), then surface the digests through Qt_AddHashBundleRes ->
// QtHashResultsDialog (the same results window the FS hash path shows). Encrypted
// archives prompt via the existing _passwordPrompt (as extract does).
void QtFileManagerWindow::doHashInArchive(const char *methodName)
{
  // operated indices = the selected archive rows (already source-mapped, exactly
  // like the drag-OUT path). Empty selection -> nothing (mirror CalculateCrc2's
  // "if (indices.IsEmpty()) return S_OK").
  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.isEmpty())
  {
    if (!_headless)
      QMessageBox::information(this, QStringLiteral("7-Zip"),
          QStringLiteral("Checksum: select one or more files first."));
    return;
  }

  CMyComPtr<IFolderOperations> ops;
  if (!GetFolderOperations(_focused, ops))
    return;

  QtArchiveHashWorker worker;
  worker.FolderOperations = ops;
  worker.PasswordPrompt = _passwordPrompt;   // encrypted archives (same as extract)
  worker.DisableUserQuestions = _headless;
  // G.4f : translate each selected MODEL row to the engine realIndex (identity when
  // ShowDots is off; ".." is already excluded from `rows`).
  for (int r : rows)
    worker.Indices.Add((UInt32)_focused->model()->modelRowToRealIndex(r));

  // hashMethods.Add(methodName) — "*" means all, else the literal method name
  // (CRC32 / CRC64 / SHA256 / ...). Same value the FS CRC submenu drives.
  worker.HashMethods.Add(methodName && methodName[0]
      ? GetUnicodeString(methodName) : UString(L"*"));

  // FirstFileName/MainName when exactly one item is selected (PanelCopy.cpp:242
  // == GetItemRelPath(indices[0])). The model's itemName is the leaf relpath.
  if (rows.size() == 1)
    worker.FirstFileName = _focused->model()->itemName(rows.first());

  // CPanel::CopyTo configures the bundle's methods (PanelCopy.cpp:266):
  //   extracter.Hash.SetMethods(EXTERNAL_CODECS_VARS_G options.hashMethods);
  // (Z7_EXTERNAL_CODECS is off in this build, so the macro is empty; the built-in
  // hashers — CRC32/CRC64/SHA*/BLAKE2sp/MD5/XXH64 — are all reachable, same as the
  // FS hash path's HashCalc.) An unknown method -> E_NOTIMPL, reported below.
  const HRESULT setRes = worker.Hash.SetMethods(EXTERNAL_CODECS_VARS_G worker.HashMethods);
  if (setRes != S_OK)
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Checksum: unsupported method."));
    else
      fprintf(stderr, "arc-hash: SetMethods failed (0x%08X)\n", (unsigned)setRes);
    return;
  }

  const HRESULT res = worker.Create(UString(L"Calculating checksum"), this);
  if (res != S_OK)
  {
    // E_ABORT == cancelled / wrong password -> silent, like the extract path.
    if (res != E_ABORT && !_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Checksum failed."));
    return;
  }

  // Build the result rows exactly like the FS path (Qt_AddHashBundleRes) and show
  // them in the same QtHashResultsDialog. Headless: print the digest line(s) so a
  // test can diff against `7zz h -scrc<method> <extracted-file>`.
  CPropNameValPairs pairs;
  Qt_AddHashBundleRes(pairs, worker.Hash);

  if (_headless)
  {
    FOR_VECTOR (i, pairs)
    {
      const AString name = UnicodeStringToMultiByte(pairs[i].Name, CP_UTF8);
      const AString val  = UnicodeStringToMultiByte(pairs[i].Value, CP_UTF8);
      printf("HASH: %s = %s\n", name.Ptr(), val.Ptr());
    }
    return;
  }

  if (!pairs.IsEmpty())
  {
    QtHashResultsDialog dlg(pairs, this);
    dlg.exec();
  }
}

// ============================================================================
// === B.4a : filesystem operations (Copy/Move/Delete/Rename/Create) ==========
// Mirrors CPanel's operation control flow (PanelCopy.cpp / PanelOperations.cpp):
// run the IFolderOperations method on the focused panel's folder, through a
// QtProgressThreadVirt worker (copy/move/delete) or a synchronous one-shot call
// (rename/create), then refresh the affected panel(s).
// ============================================================================

static UString Q_toU2(const QString &s)
{
  const std::wstring w = s.toStdWString();
  return UString(w.c_str());
}

// CPanel guards every modify op on _folderOperations being available AND the
// folder being a real FS folder; archive in-place modify is deferred. Here we
// refuse when the focused panel is inside an archive.
bool QtFileManagerWindow::checkFsOpAllowed(const char *opName)
{
  if (!_focused)
    return false;
  if (_focused->isInArchive())
  {
    // B.5b : in-place modify is allowed on an UPDATABLE archive (CAgent::CanUpdate
    // && !IsThere_ReadOnlyArc, captured at open time). Read-only formats stay
    // refused (the engine would otherwise fail late with E_NOTIMPL).
    if (!_focused->isUpdatableArchive())
    {
      if (!_headless)
        QMessageBox::information(this, QStringLiteral("7-Zip"),
            QString::fromLatin1(opName) +
            QStringLiteral(": this archive is read-only."));
      return false;
    }
    return true;
  }

  // G.5d : the FS-folder read-only target check, mirroring CPanel::CheckBeforeUpdate /
  // IsThereReadOnlyFolder (PanelMenu.cpp:866-917). Query kpidReadOnly on the target
  // folder via GetFolderProperty (IsReadOnlyFolder, PanelMenu.cpp:851-863): if it
  // reports read-only, refuse up front with a folder-named message (the original's
  // IDS_OPERATION_IS_NOT_SUPPORTED + folder path + IDS_PROP_READ_ONLY) instead of
  // failing late with a generic engine error. (The port keeps no FS _parentFolders
  // chain, so only the bound current folder is queried — the same per-folder predicate
  // the original loops over.)
  IFolderFolder *folder = _focused->currentFolder();
  if (folder)
  {
    bool readOnly = false;
    NCOM::CPropVariant prop;
    if (folder->GetFolderProperty(kpidReadOnly, &prop) == S_OK
        && prop.vt == VT_BOOL)
      readOnly = VARIANT_BOOLToBool(prop.boolVal);
    if (readOnly)
    {
      // CheckBeforeUpdate's message body (PanelMenu.cpp:901-912): "<op error label>\n
      // Operation is not supported.\n<folder path>\nRead-only". The op-error label per
      // call site is folded into the leading line; we use IDS_OPERATION_IS_NOT_SUPPORTED
      // + the folder path + IDS_PROP_READ_ONLY (the faithful read-only refusal).
      UString s = Q_toU_local(
          FmLang(IDS_OPERATION_IS_NOT_SUPPORTED, QStringLiteral("Operation is not supported.")));
      s.Add_LF();
      s += _focused->currentFsDirPath();
      s.Add_LF();
      s += Q_toU_local(FmLang(IDS_PROP_READ_ONLY, QStringLiteral("Read-only")));
      if (_headless)
      {
        printf("FS_READONLY_REFUSED: op=%s %s\n", opName,
            (const char *)GetOemString(s));
        fflush(stdout);
      }
      else
        QMessageBox::warning(this, QStringLiteral("7-Zip"), UStr_toQ(s));
      return false;
    }
  }
  return true;
}

void QtFileManagerWindow::refreshBothPanels()
{
  if (_left) _left->refresh();
  if (_right) _right->refresh();
}

// Map a panel's IFolderFolder to its IFolderOperations (CFSFolder implements both).
static bool GetFolderOperations(QtPanel *panel, CMyComPtr<IFolderOperations> &ops)
{
  if (!panel) return false;
  IFolderFolder *folder = panel->currentFolder();
  if (!folder) return false;
  ops.Release();
  folder->QueryInterface(IID_IFolderOperations, (void **)&ops);
  return ops != nullptr;
}

// === B.5b : add FS items into an updatable archive panel (CopyFrom) ==========
// Mirror CPanel::CopyFromNoAsk (PanelCopy.cpp:452): empty folder-prefix + full
// FS paths form. The in-archive destination is the dest panel's current folder
// (implicit in the CAgentFolder). Runs the QtArchiveAddWorker on the progress
// thread, then re-binds the dest panel to the reopened archive (refresh()).
bool QtFileManagerWindow::addPathsToArchive(QtPanel *destPanel,
    const UStringVector &srcPaths, bool moveMode)
{
  if (!destPanel || !destPanel->isUpdatableArchive() || srcPaths.Size() == 0)
    return false;

  CMyComPtr<IFolderOperations> ops;
  if (!GetFolderOperations(destPanel, ops))
    return false;

  QtArchiveAddWorker worker;
  worker.FolderOperations = ops;
  worker.FromFolderPrefix = UString();   // empty prefix; full paths in ItemNames
  worker.ItemNames = srcPaths;
  worker.MoveMode = moveMode;
  worker.DisableUserQuestions = _headless;
  // Encrypted-FM : prompt for the password when adding INTO an encrypted archive
  // (symmetric with the OPEN and extract paths). QtArchiveUpdateCallback's
  // CryptoGetTextPassword null-checks this and fails safe to E_ABORT when unset.
  worker.PasswordPrompt = _passwordPrompt;

  // B.5b crash fix : freeze the dest archive panel's view (0 rows, no GetProperty)
  // before the worker's CommonUpdateOperation Close()+ReOpen() rebinds the
  // CAgentFolder; the refresh() below lifts it. Without this a GUI-thread relayout
  // races the worker-thread rebind on the same agent -> SIGSEGV.
  destPanel->model()->setSuspended(true);

  worker.Create(UString(L"Adding"), this);

  // Engine did Close()+ReOpen() in place; re-bind the dest panel to the reopened
  // archive root so the new entries show.
  destPanel->refresh();
  return true;
}

// === Copy To (F5, moveMode=0) / Move To (F6, moveMode=1) ====================
// Mirrors CPanel::CopyTo: destination = the OTHER panel's FS directory; build the
// selected-index list; run CopyTo(moveMode, indices, dest) in a progress thread.
void QtFileManagerWindow::doCopyMove(bool moveMode, const UString &destOverride)
{
  if (!checkFsOpAllowed(moveMode ? "Move" : "Copy"))
    return;

  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.isEmpty())
  {
    if (!_headless)
      QMessageBox::information(this, QStringLiteral("7-Zip"),
          QStringLiteral("Select one or more items first."));
    return;
  }

  // B.5b : Copy/Move To when the OTHER panel is an UPDATABLE archive -> add the
  // focused panel's selected FS files INTO that archive (CopyFrom). The focused
  // panel must itself be a FS folder (archive->archive copy is out of scope).
  // A scripted destOverride forces the FS-dest path instead.
  if (destOverride.IsEmpty() && otherPanel()->isUpdatableArchive())
  {
    if (_focused->isInArchive())
    {
      if (!_headless)
        QMessageBox::information(this, QStringLiteral("7-Zip"),
            QStringLiteral("Copy/Move between two archives is not supported."));
      return;
    }
    const UStringVector srcPaths = _focused->selectedFullPaths();
    if (srcPaths.Size() == 0)
      return;
    if (!addPathsToArchive(otherPanel(), srcPaths, moveMode))
    {
      if (!_headless)
        QMessageBox::warning(this, QStringLiteral("7-Zip"),
            QStringLiteral("Add to archive failed."));
      return;
    }
    if (moveMode)
      _focused->refresh();   // sources removed from disk on move
    return;
  }

  // Destination = the OTHER panel's FS dir (App.h GetAnotherPanel), unless inside
  // an archive (then fall back to the focused dir). A scripted override wins.
  UString dest = destOverride;
  if (dest.IsEmpty())
  {
    dest = otherPanel()->currentFsDirPath();
    if (dest.IsEmpty())
      dest = _focused->currentFsDirPath();
  }
  if (dest.IsEmpty())
    return;

  // Optional destination-confirm dialog (mirrors CopyTo's CCopyDialog), simplified
  // to a single editable path field. Skipped in headless / scripted runs.
  if (!_headless && destOverride.IsEmpty())
  {
    bool ok = false;
    const QString title = moveMode ? QStringLiteral("Move To") : QStringLiteral("Copy To");
    const QString text = QInputDialog::getText(this, title,
        QStringLiteral("Destination folder:"), QLineEdit::Normal,
        QString::fromWCharArray(dest.Ptr(), (int)dest.Len()), &ok);
    if (!ok)
      return;
    dest = Q_toU2(text);
    if (dest.IsEmpty())
      return;
  }
  // CopyTo expects a directory destination ending with a path separator (so each
  // item is placed inside it; without a trailing sep, isDirectPath => single item
  // rename-on-copy).
  NWindows::NFile::NName::NormalizeDirPathPrefix(dest);

  CMyComPtr<IFolderOperations> ops;
  if (!GetFolderOperations(_focused, ops))
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("This folder does not support copy/move."));
    return;
  }

  QtFsCopyWorker worker;
  worker.FolderOperations = ops;
  worker.DestPath = dest;
  worker.MoveMode = moveMode;
  worker.OverwritePrompt = _overwritePrompt;
  worker.DisableUserQuestions = _headless;
  // G.4f : MODEL row -> engine realIndex (identity when ShowDots off; ".." excluded).
  for (int r : rows)
    worker.Indices.Add((UInt32)_focused->model()->modelRowToRealIndex(r));

  const UString title = moveMode ? UString(L"Moving") : UString(L"Copying");
  worker.Create(title, this);

  // Refresh: destination always; source too on move (items removed).
  otherPanel()->refresh();
  _focused->refresh();
}

void QtFileManagerWindow::onCopyTo() { doCopyMove(false); }
void QtFileManagerWindow::onMoveTo() { doCopyMove(true); }

// ============================================================================
// === B.4b : drag & drop dispatch ============================================
// dropOnto() is the GUI-thread drop handler installed on each panel. It mirrors
// PanelDrag's target logic with Qt-native transport:
//   * read the dropped text/uri-list (file:// -> absolute FS paths),
//   * decide move-vs-copy (QtFsDnd::MapDropAction == PanelDrag GetEffect),
//   * dispatch INTERNAL (a same-process panel is the source: reuse its
//     folder+indices, exactly Copy To / Move To) vs EXTERNAL (bind a temporary
//     CFSFolder per source parent and CopyTo) — both via the B.4a QtFsCopyWorker.
// ============================================================================

// If `paths` exactly matches a panel's current selection (the panel produced the
// drag), return that panel; else nullptr. (Same-process internal-drop detection.)
static bool PathListsEqual(const UStringVector &a, const UStringVector &b)
{
  if (a.Size() != b.Size() || a.Size() == 0)
    return false;
  // Membership test (order can differ between the mime and the selection set).
  FOR_VECTOR (i, a)
  {
    bool found = false;
    FOR_VECTOR (j, b)
      if (a[i] == b[j]) { found = true; break; }
    if (!found)
      return false;
  }
  return true;
}

QtPanel *QtFileManagerWindow::sourcePanelForPaths(const UStringVector &paths) const
{
  for (QtPanel *p : { _left, _right })
  {
    if (!p || p->isInArchive())
      continue;
    if (PathListsEqual(paths, p->selectedFullPaths()))
      return p;
  }
  return nullptr;
}

bool QtFileManagerWindow::dropOnto(QtPanel *destPanel, const QMimeData *mime,
    Qt::DropAction action)
{
  if (!destPanel)
    return false;

  // The dropped filesystem paths (file:// URIs only).
  UStringVector srcPaths;
  if (QtFsDnd::UriListToPaths(mime, srcPaths) == 0)
    return false;

  // G.6d : a RIGHT-button drag (marker stamped by the source panel) pops the
  // Copy / Move / Add-to-archive / Cancel action menu instead of inferring the action
  // from the Qt DropAction — the mirror of CDropTarget::Drop's m_IsRightButton branch
  // (PanelDrag.cpp:2533). The destination directory honors the sub-folder under the
  // drop cursor exactly as the modifier-driven path does (an FS target); an archive
  // target needs no FS dir.
  if (QtFsDnd::IsRightButtonDrag(mime))
  {
    const UString rdestDir = destPanel->isInArchive()
        ? UString()
        : destPanel->dropTargetFsDirPath();
    // An FS Copy/Move needs a real dest dir; an archive target (Copy-to-archive) does
    // not. Bail only when an FS target produced no dir (mirrors the empty-destDir
    // guard below).
    if (!destPanel->isInArchive() && rdestDir.IsEmpty())
      return false;
    return dropRightButton(destPanel, srcPaths, rdestDir, _dragMenuCmdOverride);
  }

  // B.5b : drop ONTO an archive panel -> add-into-archive (CopyFrom). Only for
  // an updatable archive (read-only rejects); the dropped paths are full FS paths
  // (CopyFromNoAsk's empty-prefix form). Drop-into-archive is copy semantics
  // (the on-disk sources are left in place).
  if (destPanel->isInArchive())
  {
    if (!destPanel->isUpdatableArchive())
      return false;

    // G.6a : a drop onto an updatable archive (k_Copy_ToArc) ALWAYS confirms before
    // touching the archive — the faithful mirror of CDropTarget::Drop's k_Copy_ToArc
    // MessageBox (PanelDrag.cpp:2610-2626): title IDS_CONFIRM_FILE_COPY, body
    // IDS_COPY_TO + LF + the archive path + LF + IDS_WANT_TO_COPY_FILES + " ?",
    // MB_YESNOCANCEL; non-Yes cancels. Drop-into-archive is always COPY here (the
    // sources stay on disk), so the body uses IDS_COPY_TO (not IDS_MOVE_TO). Headless
    // auto-proceeds (DisableUserQuestions) but prints the chosen wording as a marker.
    {
      const QString title = FmLang(IDS_CONFIRM_FILE_COPY,
          QStringLiteral("Confirm File Copy"));
      QString body = FmLang(IDS_COPY_TO, QStringLiteral("Copy to:"));
      body += QLatin1Char('\n');
      body += UStr_toQ(destPanel->currentFolderPrefix());   // m_Panel->_currentFolderPrefix
      body += QLatin1Char('\n');
      body += FmLang(IDS_WANT_TO_COPY_FILES,
          QStringLiteral("Are you sure you want to copy files to archive"));
      body += QStringLiteral(" ?");
      if (_headless)
      {
        printf("DROP_ARC_CONFIRM_TITLE: %s\n", title.toUtf8().constData());
        printf("DROP_ARC_CONFIRM_MSG: %s\n", body.toUtf8().constData());
        fflush(stdout);
      }
      else if (QMessageBox::question(this, title, body,
                   QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                   QMessageBox::No) != QMessageBox::Yes)
        return false;   // No / Cancel -> NDragMenu::k_Cancel (no archive change)
    }

    return addPathsToArchive(destPanel, srcPaths, /*moveMode*/ false);
  }

  // G.6c : the FS destination honors the sub-folder under the drop cursor (set by the
  // view's drag-over/drop hit-test), mirroring CDropTarget::GetTargetPath appending
  // m_DropHighlighted_SubFolderName (PanelDrag.cpp:2186-2198). With no sub-folder hit
  // this is just the panel's current FS dir, exactly as before.
  const UString destDir = destPanel->dropTargetFsDirPath();
  if (destDir.IsEmpty())
    return false;

  // Move-vs-copy: PanelDrag GetEffect semantics via the Qt action + same-volume.
  const bool moveMode = QtFsDnd::MapDropAction(action, srcPaths, destDir);

  // INTERNAL drop : the source is one of our own panels. Reuse its folder +
  // selected indices directly (this is exactly Copy To / Move To, B.4a).
  QtPanel *src = sourcePanelForPaths(srcPaths);
  if (src)
  {
    // No-op self-drop (dropping a panel's selection back onto its own folder):
    // treat as handled but do nothing.
    if (src == destPanel)
      return true;

    CMyComPtr<IFolderOperations> ops;
    if (!GetFolderOperations(src, ops))
      return false;
    const QVector<int> rows = src->selectedSourceRows();
    if (rows.isEmpty())
      return false;

    UString dest = destDir;
    NWindows::NFile::NName::NormalizeDirPathPrefix(dest);

    QtFsCopyWorker worker;
    worker.FolderOperations = ops;
    worker.DestPath = dest;
    worker.MoveMode = moveMode;
    worker.OverwritePrompt = _overwritePrompt;
    worker.DisableUserQuestions = _headless;
    // G.4f : MODEL row -> engine realIndex on the SOURCE panel (identity when
    // ShowDots off; ".." already excluded from `rows`).
    for (int rrow : rows)
      worker.Indices.Add((UInt32)src->model()->modelRowToRealIndex(rrow));

    const UString title = moveMode ? UString(L"Moving") : UString(L"Copying");
    // G.6e : Create() returns the worker HRESULT. A failed drop is no longer silent —
    // reportDropError surfaces any non-S_OK/non-E_ABORT result (PanelDrag.cpp:1793).
    const HRESULT res = worker.Create(title, this);

    destPanel->refresh();
    if (moveMode)
      src->refresh();   // items removed from the source

    if (reportDropError(res))
      return false;     // failed: leave the source selection intact (no KillSelection)
    // G.6e : a completed drag with no errors clears the SOURCE selection, the Qt
    // mirror of PanelDrag.cpp:1800-1801 KillSelection (res == S_OK, not cancelled).
    src->killSelection();
    return true;
  }

  // G.6b : EXTERNAL drop onto an FS panel -> "Add to Archive" (compress-on-drop),
  // the faithful analog of CDropTarget::Drop's createNewArchive = !is7zip branch
  // (PanelDrag.cpp:2561-2577 -> k_AddToArc -> CompressDropFiles(createNewArchive=
  // true), PanelDrag.cpp:2817-2876). The trigger is precisely "the source is NOT one
  // of our own panels" (src == nullptr above), the Qt mirror of is7zip ==
  // m_TargetPath_WasSent_ToDataObject: an in-process panel drag stays a plain
  // copy/move (handled by the INTERNAL branch); anything else (a uri-list from
  // another application) offers the compress flow. The dropped paths become the
  // items to compress; the new archive is named from the first dropped item and
  // created in this FS panel's folder. Headless auto-drives RunFmCommand to a
  // default-named archive (DisableUserQuestions) instead of showing the dialog.
  if (compressDroppedFiles(destPanel, srcPaths))
    return true;

  // (Fallback) plain FS copy/move into the dest dir — kept for the case the
  // compress offer could not run (e.g. an empty dest dir). Bind temporary
  // CFSFolders (grouped by parent) and CopyTo via the same B.4a worker.
  const QtFsDnd::DropResult res = QtFsDnd::CopyPathsInto(
      srcPaths, destDir, moveMode, _overwritePrompt, this, _headless);
  destPanel->refresh();
  // On an external move the originals live elsewhere; refresh both panels in case
  // either shows a source parent.
  if (moveMode)
    refreshBothPanels();
  // G.6e : surface a worker error from the external path too — using the real first
  // failing HRESULT (so an E_ABORT/user-cancel stays silent, like PanelDrag). An
  // external drop has no in-process source panel, so there is no KillSelection here.
  if (!res.Ok)
    reportDropError(res.LastError);
  return res.NumItems != 0;
}

// G.6e : surface a worker HRESULT from a drop — the Qt mirror of PanelDrag's
// MessageBox_Error_HRESULT after a drag (PanelDrag.cpp:1779-1782/1793-1798). S_OK is
// success; E_ABORT (DRAGDROP_S_CANCEL — user cancel) is silent; anything else shows
// the decoded HResultToMessage text. Returns true if `res` was a reportable error.
bool QtFileManagerWindow::reportDropError(HRESULT res)
{
  if (res == S_OK || res == E_ABORT)
    return false;
  const UString message = HResultToMessage(res);
  if (_headless)
  {
    printf("DROP_ERROR: res=0x%08X %s\n", (unsigned)res,
        UStr_toQ(message).toUtf8().constData());
    fflush(stdout);
  }
  else
    QMessageBox::critical(this, QStringLiteral("7-Zip"), UStr_toQ(message));
  return true;
}

bool QtFileManagerWindow::scriptedDrop(QtPanel *destPanel,
    const UStringVector &srcPaths, Qt::DropAction action, int targetSubFolderRow)
{
  // Build the same QMimeData the drag source would, then run the real handler.
  QMimeData *mime = QtFsDnd::MakeUriListMime(srcPaths);
  if (!mime)
    return false;
  // G.6c : drive the drop-target sub-folder hit-test without a live mouse — exactly
  // what the view's dragMove/drop hit-test sets when the cursor is over an FS folder.
  if (destPanel && targetSubFolderRow >= 0)
    destPanel->setDropTargetRowForTest(targetSubFolderRow);
  const bool ok = dropOnto(destPanel, mime, action);
  if (destPanel)
    destPanel->clearDropTarget();
  delete mime;
  return ok;
}

// G.6d : scripted RIGHT-button-drag drop (offscreen). Build the uri-list mime, stamp
// the right-drag marker (so dropOnto takes the menu path), FORCE the menu result to
// `cmd` (a live QMenu::exec cannot run headlessly — _dragMenuCmdOverride substitutes
// for the user's pick), and dispatch. Proves the menu-action dispatch (Copy vs Move vs
// Add) is reachable and correct. Returns true if the chosen command ran.
bool QtFileManagerWindow::scriptedRightDrop(QtPanel *destPanel,
    const UStringVector &srcPaths, DragMenuCmd cmd)
{
  QMimeData *mime = QtFsDnd::MakeUriListMime(srcPaths);
  if (!mime)
    return false;
  QtFsDnd::MarkRightButtonDrag(mime);
  // The override is read by showDragMenu()/dropRightButton() instead of exec()ing a
  // menu; restore it after the drop so a later real drag still shows the live menu.
  _dragMenuCmdOverride = cmd;
  const bool ok = dropOnto(destPanel, mime, Qt::CopyAction);
  _dragMenuCmdOverride = DragMenuCmd::None;
  if (destPanel)
    destPanel->clearDropTarget();
  delete mime;
  return ok;
}

// === Delete (Del) ===========================================================
// Mirrors CPanel::DeleteItems: confirm, then _folder->Delete(indices) in a
// progress thread, then refresh.
void QtFileManagerWindow::doDelete(bool permanentOverride)
{
  if (!checkFsOpAllowed("Delete"))
    return;

  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.isEmpty())
  {
    if (!_headless)
      QMessageBox::information(this, QStringLiteral("7-Zip"),
          QStringLiteral("Select one or more items to delete."));
    return;
  }

  // B.8 : recycle-vs-permanent decision (CPanel::DeleteItems(toRecycleBin)).
  //   toRecycleBin == !Shift && DeleteToTrash && the panel is a FILESYSTEM panel.
  // Archive in-place delete (B.5b) is ALWAYS the engine path (the entry lives
  // inside the archive — the XDG trash applies only to real files), so trash is
  // gated on !isInArchive(). Shift held (or --delete-perm) forces permanent.
  const bool inArchive = _focused->isInArchive();
  const bool shiftHeld =
      (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
  QtFmSettings::CInfo s;
  s.Load();
  const bool useTrash = s.DeleteToTrash && !inArchive
      && !shiftHeld && !permanentOverride;

  // Confirmation (CPanel::DeleteItems, PanelOperations.cpp:224-249). G.5e : pick the
  // title/message variant by COUNT and (for a single item) the file/folder distinction
  // — IsItem_Folder(index) -> the model's isFolder(row). A SINGLE folder uses
  // IDS_CONFIRM_FOLDER_DELETE / IDS_WANT_TO_DELETE_FOLDER; a SINGLE file uses
  // IDS_CONFIRM_FILE_DELETE / IDS_WANT_TO_DELETE_FILE; MULTIPLE uses
  // IDS_CONFIRM_ITEMS_DELETE / IDS_WANT_TO_DELETE_ITEMS. {0} = the item name (single)
  // or the count (multi), substituted the way MyFormatNew does. The original uses a
  // MB_YESNOCANCEL button set; we mirror it (Yes/No/Cancel). The recycle mode keeps the
  // SAME wording (no separate trash question upstream).
  QString delTitle;
  QString delMsg;
  {
    if (rows.size() == 1)
    {
      const int row = rows.first();
      const bool isFolder = _focused->model()->isFolder(row);
      const UString name = _focused->model()->itemName(row);
      const QString qn = UStr_toQ(name);
      if (isFolder)
      {
        delTitle = FmLang(IDS_CONFIRM_FOLDER_DELETE, QStringLiteral("Confirm Folder Delete"));
        delMsg = FmLang(IDS_WANT_TO_DELETE_FOLDER,
            QStringLiteral("Are you sure you want to delete the folder '{0}' and all its contents?"))
              .replace(QStringLiteral("{0}"), qn);
      }
      else
      {
        delTitle = FmLang(IDS_CONFIRM_FILE_DELETE, QStringLiteral("Confirm File Delete"));
        delMsg = FmLang(IDS_WANT_TO_DELETE_FILE,
            QStringLiteral("Are you sure you want to delete '{0}'?"))
              .replace(QStringLiteral("{0}"), qn);
      }
    }
    else
    {
      delTitle = FmLang(IDS_CONFIRM_ITEMS_DELETE, QStringLiteral("Confirm Multiple File Delete"));
      delMsg = FmLang(IDS_WANT_TO_DELETE_ITEMS,
          QStringLiteral("Are you sure you want to delete these {0} items?"))
            .replace(QStringLiteral("{0}"), QString::number(rows.size()));
    }
  }
  if (_headless)
  {
    // Headless read-back of the CHOSEN wording (the G.5e test prints these markers).
    printf("DELETE_CONFIRM_TITLE: %s\n", delTitle.toUtf8().constData());
    printf("DELETE_CONFIRM_MSG: %s\n", delMsg.toUtf8().constData());
    fflush(stdout);
  }
  else
  {
    // MB_YESNOCANCEL | MB_ICONQUESTION (PanelOperations.cpp:247-248): proceed only on Yes.
    if (QMessageBox::question(this, delTitle, delMsg,
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::No) != QMessageBox::Yes)
      return;
  }

  if (useTrash)
  {
    // FS items -> XDG trash via QFile::moveToTrash (freedesktop spec:
    // $XDG_DATA_HOME/Trash/files + info/<name>.trashinfo). selectedFullPaths()
    // gives the absolute FS paths (empty inside an archive — already excluded).
    // DATA-SAFETY: on ANY failure we do NOT silently fall through to unlink; we
    // surface the error and stop, so nothing is destroyed unexpectedly.
    const UStringVector paths = _focused->selectedFullPaths();
    bool allOk = true;
    for (unsigned i = 0; i < paths.Size(); i++)
    {
      const QString qp =
          QString::fromWCharArray(paths[i].Ptr(), (int)paths[i].Len());
      QString landed;
      if (!QFile::moveToTrash(qp, &landed))
      {
        allOk = false;
        if (_headless)
          printf("TRASH_FAIL: %s\n", qp.toUtf8().constData());
      }
      else if (_headless)
        printf("TRASH_OK: %s -> %s\n", qp.toUtf8().constData(),
            landed.toUtf8().constData());
    }
    if (!allOk && !_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Some items could not be moved to the Trash."));
    _focused->refresh();
    return;
  }

  // Permanent path (Shift+Del / trash off / inside an archive): the engine
  // IFolderOperations::Delete (a true unlink), unchanged from the prior behaviour.
  CMyComPtr<IFolderOperations> ops;
  if (!GetFolderOperations(_focused, ops))
    return;

  QtFsDeleteWorker worker;
  worker.FolderOperations = ops;
  worker.DisableUserQuestions = _headless;
  // G.4f : MODEL row -> engine realIndex (identity when ShowDots off; ".." excluded).
  for (int r : rows)
    worker.Indices.Add((UInt32)_focused->model()->modelRowToRealIndex(r));

  // B.5b crash fix : for an archive delete the worker's CommonUpdateOperation
  // Close()+ReOpen()s the CAgentFolder in place; freeze this panel's view first so
  // no GUI-thread relayout calls GetProperty on the half-rebuilt agent (race ->
  // SIGSEGV). FS delete leaves the CFSFolder valid, so it needs no suspend.
  if (inArchive)
    _focused->model()->setSuspended(true);

  worker.Create(UString(L"Deleting"), this);
  _focused->refresh();
}
void QtFileManagerWindow::onDelete() { doDelete(); }

// B.8 : Tools->Options. Loads the persisted settings, shows the dialog (GUI
// only), saves on accept, and pushes the view tweaks onto both panels.
void QtFileManagerWindow::doOptions()
{
  QtFmSettings::CInfo s;
  s.Load();
  if (_headless)
    return;   // the dialog is GUI-only; headless drives QSettings directly
  QtOptionsDialog dlg(this);
  dlg.Settings = s;
  dlg.fillFromState();
  if (dlg.exec() != QDialog::Accepted)
    return;
  // G.9b : detect a language change BEFORE saving, so we can retranslate the live
  // UI when (and only when) the chosen Lang actually differs (OptionsDialog.cpp:68
  // gates the MyLoadMenu/ReloadToolbars on langPage.LangWasChanged).
  const bool langChanged = (dlg.Settings.LangName != s.LangName);

  dlg.Settings.Save();
  applySettingsToPanels(dlg.Settings);
  // G.9c : pass the full three-way work-dir mode (System/Current/Specified).
  Qt_SetWorkDir(dlg.Settings.WorkDirPath.Ptr(), dlg.Settings.WorkDirMode);
  // P.2 : re-apply the language (ReloadLang equivalent) so a just-chosen Lang/*.txt
  // takes effect for any newly-built widget.
  QtFmSettings::StartupLoadLang();
  // G.9b : retranslate the ALREADY-built menu + toolbars LIVE, mirroring
  // OptionsDialog.cpp:68-75 (MyLoadMenu(true) + g_App.ReloadToolbars() +
  // ReloadLangItems()) — so the new language shows immediately without restart.
  if (langChanged)
    retranslateUi();
}
void QtFileManagerWindow::onOptions() { doOptions(); }

// G.10a : Tools->Benchmark. Mirrors MyLoadMenu.cpp MyBenchmark(totalMode):
// Benchmark(totalMode) opens GUI/BenchmarkDialog. No benchmark props are passed
// from the menu (the original Benchmark(totalMode) starts with an empty prop set;
// the dialog's own combos drive dict/threads). Total mode toggles IDM_BENCHMARK2.
static void RunFmBenchmark(QWidget *parent, bool headless, bool totalMode)
{
  CObjectVector<CProperty> props;   // empty : the dialog supplies dict/threads/passes
  if (headless)
  {
    // No display : drive the engine Bench() and print the textual report (the
    // BenchCon analogue). 1 iteration keeps the headless self-test short.
    RunBenchmarkConsole(props, 1, stdout);
    std::fflush(stdout);
    return;
  }
  QtBenchmarkDialog::Benchmark(props, k_NumBenchIterations_Default, totalMode, parent);
}
void QtFileManagerWindow::onBenchmark()      { RunFmBenchmark(this, _headless, false); }
void QtFileManagerWindow::onBenchmarkTotal() { RunFmBenchmark(this, _headless, true); }

void QtFileManagerWindow::applySettingsToPanels(const QtFmSettings::CInfo &s)
{
  if (_left)  _left->applySettings(s);
  if (_right) _right->applySettings(s);
}

// === B.5b test hooks ========================================================
void QtFileManagerWindow::focusPanelForTest(QtPanel *p)
{
  if (p) setFocusedPanel(p);
}
void QtFileManagerWindow::selectRowForTest(QtPanel *p, int sourceRow)
{
  if (p) p->selectSourceRowForTest(sourceRow);
}
void QtFileManagerWindow::selectRowsForTest(QtPanel *p, const QVector<int> &sourceRows)
{
  if (p) p->selectSourceRowsForTest(sourceRows);
}
void QtFileManagerWindow::refreshStatusBarForTest()
{
  updateStatusBar();
}
QString QtFileManagerWindow::statusSelText() const
{
  return _statusSel ? _statusSel->text() : QString();
}
QString QtFileManagerWindow::statusFocSizeText() const
{
  return _statusFocSize ? _statusFocSize->text() : QString();
}
QString QtFileManagerWindow::statusFocDateText() const
{
  return _statusFocDate ? _statusFocDate->text() : QString();
}

// === Rename (F2) ============================================================
// Mirrors CPanel::Rename: prompt a new name -> _folder->Rename(index, newName).
// Synchronous one-shot (the original does not use a progress thread for rename).
void QtFileManagerWindow::doRename(const UString &newNameOverride)
{
  if (!checkFsOpAllowed("Rename"))
    return;

  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.isEmpty())
    return;
  const int row = rows.first();
  const UString oldName = _focused->model()->itemName(row);

  UString newName = newNameOverride;
  if (newName.IsEmpty())
  {
    if (_headless)
      return;
    bool ok = false;
    const QString text = QInputDialog::getText(this, QStringLiteral("Rename"),
        QStringLiteral("New name:"), QLineEdit::Normal,
        QString::fromWCharArray(oldName.Ptr(), (int)oldName.Len()), &ok);
    if (!ok)
      return;
    newName = Q_toU2(text);
  }
  if (newName.IsEmpty() || newName == oldName)
    return;

  CMyComPtr<IFolderOperations> ops;
  if (!GetFolderOperations(_focused, ops))
    return;

  // G.4f : MODEL row -> engine realIndex for the engine Rename (identity when
  // ShowDots off; the ".." row is excluded from `rows`).
  const UInt32 realIndex = (UInt32)_focused->model()->modelRowToRealIndex(row);
  const HRESULT res = ops->Rename(realIndex, newName, nullptr);
  if (res != S_OK && !_headless)
    QMessageBox::warning(this, QStringLiteral("7-Zip"),
        QStringLiteral("Rename failed."));
  _focused->refresh();
}
void QtFileManagerWindow::onRename() { doRename(); }

// === Create Folder (F7) =====================================================
// Mirrors CPanel::CreateFolder: prompt a name -> _folder->CreateFolder(name).
void QtFileManagerWindow::doCreateFolder(const UString &nameOverride)
{
  if (!checkFsOpAllowed("Create Folder"))
    return;

  UString name = nameOverride;
  if (name.IsEmpty())
  {
    if (_headless)
      return;
    bool ok = false;
    // Dlg_CreateFolder seeds title IDS_CREATE_FOLDER, label IDS_CREATE_FOLDER_NAME,
    // default IDS_CREATE_FOLDER_DEFAULT_NAME (resource.rc:227/229/231).
    const QString text = QInputDialog::getText(this,
        FmLang(IDS_CREATE_FOLDER, QStringLiteral("Create Folder")),
        FmLang(IDS_CREATE_FOLDER_NAME, QStringLiteral("Folder name:")), QLineEdit::Normal,
        FmLang(IDS_CREATE_FOLDER_DEFAULT_NAME, QStringLiteral("New Folder")), &ok);
    if (!ok)
      return;
    name = Q_toU2(text);
  }
  if (name.IsEmpty())
    return;

  // G.5c : validate the typed name exactly as CPanel::CreateFolder (PanelOperations.cpp
  // :379-394): IsCorrectFsName rejects "."/".." and (the B.5a fail-safe) an embedded
  // path separator; on rejection show MessageBox_Error_HRESULT(E_INVALIDARG). The
  // SAME shared validator the inline rename uses (QtFolderModel::IsCorrectNewFsName).
  if (!QtFolderModel::IsCorrectNewFsName(name))
  {
    if (_headless)
      printf("CREATE_FOLDER_REJECTED: %s\n", (const char *)GetOemString(name));
    else
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          UStr_toQ(HResultToMessage(E_INVALIDARG)));
    return;
  }

  CMyComPtr<IFolderOperations> ops;
  if (!GetFolderOperations(_focused, ops))
    return;

  const HRESULT res = ops->CreateFolder(name, nullptr);
  if (res != S_OK && !_headless)
    QMessageBox::warning(this, QStringLiteral("7-Zip"),
        FmLang(IDS_CREATE_FOLDER_ERROR, QStringLiteral("Error Creating Folder")));
  _focused->refresh();
}
void QtFileManagerWindow::onCreateFolder() { doCreateFolder(); }

// === Create File (Ctrl+N) ===================================================
// Mirrors CPanel::CreateFile: prompt a name -> _folder->CreateFile(name).
void QtFileManagerWindow::doCreateFile(const UString &nameOverride)
{
  if (!checkFsOpAllowed("Create File"))
    return;

  UString name = nameOverride;
  if (name.IsEmpty())
  {
    if (_headless)
      return;
    bool ok = false;
    // CComboDialog seeds title IDS_CREATE_FILE, static IDS_CREATE_FILE_NAME, value
    // IDS_CREATE_FILE_DEFAULT_NAME (PanelOperations.cpp:438-440). G.5h : the prompt
    // default is IDS_CREATE_FILE_DEFAULT_NAME ("New File"), not a hard-coded literal.
    const QString text = QInputDialog::getText(this,
        FmLang(IDS_CREATE_FILE, QStringLiteral("Create File")),
        FmLang(IDS_CREATE_FILE_NAME, QStringLiteral("File Name:")), QLineEdit::Normal,
        FmLang(IDS_CREATE_FILE_DEFAULT_NAME, QStringLiteral("New File")), &ok);
    if (!ok)
      return;
    name = Q_toU2(text);
  }
  if (name.IsEmpty())
    return;

  // G.5c : validate the typed name (CPanel::CreateFile, PanelOperations.cpp:437-460
  // runs CorrectFsPath; on Linux that is the identity, so the meaningful guard is the
  // shared IsCorrectFsName + path-separator reject -> MessageBox_Error_HRESULT(E_INVALIDARG)).
  if (!QtFolderModel::IsCorrectNewFsName(name))
  {
    if (_headless)
      printf("CREATE_FILE_REJECTED: %s\n", (const char *)GetOemString(name));
    else
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          UStr_toQ(HResultToMessage(E_INVALIDARG)));
    return;
  }

  CMyComPtr<IFolderOperations> ops;
  if (!GetFolderOperations(_focused, ops))
    return;

  const HRESULT res = ops->CreateFile(name, nullptr);
  if (res != S_OK && !_headless)
    QMessageBox::warning(this, QStringLiteral("7-Zip"),
        FmLang(IDS_CREATE_FILE_ERROR, QStringLiteral("Error Creating File")));
  _focused->refresh();
}
void QtFileManagerWindow::onCreateFile() { doCreateFile(); }

// ============================================================================
// === B.7a : item actions — Properties / Comment / Open-Outside / View / Edit =
// Mirrors CPanel::Properties (PanelMenu.cpp:172), CPanel::ChangeComment
// (PanelOperations.cpp:487), and CPanel::OpenItem / EditItem (PanelItemOpen.cpp:
// 820/950/1461). Qt-side only; the property enumeration and value formatting are
// the same engine triad the model already uses.
// ============================================================================

// === Properties (Alt+Enter) =================================================
// CPanel::Properties for a single selected item loops the focused folder's
// GetNumberOfProperties/GetPropertyInfo/GetProperty triad into a 2-col name/value
// list. Works for FS rows (CFSFolder) and archive rows (CAgentFolder) alike. The
// multi-select aggregate summary (G.5a) and the open-archive IFolderArcProps
// metadata (Q.5) branches are handled below; raw hash props come via
// IArchiveGetRawProps.
void QtFileManagerWindow::doProperties()
{
  if (!_focused)
    return;
  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.isEmpty())
  {
    if (!_headless)
      QMessageBox::information(this, QStringLiteral("7-Zip"),
          QStringLiteral("Select an item first."));
    return;
  }
  QtPropertiesDialog dlg(this);
  dlg.Folder = _focused->currentFolder();   // FS CFSFolder or CAgentFolder
  // G.4f : MODEL row -> engine realIndex (identity when ShowDots off; ".." excluded).
  dlg.Index = (UInt32)_focused->model()->modelRowToRealIndex(rows.first());
  // G.5a : pass ALL selected engine realIndices so the dialog can render the
  // multi-select aggregate summary (PanelMenu.cpp:260-295) when >1 is selected.
  // The ".." pseudo-row (kParentRow = -1) is excluded — it is not a real item.
  for (int row : rows)
  {
    const int ri = _focused->model()->modelRowToRealIndex(row);
    if (ri >= 0)
      dlg.Indices.append((UInt32)ri);
  }
  dlg.fill();
  if (_headless)
  {
    dlg.dumpTo(stdout);
    return;
  }
  dlg.exec();
}
void QtFileManagerWindow::onProperties() { doProperties(); }

// G.5b : headless reachability test for the Properties dialog interactivity
// (Ctrl+C copy + value viewer). Builds + fills the dialog over the current
// selection exactly like doProperties, then drives the copy/value-viewer self
// test and prints markers so the harness can assert both paths are wired.
void QtFileManagerWindow::doPropertiesInteractTest(int valueViewerRow)
{
  if (!_focused)
    return;
  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.isEmpty())
    return;
  QtPropertiesDialog dlg(this);
  dlg.Folder = _focused->currentFolder();
  dlg.Index = (UInt32)_focused->model()->modelRowToRealIndex(rows.first());
  for (int row : rows)
  {
    const int ri = _focused->model()->modelRowToRealIndex(row);
    if (ri >= 0)
      dlg.Indices.append((UInt32)ri);
  }
  dlg.fill();
  QString clip, viewer;
  if (!dlg.runInteractivitySelfTest(valueViewerRow, clip, viewer))
  {
    printf("PROPS_INTERACT: no rows\n");
    return;
  }
  // The clipboard text is the "Name: Value\n" form (one row per line).
  printf("PROPS_INTERACT_CLIPBOARD_BEGIN\n%s\nPROPS_INTERACT_CLIPBOARD_END\n",
      clip.toUtf8().constData());
  printf("PROPS_INTERACT_VIEWER_ROW=%d VALUE=%s\n",
      valueViewerRow, viewer.toUtf8().constData());
}

// === Comment (Ctrl+Z) =======================================================
// Mirror CPanel::ChangeComment (PanelOperations.cpp:487-533): read the current
// kpidComment via GetProperty, edit it, then IFolderOperations::SetProperty
// (realIndex, kpidComment, VT_BSTR). Gated to updatable archives like B.5b.
void QtFileManagerWindow::doComment(const UString &textOverride, bool commentGet)
{
  // commentGet (headless --comment-get): just print the current comment without
  // the updatable gate (read-only archives still carry a comment).
  if (!commentGet && !checkFsOpAllowed("Comment"))
    return;
  if (!_focused)
    return;

  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.isEmpty())
    return;
  // G.4f : MODEL row -> engine realIndex (identity when ShowDots off; ".." excluded).
  const UInt32 realIndex = (UInt32)_focused->model()->modelRowToRealIndex(rows.first());
  IFolderFolder *folder = _focused->currentFolder();
  if (!folder)
    return;

  // Read the existing comment (PanelOperations.cpp:503-511).
  UString comment;
  {
    NCOM::CPropVariant pv;
    if (folder->GetProperty(realIndex, kpidComment, &pv) == S_OK)
    {
      if (pv.vt == VT_BSTR)
        comment = pv.bstrVal;
      else if (pv.vt != VT_EMPTY)
        return; // a non-string comment prop: bail like the original
    }
  }

  if (commentGet)
  {
    // Headless read-back hook.
    printf("COMMENT: %s\n",
        (const char *)GetOemString(comment));
    return;
  }

  UString newComment = textOverride;
  if (textOverride.IsEmpty())
  {
    if (_headless)
      return;
    // CComboDialog : title "<relpath> : Comment", a static label + an editable
    // field seeded with the existing comment.
    bool ok = false;
    // G.5g : CComboDialog seeds Title = "<relpath> : <IDS_COMMENT>" and the static
    // label = IDS_COMMENT2 ("&Comment:") (PanelOperations.cpp:512-518). The editor is
    // multi-line (getMultiLineText, the CComboDialog analogue) seeded with the existing
    // kpidComment. Strip the leading Win32 '&' mnemonic from the label for the plain
    // Qt prompt.
    const UString name = _focused->model()->itemName(rows.first());
    const QString title = UStr_toQ(name)
        + QStringLiteral(" : ")
        + FmLang(IDS_COMMENT, QStringLiteral("Comment"));
    QString label = FmLang(IDS_COMMENT2, QStringLiteral("&Comment:"));
    label.remove(QLatin1Char('&'));
    const QString text = QInputDialog::getMultiLineText(this, title, label,
        QString::fromWCharArray(comment.Ptr(), (int)comment.Len()), &ok);
    if (!ok)
      return;
    newComment = Q_toU2(text);
  }

  CMyComPtr<IFolderOperations> ops;
  if (!GetFolderOperations(_focused, ops))
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("This folder does not support comments."));
    return;
  }

  NCOM::CPropVariant pv(newComment);   // VT_BSTR
  const HRESULT res = ops->SetProperty(realIndex, kpidComment, &pv, nullptr);
  if (res != S_OK && !_headless)
  {
    // E_NOINTERFACE -> unsupported operation (PanelOperations.cpp:526-528).
    QMessageBox::warning(this, QStringLiteral("7-Zip"),
        (res == E_NOINTERFACE)
            ? QStringLiteral("This operation is not supported.")
            : QStringLiteral("Set Comment failed."));
  }
  // RefreshListCtrl analogue: re-bind the (reopened-in-place) archive.
  _focused->refresh();
}
void QtFileManagerWindow::onComment() { doComment(); }

// === Open-Outside (Shift+Enter) / View (F3) / Edit (F4) =====================
// Mirror CPanel::OpenItem / EditItem (PanelItemOpen.cpp). FS item -> the OS open
// (xdg-open; Edit launches $VISUAL/$EDITOR else xdg-open). Archive item -> reuse
// B.5c extract-to-temp, then open the temp path. View = read-only open; Edit of an
// updatable-archive interior item writes changes back via doEditInArchive
// (QFileSystemWatcher watch + CopyFromFile, G.2a).
void QtFileManagerWindow::doOpen(int kind)
{
  if (!_focused)
    return;
  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.isEmpty())
  {
    if (!_headless)
      QMessageBox::information(this, QStringLiteral("7-Zip"),
          QStringLiteral("Select an item first."));
    return;
  }

  // G.3e : IsVirus_Message guard at the EXTERNAL-launch chokepoint. doOpen is only
  // ever reached for the tryExternal commands (Open Outside / View / Edit, and the
  // double-click -> onOpenExternally route), exactly the OpenItem/OpenItemInArchive
  // entries that run IsVirus_Message before launching (PanelItemOpen.cpp:955-957 /
  // :1470-1472). A spoofed selected name blocks the whole open (the panel emits the
  // IDS_VIRUS warning via onPanelOpenBlockedAsVirus). The double-click path is also
  // guarded earlier in onLeafActivated; this is the idempotent menu/toolbar guard.
  if (_focused->checkSelectionForVirus())
    return;

  const bool inArchive = _focused->isInArchive();

  // G.2a : Edit (F4) of an in-archive item -> extract-to-temp, launch the editor,
  // and WATCH the temp for changes (write the edit BACK into the archive on save).
  // This is the faithful CPanel::EditItem -> OpenItemInArchive(editMode) analog;
  // View (F3) and Open-Outside stay read-only. The branch handles its own editor
  // launch + watcher, so it returns instead of falling through to the read-only
  // open loop below.
  if (inArchive && kind == (int)OpenKind::Edit && _focused->isUpdatableArchive())
  {
    doEditInArchive();
    return;
  }

  UStringVector paths;
  if (inArchive)
  {
    // Archive item: extract the selection to a window-owned temp dir (B.5c),
    // then open the temp path. View (F3) / Open-Outside are read-only opens from
    // temp. (Edit of an item in a READ-ONLY archive also lands here — there is no
    // write-back path, so it degrades to a read-only open, as before.)
    paths = _focused->extractSelectionToTempPaths();
  }
  else
  {
    paths = _focused->selectedFullPaths();   // FS absolute paths
  }

  if (paths.Size() == 0)
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Nothing to open."));
    return;
  }

  for (unsigned i = 0; i < paths.Size(); i++)
  {
    const QString p = QString::fromWCharArray(paths[i].Ptr(), (int)paths[i].Len());

    // Edit on an FS item -> $VISUAL else $EDITOR (StartEditApplication's
    // registry-editor + notepad fallback, mirrored to the env-var convention).
    // Edit of an archive item degrades to View (read-only open from temp).
    if (kind == (int)OpenKind::Edit && !inArchive)
    {
      // B.8 : the Editor command is now sourced from QtFmSettings — the Options
      // Editor tab writes [FM] Editor; GetEditorCommand() falls back to $VISUAL
      // then $EDITOR (preserving the prior behaviour) and returns empty if none,
      // in which case we fall through to xdg-open (StartEditApplication's notepad
      // fallback analogue).
      const UString edU = QtFmSettings::GetEditorCommand();
      if (!edU.IsEmpty())
      {
        const QString ed = QString::fromWCharArray(edU.Ptr(), (int)edU.Len());
        if (_headless)
          printf("OPEN_CMD: %s %s\n", ed.toUtf8().constData(),
              p.toUtf8().constData());
        else
          QProcess::startDetached(ed, QStringList() << p);
        continue;
      }
    }

    if (_headless)
    {
      printf("OPEN_CMD: xdg-open %s\n", p.toUtf8().constData());
      continue;
    }
    // StartApplication (ShellExecute) -> the registered handler (xdg-open).
    QDesktopServices::openUrl(QUrl::fromLocalFile(p));
  }
}
void QtFileManagerWindow::onOpenOutside() { doOpen((int)OpenKind::OpenOutside); }
void QtFileManagerWindow::onView()        { doOpen((int)OpenKind::View); }
void QtFileManagerWindow::onEdit()        { doOpen((int)OpenKind::Edit); }


// === G.2a : Edit (F4) of an in-archive item -> write-back into the archive ===
// CPanel::EditItem (PanelItemOpen.cpp:820) -> OpenItemInArchive(editMode=true)
// extracts the item to a temp, StartEditApplication launches the editor, and
// Thread_Create(MyThreadFunction) watches the temp; on change it prompts
// IDS_WANT_UPDATE_MODIFIED_FILE and OnOpenItemChanged -> CThreadCopyFrom ->
// CopyFromFile writes the temp back into the archive. The Win32 watch uses a
// Toolhelp32 process snapshot (no Linux equivalent), so we substitute a
// QFileSystemWatcher on the temp file — the same _WIN32-substitution discipline
// the rest of this port uses.

// True if the temp's mtime now differs from the value captured at launch.
bool QtFileManagerWindow::editTempWasChanged(const CEditWatch &w)
{
  const QString qp =
      QString::fromWCharArray(fs2us(w.TempPath).Ptr(), (int)fs2us(w.TempPath).Len());
  QFileInfo fi(qp);
  if (!fi.exists())
    return false;
  return fi.lastModified().toMSecsSinceEpoch() != w.OriginalMtime;
}

// Register a watch on an extracted temp. Lazily creates the shared watcher and
// connects its fileChanged() to onEditedFileChanged.
bool QtFileManagerWindow::startArchiveEditWatch(QtPanel *panel,
    IFolderFolder *folder, UInt32 realIndex, const UString &entryName,
    const FString &tempPath)
{
  if (!panel || !folder)
    return false;

  const UString tpU = fs2us(tempPath);
  const QString qp = QString::fromWCharArray(tpU.Ptr(), (int)tpU.Len());
  QFileInfo fi(qp);
  if (!fi.exists())
    return false;

  CEditWatch w;
  w.Panel = panel;
  w.Folder = folder;                         // hold a ref so the agent stays alive
  w.RealIndex = realIndex;
  w.EntryName = entryName;
  w.TempPath = tempPath;
  w.OriginalMtime = fi.lastModified().toMSecsSinceEpoch();
  w.WrittenBack = false;
  _openEdits.append(w);

  if (!_editWatcher)
  {
    _editWatcher = new QFileSystemWatcher(this);
    connect(_editWatcher, &QFileSystemWatcher::fileChanged,
        this, &QtFileManagerWindow::onEditedFileChanged);
  }
  _editWatcher->addPath(qp);
  return true;
}

// CPanel::EditItem (PanelItemOpen.cpp:820) for an in-archive item. Extract the
// single selected entry to a window-owned temp, launch the editor, and watch.
void QtFileManagerWindow::doEditInArchive()
{
  if (!_focused)
    return;
  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.size() != 1)
  {
    // The original edits one item; for a multi-selection fall back to the
    // read-only open (extract-to-temp + xdg-open of each), which doOpen's
    // non-Edit branch already does. Re-enter doOpen as a View to avoid recursion.
    if (!_headless)
      QMessageBox::information(this, QStringLiteral("7-Zip"),
          QStringLiteral("Edit: select a single file inside the archive."));
    return;
  }
  const int row = rows.first();
  const UString entryName = _focused->model()->itemName(row);

  // Capture the CAgentFolder (its IFolderOperations performs the write-back) and
  // the realIndex BEFORE extraction. The realIndex is re-resolved by name at
  // write-back time (the agent ReOpen()s in place after any in-place op).
  IFolderFolder *folder = _focused->currentFolder();
  if (!folder)
    return;

  // Extract the single selection to a window-owned temp (B.5c). With one row
  // selected, extractSelectionToTempPaths() returns exactly that file's temp path.
  const UStringVector paths = _focused->extractSelectionToTempPaths();
  if (paths.Size() != 1)
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Edit: could not extract the item for editing."));
    return;
  }
  const FString tempPath = us2fs(paths[0]);

  // Register the watch BEFORE launching the editor (so a fast save isn't missed).
  // G.4f : MODEL row -> engine realIndex (identity when ShowDots off; ".." excluded).
  startArchiveEditWatch(_focused, folder,
      (UInt32)_focused->model()->modelRowToRealIndex(row), entryName, tempPath);

  // Launch the editor, exactly like doOpen's FS-Edit branch (StartEditApplication:
  // $VISUAL else $EDITOR, else xdg-open as the notepad fallback analogue).
  const QString p =
      QString::fromWCharArray(paths[0].Ptr(), (int)paths[0].Len());
  const UString edU = QtFmSettings::GetEditorCommand();
  if (_headless)
  {
    // Headless can't run a real editor; the watcher is unreachable. The dedicated
    // --arc-edit hook (arcEditWriteBackForTest) drives the write-back directly.
    printf("ARC-EDIT-LAUNCH: watch=%s editor=%s\n",
        p.toUtf8().constData(),
        edU.IsEmpty() ? "xdg-open" : "configured");
    return;
  }
  if (!edU.IsEmpty())
  {
    const QString ed = QString::fromWCharArray(edU.Ptr(), (int)edU.Len());
    QProcess::startDetached(ed, QStringList() << p);
  }
  else
  {
    QDesktopServices::openUrl(QUrl::fromLocalFile(p));
  }
}

// QFileSystemWatcher fired: the editor saved a watched temp. Mirror the Win32
// MyThreadFunction "WasChanged" branch (PanelItemOpen.cpp:1245): prompt
// IDS_WANT_UPDATE_MODIFIED_FILE and, on Yes, CopyFromFile the temp back.
void QtFileManagerWindow::onEditedFileChanged(const QString &tempPath)
{
  // Find the matching tracked edit (by temp path). Some editors replace the file
  // (delete+rename), which drops the watch; re-add it so a subsequent save still
  // fires while the edit is still pending.
  const UString keyU = Q_toU2(tempPath);
  int found = -1;
  for (int i = 0; i < _openEdits.size(); i++)
    if (fs2us(_openEdits[i].TempPath) == keyU)
    {
      found = i;
      break;
    }
  if (found < 0)
    return;

  if (_editWatcher && QFileInfo::exists(tempPath)
      && !_editWatcher->files().contains(tempPath))
    _editWatcher->addPath(tempPath);   // re-arm after a replace-on-save

  CEditWatch &w = _openEdits[found];
  if (w.WrittenBack || !editTempWasChanged(w))
    return;

  const QString relQ =
      QString::fromWCharArray(w.EntryName.Ptr(), (int)w.EntryName.Len());
  const QString msg =
      FmLang(IDS_WANT_UPDATE_MODIFIED_FILE,
             QStringLiteral("File '%1' was modified.\nDo you want to update it in the archive?"))
      .arg(relQ);
  if (QMessageBox::question(this, QStringLiteral("7-Zip"), msg,
          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
  {
    // User declined this save: re-baseline the mtime so this exact state is not
    // re-prompted, but keep the edit PENDING so a LATER save (or the Up/close
    // flush) re-asks — the original re-prompts on each subsequent change too.
    QFileInfo fi(tempPath);
    if (fi.exists())
      w.OriginalMtime = fi.lastModified().toMSecsSinceEpoch();
    return;
  }

  const HRESULT res = runEditWriteBack(w);
  if (res != S_OK)
  {
    if (!_headless)
      QMessageBox::critical(this, QStringLiteral("7-Zip"),
          FmLang(IDS_CANNOT_UPDATE_FILE, QStringLiteral("Cannot update the file"))
              + QStringLiteral("\n%1").arg(relQ));
    return;
  }
  // Written back: stop watching this temp (the edit cycle is done; a fresh Edit
  // re-extracts). Remove from the list.
  if (_editWatcher)
    _editWatcher->removePath(tempPath);
  _openEdits.remove(found);
}

// CPanel::OnOpenItemChanged -> CThreadCopyFrom (PanelItemOpen.cpp:1014/1009): the
// faithful write-back. Re-resolve the realIndex by name (the agent ReOpen()ed in
// place after any prior in-place op, so the cached index can be stale — exactly
// what OnOpenItemChanged re-scans for), suspend the view (B.5b crash-fix), run the
// CopyFromFile worker, then refresh()/re-bind.
HRESULT QtFileManagerWindow::runEditWriteBack(CEditWatch &w)
{
  if (!w.Panel || !w.Folder)
    return E_FAIL;

  // Re-resolve the realIndex by EntryName against the (possibly-reopened) folder.
  UInt32 numItems = 0;
  if (w.Folder->GetNumberOfItems(&numItems) != S_OK)
    return E_FAIL;
  UInt32 realIndex = w.RealIndex;
  bool resolved = false;
  if (realIndex < numItems)
  {
    NCOM::CPropVariant prop;
    if (w.Folder->GetProperty(realIndex, kpidName, &prop) == S_OK
        && prop.vt == VT_BSTR && w.EntryName == prop.bstrVal)
      resolved = true;
  }
  if (!resolved)
  {
    for (UInt32 i = 0; i < numItems; i++)
    {
      NCOM::CPropVariant prop;
      if (w.Folder->GetProperty(i, kpidName, &prop) == S_OK
          && prop.vt == VT_BSTR && w.EntryName == prop.bstrVal)
      {
        realIndex = i;
        resolved = true;
        break;
      }
    }
  }
  if (!resolved)
    return E_FAIL;   // entry vanished (deleted in the archive meanwhile)

  CMyComPtr<IFolderOperations> ops;
  w.Folder->QueryInterface(IID_IFolderOperations, (void **)&ops);
  if (!ops)
    return E_NOINTERFACE;

  QtArchiveCopyFromWorker worker;
  worker.FolderOperations = ops;
  worker.Index = realIndex;
  worker.FullPath = fs2us(w.TempPath);
  worker.DisableUserQuestions = _headless;
  // Encrypted archive : prompt for the repack password (symmetric with Add).
  worker.PasswordPrompt = _passwordPrompt;

  // B.5b crash-fix : CopyFromFile does Close()+ReOpen() on the CAgentFolder in
  // place; freeze the panel's view (0 rows, no GetProperty) so a GUI-thread
  // relayout doesn't race the worker-thread rebind -> SIGSEGV. The refresh() below
  // lifts it and re-binds to the reopened root.
  w.Panel->model()->setSuspended(true);

  const HRESULT res = worker.Create(UString(L"Updating"), this);

  // Re-bind the panel to the reopened archive (clears the suspend). After this the
  // cached realIndex is stale for any future write-back — re-resolved next time.
  w.Panel->refresh();
  w.WrittenBack = (res == S_OK);
  return res;
}

// OpenParentArchiveFolder (PanelItemOpen.cpp:598): before a panel exits its
// archive (Up) or on FM close, any tracked-but-unwritten edit whose temp changed
// gets the same IDS_WANT_UPDATE_MODIFIED_FILE prompt + write-back.
void QtFileManagerWindow::flushPendingEditsForPanel(QtPanel *panel)
{
  if (!panel || _openEdits.isEmpty())
    return;
  for (int i = _openEdits.size() - 1; i >= 0; i--)
  {
    CEditWatch &w = _openEdits[i];
    if (w.Panel != panel || w.WrittenBack)
      continue;
    if (!editTempWasChanged(w))
    {
      // Unchanged: drop the watch (nothing to write).
      if (_editWatcher)
        _editWatcher->removePath(
            QString::fromWCharArray(fs2us(w.TempPath).Ptr(),
                                    (int)fs2us(w.TempPath).Len()));
      _openEdits.remove(i);
      continue;
    }
    const QString relQ =
        QString::fromWCharArray(w.EntryName.Ptr(), (int)w.EntryName.Len());
    bool doWrite = _headless;   // headless: write silently (no modal on exit)
    if (!_headless)
    {
      const QString msg =
          FmLang(IDS_WANT_UPDATE_MODIFIED_FILE,
                 QStringLiteral("File '%1' was modified.\nDo you want to update it in the archive?"))
          .arg(relQ);
      doWrite = (QMessageBox::question(this, QStringLiteral("7-Zip"), msg,
                    QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes);
    }
    if (doWrite)
      runEditWriteBack(w);
    if (_editWatcher)
      _editWatcher->removePath(
          QString::fromWCharArray(fs2us(w.TempPath).Ptr(),
                                  (int)fs2us(w.TempPath).Len()));
    if (i < _openEdits.size())
      _openEdits.remove(i);
  }
}

// G.2a : headless edit-writeback self-check (the GUI watcher needs a real editor).
// Extract <sourceRow> to a temp, OVERWRITE it with `content` (or env
// SEVENZQT_EDIT_CONTENT, default "EDITED-BY-G2A\n"), then run the SAME write-back
// path directly. Returns true on a successful CopyFromFile.
bool QtFileManagerWindow::arcEditWriteBackForTest(int sourceRow, const UString &content)
{
  if (!_focused || !_focused->isInArchive() || !_focused->isUpdatableArchive())
  {
    fprintf(stderr, "arc-edit: focused panel is not an updatable archive\n");
    return false;
  }
  IFolderFolder *folder = _focused->currentFolder();
  if (!folder)
    return false;
  _focused->selectSourceRowForTest(sourceRow);
  const UString entryName = _focused->model()->itemName(sourceRow);

  const UStringVector paths = _focused->extractSelectionToTempPaths();
  if (paths.Size() != 1)
  {
    fprintf(stderr, "arc-edit: extraction did not yield exactly one temp file\n");
    return false;
  }
  const FString tempPath = us2fs(paths[0]);

  // Determine the new content: explicit arg > env SEVENZQT_EDIT_CONTENT > default.
  AString bytes;
  if (!content.IsEmpty())
    bytes = UnicodeStringToMultiByte(content, CP_UTF8);
  else if (const char *env = getenv("SEVENZQT_EDIT_CONTENT"))
    bytes = env;
  else
    bytes = "EDITED-BY-G2A\n";

  // OVERWRITE the extracted temp (simulating the editor's save).
  {
    NFile::NIO::COutFile outFile;
    if (!outFile.Create_ALWAYS(tempPath))
    {
      fprintf(stderr, "arc-edit: could not open temp for writing\n");
      return false;
    }
    if (bytes.Len() != 0 && !outFile.WriteFull(bytes.Ptr(), (size_t)bytes.Len()))
    {
      fprintf(stderr, "arc-edit: temp write failed\n");
      return false;
    }
  }

  CEditWatch w;
  w.Panel = _focused;
  w.Folder = folder;
  // G.4f : MODEL row -> engine realIndex (identity when ShowDots off).
  w.RealIndex = (UInt32)_focused->model()->modelRowToRealIndex(sourceRow);
  w.EntryName = entryName;
  w.TempPath = tempPath;
  w.OriginalMtime = -1;
  w.WrittenBack = false;

  const HRESULT res = runEditWriteBack(w);
  if (res != S_OK)
  {
    fprintf(stderr, "arc-edit: CopyFromFile write-back failed (0x%08X)\n",
        (unsigned)res);
    return false;
  }
  const AString entryUtf8 = UnicodeStringToMultiByte(entryName, CP_UTF8);
  printf("ARC-EDIT: wrote back %s (%u bytes)\n",
      entryUtf8.Ptr(), (unsigned)bytes.Len());
  return true;
}


// === B.7c Split / Combine / Link / Diff / Favorites =========================

// CApp::Split (PanelSplitFile.cpp:235-342). Split the focused FS file into
// <name>.001/.002/... on a QtSplitWorker (CThreadSplit analogue).
void QtFileManagerWindow::doSplit(const UString &volOverride)
{
  // FS-only gate (Is_IO_FS_Folder). Split/Combine/Link/Diff are strictly FS.
  if (!_focused || _focused->isInArchive())
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Operation is not supported for archive items"));
    return;
  }

  // Exactly one non-folder item (IDS_SELECT_ONE_FILE).
  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.size() != 1 || _focused->model()->isFolder(rows.first()))
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Select one file"));
    return;
  }
  const int row = rows.first();
  const UString itemName = _focused->model()->itemName(row);
  const UString srcPath = _focused->currentFsDirPath();   // trailing sep
  const FString fullPath = us2fs(srcPath + itemName);

  // Dest default: the OTHER panel's FS dir if it's an FS folder, else source.
  UString path = srcPath;
  if (otherPanel() && !otherPanel()->isInArchive())
  {
    const UString od = otherPanel()->currentFsDirPath();
    if (!od.IsEmpty())
      path = od;
  }

  // VolumeSizes : dialog, or ParseVolumeSizes(volOverride) headless.
  CRecordVector<UInt64> volumeSizes;
  if (_headless)
  {
    if (volOverride.IsEmpty())
      return;
    UString vs = volOverride;
    vs.Trim();
    // Parse the override string through the same re-lifted ParseVolumeSizes the
    // dialog's OnOK uses (exposed via QtSplit_ParseVolumeSizes).
    if (!QtSplit_ParseVolumeSizes(vs, volumeSizes) || volumeSizes.IsEmpty())
    {
      fprintf(stderr, "split: incorrect volume size '%s'\n", (const char *)GetAnsiString(vs));
      return;
    }
  }
  else
  {
    QtSplitDialog dlg(this);
    dlg.FilePath = itemName;
    dlg.Path = path;
    dlg.fillFromState();
    if (dlg.exec() != QDialog::Accepted)
      return;
    path = dlg.Path;
    volumeSizes = dlg.VolumeSizes;
  }

  // Validations (PanelSplitFile.cpp:274-303): file exists, size > front vol size,
  // >=100 confirm, mkdir -p the dest.
  NFile::NFind::CFileInfo fileInfo;
  if (!fileInfo.Find(fullPath))
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"), QStringLiteral("Cannot find file"));
    return;
  }
  if (fileInfo.Size <= volumeSizes.FrontItem())
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("The volume size must be smaller than the size of the file"));
    return;
  }
  const UInt64 numVolumes = GetNumberOfVolumes(fileInfo.Size, volumeSizes);
  if (!_headless && numVolumes >= 100)
  {
    const QMessageBox::StandardButton r = QMessageBox::question(this,
        QStringLiteral("7-Zip"),
        QStringLiteral("The operation will create more than %1 volumes. Continue?")
            .arg((qulonglong)numVolumes),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (r != QMessageBox::Yes)
      return;
  }

  NWindows::NFile::NName::NormalizeDirPathPrefix(path);
  if (!NFile::NDir::CreateComplexDir(us2fs(path)))
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"), QStringLiteral("Cannot create folder"));
    return;
  }

  QtSplitWorker worker;
  worker.FilePath = fullPath;
  worker.VolBasePath = us2fs(path + itemName);
  worker.VolumeSizes = volumeSizes;
  worker.NumVolumes = numVolumes;
  worker.DisableUserQuestions = _headless;
  worker.Create(UString(L"Splitting"), this);

  if (otherPanel())
    otherPanel()->refresh();
  _focused->refresh();
}

void QtFileManagerWindow::onSplit() { doSplit(); }


// CApp::Combine (PanelSplitFile.cpp:419-562). Reassemble a <name>.001 part series.
void QtFileManagerWindow::doCombine(const UString &destOverride)
{
  if (!_focused || _focused->isInArchive())
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Operation is not supported for archive items"));
    return;
  }

  const QVector<int> rows = _focused->selectedSourceRows();
  if (rows.size() != 1 || _focused->model()->isFolder(rows.first()))
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Select one file"));
    return;
  }
  const int row = rows.first();
  const UString itemName = _focused->model()->itemName(row);
  const UString srcPath = _focused->currentFsDirPath();

  // First-part detection (CVolSeqName::ParseName).
  CVolSeqName volSeqName;
  if (!volSeqName.ParseName(itemName))
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Cannot detect the first split file"));
    return;
  }

  // Enumerate the following parts (PanelSplitFile.cpp:458-479).
  FStringVector names;
  UInt64 totalSize = 0;
  {
    UString nextName = itemName;
    for (;;)
    {
      NFile::NFind::CFileInfo fileInfo;
      if (!fileInfo.Find(us2fs(srcPath + nextName)) || fileInfo.IsDir())
        break;
      names.Add(us2fs(nextName));
      totalSize += fileInfo.Size;
      nextName = volSeqName.GetNextName();
    }
  }
  if (names.Size() == 1)
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Cannot find more than one part of the split file"));
    return;
  }
  if (totalSize == 0)
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"), QStringLiteral("No data"));
    return;
  }

  // Output dir: override / other panel / src.
  UString path = srcPath;
  if (otherPanel() && !otherPanel()->isInArchive())
  {
    const UString od = otherPanel()->currentFsDirPath();
    if (!od.IsEmpty())
      path = od;
  }
  if (!destOverride.IsEmpty())
    path = destOverride;

  NWindows::NFile::NName::NormalizeDirPathPrefix(path);
  if (!NFile::NDir::CreateComplexDir(us2fs(path)))
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"), QStringLiteral("Cannot create folder"));
    return;
  }

  // Output filename = UnchangedPart, trailing dots stripped (PanelSplitFile.cpp:518-526).
  UString outName = volSeqName.UnchangedPart;
  while (!outName.IsEmpty())
  {
    if (outName.Back() != L'.')
      break;
    outName.DeleteBack();
  }
  if (outName.IsEmpty())
    outName = "file";

  const UString destFilePath = path + outName;
  // Overwrite guard (IDS_FILE_EXIST).
  {
    NFile::NFind::CFileInfo fi;
    if (fi.Find(us2fs(destFilePath)))
    {
      if (!_headless)
        QMessageBox::warning(this, QStringLiteral("7-Zip"),
            QStringLiteral("File already exists"));
      else
        fprintf(stderr, "combine: output already exists: %s\n",
            (const char *)GetAnsiString(destFilePath));
      return;
    }
  }

  QtCombineWorker worker;
  worker.InputDirPrefix = us2fs(srcPath);
  worker.Names = names;
  worker.OutputPath = us2fs(destFilePath);
  worker.TotalSize = totalSize;
  worker.DisableUserQuestions = _headless;
  worker.Create(UString(L"Combining"), this);

  if (otherPanel())
    otherPanel()->refresh();
  _focused->refresh();
}

void QtFileManagerWindow::onCombine() { doCombine(); }


// CApp::Link / CLinkDialog::OnButton_Link (LinkDialog.cpp:260-402). On Linux this
// collapses to link(2) (hard) / symlink(2) (symbolic).
void QtFileManagerWindow::doLink(const UString &fromOverride,
    const UString &toOverride, int kindOverride)
{
  if (!_focused || _focused->isInArchive())
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Operation is not supported for archive items"));
    return;
  }

  UString from, to;
  int kind = QtLinkDialog::Symbolic;

  if (kindOverride >= 0 || !fromOverride.IsEmpty() || !toOverride.IsEmpty())
  {
    // Headless / scripted: take the overrides; if `from` is not absolute, prepend
    // the focused panel's dir (CurDirPrefix), mirroring OnButton_Link:267-269.
    from = fromOverride;
    to = toOverride;
    kind = (kindOverride < 0) ? QtLinkDialog::Symbolic : kindOverride;
    if (!from.IsEmpty() && !NWindows::NFile::NName::IsAbsolutePath(from))
      from.Insert(0, _focused->currentFsDirPath());
  }
  else
  {
    // GUI: one FS item required; build the dialog with the OnInit prefill.
    const UString itemName = _focused->firstSelectedName();
    if (itemName.IsEmpty())
    {
      QMessageBox::warning(this, QStringLiteral("7-Zip"), QStringLiteral("Select one file"));
      return;
    }
    const UString fsPrefix = _focused->currentFsDirPath();
    UString other = fsPrefix;
    if (otherPanel() && !otherPanel()->isInArchive())
    {
      const UString od = otherPanel()->currentFsDirPath();
      if (!od.IsEmpty())
        other = od;
    }
    QtLinkDialog dlg(this);
    dlg.CurDirPrefix = fsPrefix;
    dlg.FilePath = fsPrefix + itemName;     // the existing item (target = to)
    dlg.AnotherPath = other + itemName;     // the new link to create (from)
    dlg.fillFromState();
    if (dlg.exec() != QDialog::Accepted)
      return;
    from = dlg.From;
    to = dlg.To;
    kind = dlg.Kind;
  }

  if (from.IsEmpty())
    return;

  bool ok;
  if (kind == QtLinkDialog::Hard)
  {
    // HARD : MyCreateHardLink(from, to) (LinkDialog.cpp:301) -> Linux link(to, from).
    ok = NWindows::NFile::NDir::MyCreateHardLink(us2fs(from), us2fs(to));
  }
  else
  {
    // SYMBOLIC : POSIX symlink(target, linkpath) == symlink(to, from).
    const AString toA = GetAnsiString(to);
    const AString fromA = GetAnsiString(from);
    ok = (::symlink(toA.Ptr(), fromA.Ptr()) == 0);
  }

  if (!ok)
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Cannot create link: %1").arg(QString::fromLocal8Bit(strerror(errno))));
    else
      fprintf(stderr, "link: failed: %s\n", strerror(errno));
    return;
  }

  if (_headless)
    printf("LINK_OK: kind=%s from=%s to=%s\n",
        kind == QtLinkDialog::Hard ? "hard" : "sym",
        (const char *)GetAnsiString(from), (const char *)GetAnsiString(to));

  _focused->refresh();
}

void QtFileManagerWindow::onLink() { doLink(); }

// LINK-TARGET test hook : build the dialog with the OnInit prefill for the focused
// panel's item `name` and return the read-back current symlink target (the GUI dialog
// itself needs a live event loop, so the headless path drives fillFromState() directly).
UString QtFileManagerWindow::linkTargetForTest(QtPanel *panel, const UString &name)
{
  if (!panel)
    return UString();
  setFocusedPanel(panel);
  const UString fsPrefix = panel->currentFsDirPath();
  QtLinkDialog dlg(this);
  dlg.CurDirPrefix = fsPrefix;
  dlg.FilePath = fsPrefix + name;     // the existing focused item
  dlg.AnotherPath = fsPrefix + name;
  dlg.fillFromState();
  return dlg.CurrentTarget;
}


// CApp::DiffFiles (PanelItemOpen.cpp:747-814). Pick path1/path2, read the
// [Tools] DiffCommand, launch via QProcess.
void QtFileManagerWindow::doDiff()
{
  if (!_focused || _focused->isInArchive())
  {
    if (!_headless)
      QMessageBox::warning(this, QStringLiteral("7-Zip"),
          QStringLiteral("Operation is not supported for archive items"));
    return;
  }

  const QVector<int> rows = _focused->selectedSourceRows();
  UString path1, path2;

  if (rows.size() == 2)
  {
    // Two selected in the focused panel.
    const UStringVector full = _focused->selectedFullPaths();
    if (full.Size() != 2)
      return;
    path1 = full[0];
    path2 = full[1];
  }
  else if (rows.size() == 1 && otherPanel())
  {
    // One selected + the other panel (must be FS).
    if (otherPanel()->isInArchive())
      return;
    const UStringVector full = _focused->selectedFullPaths();
    if (full.Size() != 1)
      return;
    path1 = full[0];
    const QVector<int> oRows = otherPanel()->selectedSourceRows();
    if (oRows.size() == 1)
    {
      const UStringVector oFull = otherPanel()->selectedFullPaths();
      if (oFull.Size() == 1)
        path2 = oFull[0];
    }
    if (path2.IsEmpty())
    {
      // Same-named file in the other panel's dir.
      const UString name = _focused->model()->itemName(rows.first());
      path2 = otherPanel()->currentFsDirPath() + name;
    }
  }
  else
    return;

  const UString cmd = QtFavorites::GetDiffCommand();
  if (cmd.IsEmpty())
    return;

  if (_headless)
  {
    printf("DIFF_CMD: %s %s %s\n",
        (const char *)GetAnsiString(cmd),
        (const char *)GetAnsiString(path1),
        (const char *)GetAnsiString(path2));
    return;
  }

  QStringList args;
  args << QString::fromWCharArray(path1.Ptr(), (int)path1.Len());
  args << QString::fromWCharArray(path2.Ptr(), (int)path2.Len());
  const QString program = QString::fromWCharArray(cmd.Ptr(), (int)cmd.Len());
  if (!QProcess::startDetached(program, args))
    QMessageBox::warning(this, QStringLiteral("7-Zip"),
        QStringLiteral("Cannot start the diff tool"));
}

void QtFileManagerWindow::onDiff() { doDiff(); }


// === Favorites (CFastFolders + CPanel::SetBookmark/OpenBookmark) =============

void QtFileManagerWindow::favAdd()
{
  // CPanel::SetBookmark : store the focused panel's current folder (its FS dir).
  if (!_focused)
    return;
  const UString dir = _focused->currentFsDirPath();
  if (dir.IsEmpty())
    return;
  QtFavorites::AddNext(dir);
}

void QtFileManagerWindow::onFavAdd() { favAdd(); }

void QtFileManagerWindow::favGo(int slot)
{
  // CPanel::OpenBookmark : navigate the focused panel to the stored path.
  if (!_focused)
    return;
  const UString path = QtFavorites::GetString(slot);
  if (path.IsEmpty())
    return;
  _focused->navigateToFsPath(path);
}

// G.4d : CPanel::FoldersHistory (PanelFolderChange.cpp:866). The CListViewDialog
// analogue — a modal QDialog holding a QListWidget of the app-level recent-folders
// history (most-recent-first). OK / double-click navigates the FOCUSED panel to the
// selected entry (BindToPathAndRefresh); Del removes an entry (DeleteIsAllowed). On
// accept the (possibly edited) list is written back to _folderHistory + persisted,
// mirroring the original's RemoveAll + re-AddString on StringsWereChanged.
void QtFileManagerWindow::onFoldersHistory()
{
  if (!_focused)
    return;

  QDialog dlg(this);
  dlg.setWindowTitle(FmLang(IDS_FOLDERS_HISTORY, QStringLiteral("Folders History")));
  dlg.resize(560, 400);
  QVBoxLayout *lay = new QVBoxLayout(&dlg);

  QListWidget *list = new QListWidget(&dlg);
  FOR_VECTOR (i, _folderHistory)
    list->addItem(UStr_toQ(_folderHistory[i]));
  if (list->count() > 0)
    list->setCurrentRow(0);   // CListViewDialog::SelectFirst
  lay->addWidget(list, 1);

  QDialogButtonBox *box = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  lay->addWidget(box);
  connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  // Double-click an entry == OK on it (CListViewDialog double-click -> IDOK).
  connect(list, &QListWidget::itemActivated, &dlg, &QDialog::accept);

  // Del removes the focused entry (CListViewDialog::DeleteIsAllowed). An event
  // filter on the list catches Delete and drops the current row.
  struct DelFilter : QObject
  {
    QListWidget *L;
    DelFilter(QListWidget *l) : L(l) {}
    bool eventFilter(QObject *o, QEvent *e) override
    {
      if (e->type() == QEvent::KeyPress
          && static_cast<QKeyEvent *>(e)->key() == Qt::Key_Delete)
      {
        const int r = L->currentRow();
        if (r >= 0)
        {
          delete L->takeItem(r);
          return true;
        }
      }
      return QObject::eventFilter(o, e);
    }
  } delFilter(list);
  list->installEventFilter(&delFilter);

  const int code = dlg.exec();

  // Mirror StringsWereChanged : rebuild _folderHistory from whatever rows remain
  // (Del may have removed some), regardless of OK/Cancel — the original rebuilds the
  // store from the dialog's edited Strings on accept; on Cancel it leaves the store.
  // We rebuild only on accept to match "Cancel leaves the list untouched".
  if (code != QDialog::Accepted)
    return;

  UString chosen;
  if (list->currentItem())
    chosen = Q_toU_local(list->currentItem()->text());

  UStringVector rebuilt;
  for (int i = 0; i < list->count(); i++)
    rebuilt.Add(Q_toU_local(list->item(i)->text()));
  _folderHistory = rebuilt;
  QtFmSettings::SaveFolderHistory(_folderHistory);

  // CPanel::FoldersHistory : BindToPathAndRefresh(selectString) on the focused panel.
  if (!chosen.IsEmpty())
    _focused->navigateToFsPath(chosen);
}

// G.4d test hook : the headless equivalent of the dialog's "navigate to the picked
// entry" outcome (the dialog needs a live event loop). Navigate the focused panel to
// history entry `index`; returns false on an out-of-range index.
bool QtFileManagerWindow::historyNavigateForTest(int index)
{
  if (!_focused || index < 0 || (unsigned)index >= _folderHistory.Size())
    return false;
  return _focused->navigateToFsPath(_folderHistory[(unsigned)index]);
}

void QtFileManagerWindow::rebuildFavoritesMenu()
{
  // MyLoadMenu.cpp:508-560 : a dynamic submenu. Top = "Add folder to Favorites as"
  // with 10 "Set bookmark N" items; below, the 10 stored slots navigate on click
  // (text = the stored path elided to 100, '-' if empty).
  if (!_favMenu)
    return;
  _favMenu->clear();

  QMenu *addSub = _favMenu->addMenu(QStringLiteral("&Add folder to Favorites as"));
  for (int i = 0; i < QtFavorites::kNumSlots; i++)
  {
    QAction *a = addSub->addAction(QStringLiteral("Set bookmark %1").arg(i));
    a->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | (Qt::Key_0 + i)));
    connect(a, &QAction::triggered, this, [this, i]() {
      if (_focused)
      {
        const UString dir = _focused->currentFsDirPath();
        if (!dir.IsEmpty())
          QtFavorites::SetString(i, dir);
      }
    });
  }

  _favMenu->addSeparator();

  for (int i = 0; i < QtFavorites::kNumSlots; i++)
  {
    const UString stored = QtFavorites::GetString(i);
    QString label;
    if (stored.IsEmpty())
      label = QStringLiteral("-");
    else
    {
      QString full = QString::fromWCharArray(stored.Ptr(), (int)stored.Len());
      if (full.size() > 100)
        full = full.left(48) + QStringLiteral(" ... ") + full.right(47);
      label = full;
    }
    QAction *a = _favMenu->addAction(label);
    a->setShortcut(QKeySequence(Qt::ALT | (Qt::Key_0 + i)));
    a->setEnabled(!stored.IsEmpty());
    connect(a, &QAction::triggered, this, [this, i]() { favGo(i); });
  }
}
