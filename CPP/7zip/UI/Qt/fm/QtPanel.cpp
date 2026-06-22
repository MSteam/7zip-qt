// QtPanel.cpp
// ----------------------------------------------------------------------------
// See QtPanel.h.
// ----------------------------------------------------------------------------

#include "QtPanel.h"

#include <QtWidgets/QTreeView>
#include <QtWidgets/QListView>
#include <QtWidgets/QAbstractItemView>   // B.8 : applySettings (SelectRows etc.)
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMenu>               // G.4b : header column-chooser popup
#include <QtGui/QAction>
#include <QtCore/QEvent>
#include <QtCore/QTimer>          // G.4a : debounced view-settings save
#include <QtCore/QFileSystemWatcher>  // G.4e : Auto Refresh (FS dir-change watcher)
#include <QtCore/QByteArray>      // G.4a : QHeaderView::saveState() blob
#include <QtCore/QMimeData>
#include <QtCore/QItemSelection>
#include <QtCore/QItemSelectionModel>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>       // G.6d : right-button press/move drag detection
#include <QtWidgets/QApplication>  // G.6d : QApplication::startDragDistance()
#include <QtGui/QDrag>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDragMoveEvent>
#include <QtGui/QDragLeaveEvent>   // G.6c : clear the drop-target highlight on leave
#include <QtGui/QDropEvent>
#include <QtWidgets/QRubberBand>   // marquee selection on the Details tree's empty area

#include "../../../../Common/MyString.h"
#include "../../../../Common/StringConvert.h"
#include "../../../../Common/Wildcard.h"   // B.7b : DoesWildcardMatchName (engine glob)
#include "../../../../Windows/FileDir.h"     // B.5c : RemoveDirWithSubItems / temp helpers
#include "../../../../Windows/FileFind.h"
#include "../../../../Windows/FileName.h"

#include "../../../PropID.h"          // kpidPath
#include "../../../Archive/IArchive.h" // G.2b : IInArchiveGetStream (nested archive sub-stream)
#include "../../FileManager/FSFolder.h"
#include "../../Agent/IFolderArchive.h" // B.5b : IInFolderArchive / BindToRootFolder (re-bind on refresh)

#include "QtArchiveBrowser.h"         // QtFolderSortProxy (folders-first)
#include "ArchiveOpenHelper.h"
#include "QtArchiveOpenWorker.h"      // P.1 : threaded archive-open worker
#include "../QtExtractPrompts.h"      // Encrypted-FM : QtPasswordPrompt::SetParentWidget
#include "QtFsDnd.h"                  // B.4b : mime build / drop dispatch
#include "QtFsOperations.h"           // B.5c : QtFsCopyWorker (extract-to-temp)

using namespace NWindows;

// ---------------------------------------------------------------------------

static QString UStr_toQ(const UString &s)
{
  return QString::fromWCharArray(s.Ptr(), (int)s.Len());
}

static UString Q_toU(const QString &s)
{
  const std::wstring w = s.toStdWString();
  return UString(w.c_str());
}

// === G.3a : DoItemAlwaysStart (document-extension subset) ====================
// Faithful port of CPanel::DoItemAlwaysStart / kStartExtensions (PanelItemOpen.cpp
// :633-669). The original short-circuits the archive probe (tryInternal) for files
// whose extension is in kStartExtensions, opening them with their associated app
// instead — so a ZIP-based document (.docx, .odt, .epub ...) is NOT misread as a
// zip folder. We keep ONLY the DOCUMENT / content subset that is meaningful on
// Linux; the Windows-only EXECUTABLE entries (exe bat cmd com ps1 vbs js wsf msi
// lnk chm scr cpl ...) are intentionally EXCLUDED — they are not launch targets on
// Linux. Every extension below is verbatim from the original kStartExtensions list
// (office / opendoc / ebook / publishing / media-container / source families);
// nothing is invented. The match is the engine's FindExt: case-insensitive,
// space-separated low-case word list.
static const char * const kStartExtensions_Doc =
  // MS Office (legacy + OOXML) — the original's "doc dot xls ..." / "docx ..." runs
  " doc dot xls ppt pps wps wpt wks xlr wdb vsd pub"
  " docx docm dotx dotm xlsx xlsm xltx xltm xlsb xps"
  " xlam pptx pptm potx potm ppam ppsx ppsm vsdx xsn"
  " mpp"
  " msg"
  " dwf"
  // ebooks / opendocument / other ZIP-container documents
  " epub"
  " odt ods"
  " wb3"
  // portable documents / publishing
  " pdf"
  " ps"
  " ";

// FindExt : verbatim mirror of CPanel's FindExt (ContextMenu.cpp:538) — find the
// file's extension as an exact word in a space-separated low-case ASCII list, via
// the engine's CStringFinder (MyString.cpp). NOT in the Qt link set as a symbol
// (PanelItemOpen.cpp/ContextMenu.cpp aren't compiled here), so its 6-line body is
// re-homed; CStringFinder::FindWord_In_LowCaseAsciiList_NoCase IS linked.
static bool FindExt_Doc(const char *p, const UString &name)
{
  const int dotPos = name.ReverseFind_Dot();
  const int len = (int)name.Len() - (dotPos + 1);
  if (len == 0 || len > 32 || dotPos < 0)
    return false;
  CStringFinder finder;
  return finder.FindWord_In_LowCaseAsciiList_NoCase(p, name.Ptr((unsigned)(dotPos + 1)));
}

// DoItemAlwaysStart (PanelItemOpen.cpp:665) restricted to the Linux document subset.
static bool DoItemAlwaysStart_Document(const UString &name)
{
  return FindExt_Doc(kStartExtensions_Doc, name);
}

// === G.3e : IsVirus (name-spoofing guard) ====================================
// Faithful port of the NON-_WIN32 part of CPanel::IsVirus_Message (PanelItemOpen.cpp
// :867-894): a file name carrying a U+202E RLO (right-to-left override) char, or a
// run of 5+ spaces, is a classic spoof (e.g. "photo<RLO>gpj.exe") and is BLOCKED
// from external launch (CPanel checks this before ShellExecute at :955-957 / :1470).
// The original BOTH detects and shows IDS_VIRUS; here the panel DETECTS and hands the
// presentation to the window (which builds the IDS_VIRUS message / prints the headless
// marker), so `outName2` returns the same sanitised string the original messages with
// (RLO -> "[RLO]", each 5-space run -> a single space) and `outIsSpaceError` mirrors
// the original's isSpaceError flag (controls the "(...)" paren strip in the message).
// The _WIN32-only trailing-dot/space-.exe branch (:896-916) is intentionally SKIPPED.
static bool Qt_IsVirusName(const UString &name, UString &outName2, bool &outIsSpaceError)
{
  const wchar_t cRLO = (wchar_t)0x202E;
  bool isVirus = false;
  bool isSpaceError = false;
  UString name2 = name;

  if (name2.Find(cRLO) >= 0)
  {
    const UString badString(cRLO);
    name2.Replace(badString, UString("[RLO]"));
    isVirus = true;
  }
  {
    const wchar_t * const kVirusSpaces = L"     ";   // 5 spaces (original kVirusSpaces)
    for (;;)
    {
      const int pos = name2.Find(kVirusSpaces);
      if (pos < 0)
        break;
      isVirus = true;
      isSpaceError = true;
      name2.Replace(UString(kVirusSpaces), UString(" "));
    }
  }

  outName2 = name2;
  outIsSpaceError = isSpaceError;
  return isVirus;
}

// Build a CFSFolder rooted at a directory (same as main_fm_browser.cpp's
// BindFsFolder): the engine's CFSFolder Init() over a path with a trailing
// separator. Used by the address bar to navigate to an arbitrary FS path.
static HRESULT BindFsFolderPath(const FString &dirPath,
    CMyComPtr<IFolderFolder> &folder)
{
  FString path = dirPath;
  NFile::NName::NormalizeDirPathPrefix(path);
  NFsFolder::CFSFolder *fsSpec = new NFsFolder::CFSFolder;
  CMyComPtr<IFolderFolder> f = fsSpec;
  RINOK(fsSpec->Init(path))
  folder = f;
  return S_OK;
}

// ---------------------------------------------------------------------------
// B.4b / B.7b : the four Qt DnD hooks are identical for both the Details
// QTreeView (QtPanelView) and the icon/list QListView (QtPanelListView): they
// reference only the owning QtPanel's public buildDragMimeData()/acceptsDrop()/
// handleDrop(). Factored into free helpers so both view subclasses share one
// body (the Win32 original wires the SAME OLE IDropSource/IDropTarget on the one
// list control regardless of LVS view style).
// ---------------------------------------------------------------------------

// The panel that started the CURRENT in-process drag (set across QDrag::exec, which
// blocks). The drag handlers forbid dropping back onto this panel — the mirror of
// PositionCursor's "we don't allow to drop to source panel" (PanelDrag.cpp:1956). An
// external (Dolphin) drag leaves this null, so external drops are never forbidden.
static QtPanel *g_dragSourcePanel = nullptr;

static void PanelStartDrag(QWidget *src, QtPanel *panel, Qt::DropActions supportedActions)
{
  QMimeData *mime = panel->buildDragMimeData();
  if (!mime)
    return; // empty selection, or archive extraction produced nothing
  QDrag *drag = new QDrag(src);
  drag->setMimeData(mime);
  // B.5c : an ARCHIVE drag-OUT is COPY/extract-only (move-OUT = extract +
  // delete-from-archive is deferred; the Win32 original never deleted from an
  // archive on drag either — moveIsAllowed = isFSFolder, PanelDrag.cpp:1683).
  // Restrict to CopyAction so a move never silently extracts-without-deleting.
  // For a FS panel, offer BOTH copy and move; Qt resolves the actual action
  // from the modifiers (Ctrl/Shift) + the target, mirroring GetEffect_ForKeys.
  const Qt::DropActions acts = panel->isInArchive()
      ? Qt::CopyAction
      : (supportedActions & (Qt::CopyAction | Qt::MoveAction));
  g_dragSourcePanel = panel;          // forbid drop-back onto the source panel
  drag->exec(acts, Qt::CopyAction);
  g_dragSourcePanel = nullptr;        // exec() blocks until the drag ends
}

// Forbid dropping onto the panel that STARTED this drag (forbidden cursor), the
// mirror of PositionCursor's source-panel guard (PanelDrag.cpp:1956). For a single
// archive panel this forbids the whole window, so an archive drag-OUT can only be
// dropped OUTSIDE the app (or onto the OTHER panel) — never re-added to its own
// archive. dropAllowedFrom() folds that into the existing accepts-drop test.
static bool PanelDropAllowed(QtPanel *panel, const QMimeData *mime)
{
  return panel != g_dragSourcePanel && panel->acceptsDrop()
      && mime && mime->hasUrls();
}

// Drop onto the source panel (or any non-target) is rejected with ev->ignore(), which
// is what makes Qt show the forbidden ("no-drop") cursor over a setAcceptDrops widget
// — the Qt analog of GetEffect returning DROPEFFECT_NONE for the source panel
// (PanelDrag.cpp:1956). (An accept()+IgnoreAction does NOT yield the forbidden cursor
// on this Qt/KDE, so we keep ignore().)
static void PanelDragEnter(QtPanel *panel, QDragEnterEvent *ev)
{
  if (PanelDropAllowed(panel, ev->mimeData()))
    ev->acceptProposedAction();
  else
    ev->ignore();
}

static void PanelDragMove(QAbstractItemView *view, QtPanel *panel, QDragMoveEvent *ev)
{
  if (PanelDropAllowed(panel, ev->mimeData()))
  {
    // G.6c : hit-test the row under the cursor; if it is an FS folder, target THAT
    // sub-folder and highlight it (CDropTarget::PositionCursor, PanelDrag.cpp:1981).
    panel->updateDropTarget(view, ev->position().toPoint());
    ev->acceptProposedAction();
  }
  else
  {
    panel->clearDropTarget();
    ev->ignore();
  }
}

static void PanelDragLeave(QtPanel *panel)
{
  // G.6c : the drag left the view — drop the sub-folder highlight (RemoveSelection).
  panel->clearDropTarget();
}

static void PanelDrop(QAbstractItemView *view, QtPanel *panel, QDropEvent *ev)
{
  if (!PanelDropAllowed(panel, ev->mimeData()))
  {
    panel->clearDropTarget();
    ev->ignore();
    return;
  }
  // G.6c : fix the drop-target sub-folder from the exact drop point (the last
  // dragMove may have been on a different row), then dispatch. The highlight is
  // cleared after the op (the shell refresh()es the dest panel).
  panel->setDropTargetFromDropPos(view, ev->position().toPoint());
  // Qt has folded Ctrl/Shift + supportedActions into dropAction().
  const bool handled = panel->handleDrop(ev->mimeData(), ev->dropAction());
  panel->clearDropTarget();
  if (handled)
    ev->acceptProposedAction();
  else
    ev->ignore();
}

// ---------------------------------------------------------------------------
// B.4b : the panel's Details view — a QTreeView that is a Qt drag SOURCE and a
// uri-list drop TARGET. The Win32 original (PanelDrag.cpp) wires OLE
// IDropSource/IDropTarget on the list control; here Qt's QDrag/QMimeData does
// the cross-process transport (text/uri-list with file:// URIs) and we only
// override the four DnD hooks, forwarding to the owning QtPanel.
class QtPanelView Z7_final : public QTreeView
{
public:
  explicit QtPanelView(QtPanel *panel)
    : QTreeView(panel), _panel(panel) {}

protected:
  void startDrag(Qt::DropActions supportedActions) Z7_override
    { PanelStartDrag(this, _panel, supportedActions); }
  void dragEnterEvent(QDragEnterEvent *ev) Z7_override { PanelDragEnter(_panel, ev); }
  void dragMoveEvent(QDragMoveEvent *ev) Z7_override { PanelDragMove(this, _panel, ev); }
  void dragLeaveEvent(QDragLeaveEvent *) Z7_override { PanelDragLeave(_panel); }
  void dropEvent(QDropEvent *ev) Z7_override { PanelDrop(this, _panel, ev); }

  // Rubber-band (marquee) selection on the tree's EMPTY area. The Win32 report-mode
  // ListView draws a marquee on an empty-space left-drag and selects the rows it
  // sweeps; QTreeView has NO built-in rubber band (unlike QListView, which gets one
  // from setSelectionRectVisible), so we synthesize it. A press ON an item falls
  // through to the base (click-select / start-drag); a press on empty space starts
  // the band, drag resizes it + selects intersecting rows via the protected
  // setSelection(), release ends it.
  void mousePressEvent(QMouseEvent *ev) Z7_override
  {
    if (ev->button() == Qt::LeftButton
        && !indexAt(ev->position().toPoint()).isValid())
    {
      _bandOrigin = ev->position().toPoint();
      _banding = true;
      if (!_band)
        _band = new QRubberBand(QRubberBand::Rectangle, viewport());
      _band->setGeometry(QRect(_bandOrigin, QSize()));
      _band->show();
      clearSelection();   // empty-area press clears, as the base would
      return;
    }
    QTreeView::mousePressEvent(ev);
  }
  void mouseMoveEvent(QMouseEvent *ev) Z7_override
  {
    if (_banding)
    {
      const QRect r = QRect(_bandOrigin, ev->position().toPoint()).normalized();
      _band->setGeometry(r);
      // setSelection (protected) selects the rows whose item rects intersect the
      // viewport rect — exactly the ListView marquee behavior.
      setSelection(r, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      return;
    }
    QTreeView::mouseMoveEvent(ev);
  }
  void mouseReleaseEvent(QMouseEvent *ev) Z7_override
  {
    if (_banding)
    {
      _banding = false;
      if (_band)
        _band->hide();
      return;
    }
    QTreeView::mouseReleaseEvent(ev);
  }

private:
  QtPanel *_panel;
  QRubberBand *_band = nullptr;
  QPoint _bandOrigin;
  bool _banding = false;
};

// ---------------------------------------------------------------------------
// B.7b : the panel's icon/list view — a QListView with the SAME four DnD hooks
// as QtPanelView, used for Large/Small Icons and List modes. It shares the
// panel's _proxy and the QTreeView's QItemSelectionModel (so drag-out, selection
// and selectedSourceRows() work identically in any view mode). The Win32 FM
// reuses the one list control for all four LVS styles; here a QStackedWidget
// swaps a QTreeView (Details) and this QListView (the three icon/list modes).
// ---------------------------------------------------------------------------

class QtPanelListView Z7_final : public QListView
{
public:
  explicit QtPanelListView(QtPanel *panel)
    : QListView(panel), _panel(panel) {}

protected:
  void startDrag(Qt::DropActions supportedActions) Z7_override
    { PanelStartDrag(this, _panel, supportedActions); }
  void dragEnterEvent(QDragEnterEvent *ev) Z7_override { PanelDragEnter(_panel, ev); }
  void dragMoveEvent(QDragMoveEvent *ev) Z7_override { PanelDragMove(this, _panel, ev); }
  void dragLeaveEvent(QDragLeaveEvent *) Z7_override { PanelDragLeave(_panel); }
  void dropEvent(QDropEvent *ev) Z7_override { PanelDrop(this, _panel, ev); }

private:
  QtPanel *_panel;
};

// ---------------------------------------------------------------------------

QtPanel::QtPanel(QWidget *parent)
  : QWidget(parent)
  , _inArchive(false)
  , _isUpdatableArchive(false)
{
  _model = new QtFolderModel(this);
  _proxy = new QtFolderSortProxy(_model, this);

  QVBoxLayout *lay = new QVBoxLayout(this);
  lay->setContentsMargins(2, 2, 2, 2);
  lay->setSpacing(2);

  // --- address row : "Up" button + editable path field -------------------
  QHBoxLayout *addrRow = new QHBoxLayout();
  addrRow->setContentsMargins(0, 0, 0, 0);
  _upButton = new QToolButton(this);
  _upButton->setText(QStringLiteral("Up"));
  _upButton->setToolTip(QStringLiteral("Up One Level (Backspace)"));
  connect(_upButton, &QToolButton::clicked, this, &QtPanel::onUp);
  addrRow->addWidget(_upButton);

  _address = new QLineEdit(this);
  _address->setClearButtonEnabled(true);
  connect(_address, &QLineEdit::returnPressed, this, &QtPanel::onAddressEntered);
  addrRow->addWidget(_address, 1);

  // G.4d : the address-bar dropdown affordance — the Qt mirror of the header
  // combo's drop button (CBN_DROPDOWN). Pops a menu of this path's ancestor
  // folders (indented breadcrumb) plus the app-level recent-folders history.
  _addressDropButton = new QToolButton(this);
  _addressDropButton->setArrowType(Qt::DownArrow);
  _addressDropButton->setToolTip(QStringLiteral("Folders dropdown"));
  connect(_addressDropButton, &QToolButton::clicked, this, &QtPanel::onAddressDropdown);
  addrRow->addWidget(_addressDropButton);
  lay->addLayout(addrRow);

  // --- the items view : multi-selection, folders-first sort --------------
  _view = new QtPanelView(this);
  _view->setModel(_proxy);
  _view->setRootIsDecorated(false);
  _view->setSortingEnabled(true);
  _view->setUniformRowHeights(true);
  _view->setSelectionBehavior(QAbstractItemView::SelectRows);
  _view->setSelectionMode(QAbstractItemView::ExtendedSelection); // multi-select
  _view->setAlternatingRowColors(true);
  _view->sortByColumn(0, Qt::AscendingOrder);
  // G.4l : inline (in-place) rename, the Qt analogue of CPanel's list-view label
  // edit. F2 (EditKeyPressed) + a slow-double-click on the already-selected row
  // (SelectedClicked) begin the edit; we deliberately do NOT enable DoubleClicked,
  // so a fast double-click still ENTERS/OPENS the item (onDoubleClicked) instead of
  // editing it. Enter/Return is intercepted in eventFilter for open before the view
  // sees it, so it never starts an edit either. The model's flags() gates the edit
  // off (no ItemIsEditable) for a read-only folder, mirroring OnBeginLabelEdit.
  _view->setEditTriggers(
      QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
  // B.7b : a header click is the ORIGINAL click-to-sort path (CPanel sorts by the
  // clicked column's PROPID). Clear any arrange-by key override so the proxy
  // reverts to the clicked column's PROPID. sectionClicked fires only on real
  // user clicks, never on programmatic sortByColumn (arrange-by).
  if (_view->header())
  {
    connect(_view->header(), &QHeaderView::sectionClicked, this,
        [this](int){ _proxy->setUnsortedMode(false); _proxy->setSortKey(kpidNoProperty); });
    // G.4b : CPanel::OnRightClick on the list header -> ShowColumnsContextMenu
    // (PanelItems.cpp:1362). A right-click on the header pops the column chooser.
    _view->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_view->header(), &QWidget::customContextMenuRequested,
        this, &QtPanel::onHeaderContextMenu);
    // G.4a : let the user reorder + hide/show columns by dragging the header, the
    // moves CListViewInfo serializes. The persisted state round-trips section order.
    _view->header()->setSectionsMovable(true);
    // G.4a : persist this list-type's header layout on a column resize / reorder /
    // sort-indicator change (CPanel::SaveListViewInfo on a column change), debounced.
    connect(_view->header(), &QHeaderView::sectionResized,
        this, [this](int, int, int){ scheduleSaveViewSettings(); });
    connect(_view->header(), &QHeaderView::sectionMoved,
        this, [this](int, int, int){ scheduleSaveViewSettings(); });
    connect(_view->header(), &QHeaderView::sortIndicatorChanged,
        this, [this](int, Qt::SortOrder){ scheduleSaveViewSettings(); });
  }

  // G.4a : the debounce timer that coalesces a header drag's burst of
  // sectionResized signals into one QSettings write.
  _saveViewTimer = new QTimer(this);
  _saveViewTimer->setSingleShot(true);
  _saveViewTimer->setInterval(400);
  connect(_saveViewTimer, &QTimer::timeout, this, &QtPanel::saveViewSettingsNow);
  // B.4b : Qt-native drag & drop (text/uri-list). DragDrop = both source & sink;
  // Qt's default drop indicator is fine.
  _view->setDragEnabled(true);
  _view->setAcceptDrops(true);
  _view->setDropIndicatorShown(true);
  _view->setDragDropMode(QAbstractItemView::DragDrop);
  _view->setDefaultDropAction(Qt::CopyAction);
  _view->installEventFilter(this);
  _view->viewport()->installEventFilter(this);
  // CPanel::OnContextMenu (PanelMenu.cpp:1081): right-click in the list builds
  // the per-file menu. Qt routes the right-click to customContextMenuRequested
  // with a viewport-local point; onCustomContextMenu focuses + selects, then
  // re-emits to the shell which assembles the actual QMenu.
  _view->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(_view, &QWidget::customContextMenuRequested,
      this, &QtPanel::onCustomContextMenu);

  // --- B.7b : the icon/list view (Large/Small Icons + List) ---------------
  // Shares the SAME _proxy and (below, via rebindSelectionModel) the SAME
  // QItemSelectionModel as the tree, so selection/current stay consistent across
  // view-mode switches and selectedSourceRows() is correct in any mode.
  _listView = new QtPanelListView(this);
  _listView->setModel(_proxy);
  _listView->setModelColumn(0);               // Name+icon column (icon views show one)
  _listView->setSelectionBehavior(QAbstractItemView::SelectRows);
  _listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  // Native marquee on empty-space drag (QListView supports the rubber band that
  // QTreeView lacks); mirrors the Win32 ListView's selection rectangle.
  _listView->setSelectionRectVisible(true);
  _listView->setMovement(QListView::Static);
  _listView->setResizeMode(QListView::Adjust);
  _listView->setUniformItemSizes(true);
  _listView->setWordWrap(true);
  // G.4l : same inline-rename triggers as the tree (F2 + slow-double-click), and
  // again NOT DoubleClicked so a fast double-click opens/enters via onDoubleClicked.
  _listView->setEditTriggers(
      QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
  // Drag & drop identical to the tree (B.4b uri-list source + sink).
  _listView->setDragEnabled(true);
  _listView->setAcceptDrops(true);
  _listView->setDropIndicatorShown(true);
  _listView->setDragDropMode(QAbstractItemView::DragDrop);
  _listView->setDefaultDropAction(Qt::CopyAction);
  _listView->installEventFilter(this);
  _listView->viewport()->installEventFilter(this);
  _listView->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(_listView, &QWidget::customContextMenuRequested,
      this, &QtPanel::onCustomContextMenu);
  connect(_listView, &QListView::doubleClicked,
      this, &QtPanel::onDoubleClicked);
  // Mirror of CPanel: a mouse double-click in the DEFAULT Details (tree) view must
  // enter/open the item exactly as it does in the icon/list view. Without this the
  // tree's mouse double-click was dead (only the Enter-key eventFilter path fired).
  connect(_view, &QAbstractItemView::doubleClicked,
      this, &QtPanel::onDoubleClicked);

  // Stack the two views; Details (index 0) is the default visible widget.
  _viewStack = new QStackedWidget(this);
  _viewStack->addWidget(_view);        // index 0 = Details (QTreeView)
  _viewStack->addWidget(_listView);    // index 1 = icon/list (QListView)
  lay->addWidget(_viewStack, 1);

  connect(_model, &QtFolderModel::leafActivated,
      this, &QtPanel::onLeafActivated);
  // G.4f : a double-click / Enter on the synthetic ".." row activates the parent
  // navigation. The model emits parentActivated (it does not bind a child); the
  // panel runs its full onUp() — which also exits an archive root via the
  // parent-stack, where the model's own goParent() returns false. Mirrors CPanel
  // mapping a kParentIndex activation to OpenParentFolder.
  connect(_model, &QtFolderModel::parentActivated,
      this, &QtPanel::onUp);
  connect(_model, &QAbstractItemModel::modelReset,
      this, &QtPanel::onModelReset);
  // G.4l : after an inline rename committed by the model's setData(), re-list the
  // folder. For an ARCHIVE the engine did Close()+ReOpen() inside Rename(), so the
  // CAgentFolder reset to root and any cached child indices are stale — refresh()
  // re-binds from the reopened agent root (the B.5b refresh() path). For a plain
  // FS folder it re-scans disk. (The rename ran synchronously on the GUI thread,
  // so there is no worker-thread race here — only the re-bind is needed.)
  connect(_model, &QtFolderModel::itemRenamed,
      this, &QtPanel::onItemRenamed);
  // G.4b : whenever the model rebuilds its columns (folder change) or a column is
  // toggled, re-apply the hidden sections to the header so the view matches the
  // model's IsVisible flags.
  connect(_model, &QtFolderModel::columnVisibilityChanged,
      this, &QtPanel::applyColumnVisibility);

  // Share the tree's selection model with the list view + hook selectionChanged.
  rebindSelectionModel();
}

// B.7b : QTreeView recreates its QItemSelectionModel whenever the source model
// is reset (setRootFolder/enterItem/goParent all begin/endResetModel). Re-share
// it with the QListView and re-connect the single selectionChanged so the two
// views never desync and selectedSourceRows() always reads the live model.
void QtPanel::rebindSelectionModel()
{
  QItemSelectionModel *sm = _view->selectionModel();
  if (_listView->selectionModel() != sm)
    _listView->setSelectionModel(sm);
  // Avoid stacking duplicate connections across resets.
  disconnect(sm, &QItemSelectionModel::selectionChanged,
      this, &QtPanel::onViewSelectionChanged);
  connect(sm, &QItemSelectionModel::selectionChanged,
      this, &QtPanel::onViewSelectionChanged);
  // G.4i : the status bar's focused-item size/date track the FOCUS rectangle
  // (currentIndex), which can move without the selection changing (e.g.
  // Ctrl+Arrow). Mirror CPanel calling Refresh_StatusBar on a focus change by
  // re-emitting selectionChanged on currentRowChanged so the shell recomputes.
  disconnect(sm, &QItemSelectionModel::currentRowChanged,
      this, &QtPanel::onViewSelectionChanged);
  connect(sm, &QItemSelectionModel::currentRowChanged,
      this, &QtPanel::onViewSelectionChanged);
}

void QtPanel::setRootFolder(IFolderFolder *rootFolder, bool isFsRoot)
{
  _inArchive = !isFsRoot;
  _isUpdatableArchive = false;
  // G.2b : a fresh root resets the whole archive parent chain (the Win32 FM rebuilds
  // _parentFolders from empty on a top-level OpenFolder too).
  _parentStack.clear();
  // _model->setRootFolder() emits modelReset -> onModelReset ->
  // restoreViewSettingsForType(), which restores the persisted header geometry for
  // this list-type when one is stored (and updates the section-0 width). Only fall
  // back to the hard-coded default width when NO state was restored (a first run, or
  // persistence disabled), so a restored layout is not clobbered.
  _model->setRootFolder(rootFolder);
  if (_view->header()->count() > 0
      && QtFmSettings::ReadListViewState(_currentTypeKey).isEmpty())
    _view->header()->resizeSection(0, 280);
  applyColumnVisibility();  // G.4b : hide the default-hidden FS columns on the new header
  rebindSelectionModel();   // B.7b : keep the list view sharing the live model
  refreshAddress();
}

// --- queries ----------------------------------------------------------------

bool QtPanel::isEmptySelection() const
{
  return _view->selectionModel()->selectedRows().isEmpty();
}

QVector<int> QtPanel::selectedSourceRows() const
{
  // G.4f : returns SOURCE-MODEL rows (what every consumer treats as a row and feeds
  // to the model's MODEL-row-aware accessors). The synthetic ".." pseudo-row is
  // NEVER an operation target — it is excluded here, mirroring CPanel never adding
  // kParentIndex to an operation's index list (PanelItems.cpp:442-443 etc.). Engine
  // operations translate each returned row to an engine realIndex via the model's
  // modelRowToRealIndex() at the worker/op boundary.
  QVector<int> rows;
  const QModelIndexList sel = _view->selectionModel()->selectedRows();
  rows.reserve(sel.size());
  for (const QModelIndex &proxyIdx : sel)
  {
    const int srcRow = _proxy->mapToSource(proxyIdx).row();
    if (_model->isParentRow(srcRow))
      continue;   // ".." is excluded from selection/operation counts
    rows.push_back(srcRow);
  }
  return rows;
}

int QtPanel::focusedSourceRow() const
{
  // G.4i : the view's current (focused) index, mapped to a source row. The
  // selection model is shared across the Details tree and the icon/list view, so
  // _view->currentIndex() is the focused index regardless of which view is shown.
  // G.4f : the ".." pseudo-row is never a focused OPERATION target — return -1 so
  // the status bar / operation focus paths skip it (CPanel's realIndex ==
  // kParentIndex guards, PanelItems.cpp:389/443/975).
  const QModelIndex cur = _view->currentIndex();
  if (!cur.isValid())
    return -1;
  const int srcRow = _proxy->mapToSource(cur).row();
  if (_model->isParentRow(srcRow))
    return -1;
  return srcRow;
}

// G.4k : the OnTab target — focus this panel's currently-visible item view
// (CPanelCallbackImp::OnTab -> Panels[other].SetFocusToList, App.cpp:45). Emitting
// focused(this) keeps the shell's _lastFocusedPanel tracking in step with the
// keyboard focus move, exactly as the original SetFocusToList path triggers the
// panel-focused notification.
void QtPanel::focusActiveView()
{
  QAbstractItemView *v = activeView();
  if (v)
  {
    v->setFocus(Qt::OtherFocusReason);
    emit focused(this);
  }
}

// G.4k : the SetFocusToPath target — focus the path/address field and select its
// text (CPanelCallbackImp::SetFocusToPath -> _headerComboBox.SetFocus() +
// ShowDropDown(), App.cpp:55-56). We have no history dropdown to show; selecting
// the text is the closest faithful analogue (ready for the user to type a path).
void QtPanel::focusAddressField()
{
  if (_address)
  {
    _address->setFocus(Qt::OtherFocusReason);
    _address->selectAll();
  }
  emit focused(this);
}

// G.4k : the focused item's name when it IS a real folder (for OnSetSubFolder's
// descend-into-focused-sub-folder). CApp::OnSetSubFolder reads srcPanel's focused
// item and only proceeds when IsItem_Folder(realIndex) (App.cpp:874-878). G.4f : the
// synthetic ".." row is handled by the kParentIndex/ascend branch (focusedRowIsParentUp
// below), so focusedSourceRow() returns -1 for it and this returns empty for ".." —
// it never reports ".." as a descend target.
UString QtPanel::focusedSubFolderName() const
{
  const int row = focusedSourceRow();
  if (row < 0)
    return UString();
  if (!_model->isFolder(row))
    return UString();
  return _model->itemName(row);
}

// G.4k / G.4f : true when the FOCUSED list row is the synthetic ".." up-entry — the
// CApp::OnSetSubFolder realIndex == kParentIndex branch (ascend into the OTHER panel).
// With ShowDots off there is no ".." row, so this stays false (the prior behaviour).
bool QtPanel::focusedRowIsParentUp() const
{
  // G.4f : the view's current (focused) index, mapped to a source-model row. When it
  // is the ".." pseudo-row this is true (the OnSetSubFolder ascend branch). The shared
  // selection model makes _view->currentIndex() the focus on either view.
  const QModelIndex cur = _view->currentIndex();
  if (!cur.isValid())
    return false;
  return _model->isParentRow(_proxy->mapToSource(cur).row());
}

UStringVector QtPanel::selectedFullPaths() const
{
  UStringVector out;
  if (_inArchive)
    return out; // real-file paths only meaningful in the FS view
  const UString dir = _model->currentPath(); // ends with a separator
  const QVector<int> rows = selectedSourceRows();
  for (int row : rows)
  {
    const UString name = _model->itemName(row);
    if (!name.IsEmpty())
      out.Add(dir + name);
  }
  return out;
}

UString QtPanel::currentFsDirPath() const
{
  if (_inArchive)
    return UString();
  return _model->currentPath();
}

// G.6a : the full folder prefix (address-bar text), the Qt mirror of
// CPanel::LoadFullPath (PanelFolderChange.cpp:356-369): walk the parent-stack frames
// (each archive level contributes its host path + a separator — the bottom frame's
// FS archive path, then each nested sub-archive name), then append the current
// in-archive path (GetFolderPath(_folder) == currentPath()). On the filesystem the
// stack is empty, so this is just currentPath().
UString QtPanel::currentFolderPrefix() const
{
  if (!_inArchive || _parentStack.isEmpty())
    return currentPath();   // FS: GetFolderPath(_folder)
  UString prefix;
  for (const CParentFrame &frame : _parentStack)
  {
    prefix += frame.hostPath;
    prefix.Add_PathSepar();   // PanelFolderChange.cpp:365
  }
  prefix += currentPath();    // the in-archive path within the top level
  return prefix;
}

// G.6c : the drop destination, honoring a sub-folder under the cursor — the Qt
// mirror of CDropTarget::GetTargetPath (PanelDrag.cpp:2186): GetFsPath() plus
// m_DropHighlighted_SubFolderName (with a trailing separator) when it is set.
UString QtPanel::dropTargetFsDirPath() const
{
  UString path = currentFsDirPath();           // empty inside an archive
  if (path.IsEmpty())
    return path;
  if (!_dropTargetSubFolder.IsEmpty())
  {
    path += _dropTargetSubFolder;              // GetFsPath() ends with a separator
    path.Add_PathSepar();                      // PanelDrag.cpp:2195
  }
  return path;
}

// G.6c : resolve the SOURCE row under a viewport hit and gate it to an FS folder,
// mirroring CDropTarget::PositionCursor's hit-test (PanelDrag.cpp:1981-1996):
//   * only an FS panel targets a sub-folder (IsFsOrPureDrivesFolder — an archive
//     panel keeps the current dir, since add-into-a-subfolder there is unported);
//   * skip the ".." parent row (kParentIndex);
//   * the row must be a FOLDER (IsItem_Folder).
// When `highlight`, set the drag-over highlight on that proxy row (LVIS_DROPHILITED
// analog: select it for the visual cue) and clear any previous highlight first.
int QtPanel::resolveDropTargetRow(QAbstractItemView *view, const QPoint &viewportPos)
{
  if (!view)
    return -1;
  // PanelDrag.cpp:1981 : sub-folder targeting is FS-only (archive add-into-subfolder
  // is not ported; an updatable archive drop still lands at its current level).
  if (_inArchive)
    return -1;
  const QModelIndex proxyIdx = view->indexAt(viewportPos);
  if (!proxyIdx.isValid())
    return -1;
  const QModelIndex srcIdx = _proxy->mapToSource(proxyIdx);
  if (!srcIdx.isValid())
    return -1;
  const int sourceRow = srcIdx.row();
  // Skip the synthetic ".." parent row (kParentIndex) and non-folders (IsItem_Folder).
  if (_model->isParentRow(sourceRow))
    return -1;
  if (!_model->isFolder(sourceRow))
    return -1;
  return sourceRow;
}

void QtPanel::updateDropTarget(QAbstractItemView *view, const QPoint &viewportPos)
{
  const int sourceRow = resolveDropTargetRow(view, viewportPos);
  // Clear the previous highlight first (CDropTarget::RemoveSelection at the top of
  // PositionCursor, PanelDrag.cpp:1929).
  clearDropTarget();
  if (sourceRow < 0)
    return;
  _dropTargetSubFolder = _model->itemName(sourceRow);
  // LVIS_DROPHILITED analog : highlight the folder row under the cursor. We use the
  // current-index outline (a non-destructive visual cue that does not disturb the
  // real drag-source selection set, which KillSelection clears on success).
  const QModelIndex src = _model->index(sourceRow, 0, QModelIndex());
  const QModelIndex proxyIdx = _proxy->mapFromSource(src);
  if (proxyIdx.isValid())
  {
    if (view->selectionModel())
      view->selectionModel()->setCurrentIndex(proxyIdx,
          QItemSelectionModel::Current | QItemSelectionModel::Rows);
    _dropHighlightProxyRow = proxyIdx.row();
  }
}

void QtPanel::setDropTargetFromDropPos(QAbstractItemView *view, const QPoint &viewportPos)
{
  const int sourceRow = resolveDropTargetRow(view, viewportPos);
  if (sourceRow < 0)
  {
    _dropTargetSubFolder.Empty();
    return;
  }
  _dropTargetSubFolder = _model->itemName(sourceRow);
}

void QtPanel::clearDropTarget()
{
  _dropTargetSubFolder.Empty();
  _dropHighlightProxyRow = -1;
}

bool QtPanel::setDropTargetRowForTest(int sourceRow)
{
  clearDropTarget();
  if (_inArchive)
    return false;
  if (sourceRow < 0 || _model->isParentRow(sourceRow) || !_model->isFolder(sourceRow))
    return false;
  _dropTargetSubFolder = _model->itemName(sourceRow);
  return true;
}

void QtPanel::killSelection()
{
  if (QItemSelectionModel *sm = _view->selectionModel())
    sm->clearSelection();
}

UString QtPanel::firstSelectedName() const
{
  const QVector<int> rows = selectedSourceRows();
  if (rows.isEmpty())
    return UString();
  return _model->itemName(rows.first());
}

bool QtPanel::checkSelectionForVirus()
{
  // G.3e : the SAME Qt_IsVirusName guard onLeafActivated runs, applied to every
  // currently-selected item before a menu-driven external launch (doOpen). Faithful
  // to CPanel::OpenItem/OpenItemInArchive calling IsVirus_Message at entry for any
  // tryExternal open: the first offending name blocks the whole launch (the original
  // returns from OpenItem on the first IsVirus_Message hit too).
  const QVector<int> rows = selectedSourceRows();
  for (int r : rows)
  {
    const UString name = _model->itemName(r);
    UString sanitised;
    bool isSpaceError = false;
    if (Qt_IsVirusName(name, sanitised, isSpaceError))
    {
      emit openBlockedAsVirus(this, UStr_toQ(name), UStr_toQ(sanitised), isSpaceError);
      return true;
    }
  }
  return false;
}

CMyComPtr<IUnknown> QtPanel::topAgent() const
{
  // G.2b : the agent owning the archive level currently shown = the TOP frame's
  // agentHolder. Outer (lower) frames' agents stay alive too (they back the
  // sub-streams), but the visible folder belongs to the topmost agent.
  if (_inArchive && !_parentStack.isEmpty())
    return _parentStack.last().agentHolder;
  return CMyComPtr<IUnknown>();
}

void QtPanel::refresh()
{
  // B.5b : after an in-place archive op, CommonUpdateOperation did Close()+ReOpen()
  // on the SAME CAgentFolder, resetting it to _proxyDirIndex = root and clearing
  // _items; any cached child IFolderFolder*/indices are stale. Re-bind from the
  // (reopened-in-place) agent's ROOT folder — which matches the engine state
  // exactly (it reset to root after the op). For a plain FS panel, re-binding the
  // same CFSFolder re-scans disk (CPanel::RefreshListCtrl), which is correct.
  // G.2b : the agent re-bound is the TOP-of-stack agent (the level being shown).
  if (CMyComPtr<IUnknown> holder = topAgent())
  {
    CMyComPtr<IInFolderArchive> agent;
    holder.QueryInterface(IID_IInFolderArchive, &agent);
    if (agent)
    {
      CMyComPtr<IFolderFolder> root;
      if (agent->BindToRootFolder(&root) == S_OK && root)
      {
        _model->setRootFolder(root);
        refreshAddress();
        return;
      }
    }
  }

  // Re-bind the same folder to force a re-list (CPanel::RefreshListCtrl).
  CMyComPtr<IFolderFolder> cur = _model->currentFolder();
  if (cur)
    _model->setRootFolder(cur);
}

// G.4e : Auto Refresh. ------------------------------------------------------
// CPanel::AutoRefresh_Mode + OnTimer (PanelItems.cpp:1435): when ON, the Win32 FM
// polls IFolderWasChanged each timer tick and re-lists on a change. Linux has no
// such poll-and-flag interface; the faithful analogue is an inotify watch on the
// current directory (QFileSystemWatcher), which pushes a directoryChanged signal
// instead of being polled. setAutoRefresh() just flips the flag and (re)points the
// watcher; updateWatcher() does the actual FS-folder-only watch.
void QtPanel::setAutoRefresh(bool on)
{
  if (_autoRefresh == on)
    return;
  _autoRefresh = on;
  updateWatcher();
}

void QtPanel::updateWatcher()
{
  // The directory we SHOULD be watching: the current FS path when auto-refresh is
  // on and we are NOT inside an archive (an archive folder is static — the original's
  // IFolderWasChanged is a no-op there, so we watch nothing). Empty otherwise.
  QString want;
  if (_autoRefresh && !_inArchive)
  {
    const UString fsDir = currentFsDirPath();   // empty inside an archive
    if (!fsDir.IsEmpty())
      want = UStr_toQ(fsDir);
  }

  if (want == _watchedDir)
    return;   // already watching the right thing (or nothing)

  if (!_fsWatcher)
  {
    if (want.isEmpty())
      return;   // nothing to watch yet, and no watcher needed — stay lazy
    _fsWatcher = new QFileSystemWatcher(this);
    connect(_fsWatcher, &QFileSystemWatcher::directoryChanged,
        this, &QtPanel::onDirectoryChanged);
  }

  // Drop the previously-watched directory (if any), then add the new one.
  if (!_watchedDir.isEmpty())
    _fsWatcher->removePath(_watchedDir);
  _watchedDir.clear();
  if (!want.isEmpty() && _fsWatcher->addPath(want))
    _watchedDir = want;
}

// G.4e : the watched directory changed on disk (the inotify event). Re-list the
// panel — the Qt mirror of CPanel::OnTimer detecting a change and calling
// OnReload(true) / RefreshListCtrl_SaveFocused. We preserve the FOCUSED row and the
// SELECTION by name across the re-list (the original RefreshListCtrl_SaveFocused
// restores the focused item; we additionally restore the multi-selection where the
// same names still exist — items that vanished simply drop out, exactly as a re-list
// would leave them).
void QtPanel::onDirectoryChanged(const QString & /*path*/)
{
  // Guard: a directory we are no longer showing should not re-list this panel. If we
  // navigated away (or into an archive) between the inotify event and its delivery,
  // updateWatcher() has already moved the watch; ignore a stale signal for safety.
  if (!_autoRefresh || _inArchive)
    return;

  // Capture the focused row's name + every selected row's name BEFORE the re-list,
  // so the focus rectangle and selection can be restored to the same items.
  const UString focusName = _model->itemName(focusedSourceRow());
  UStringVector selNames;
  {
    const QVector<int> rows = selectedSourceRows();
    selNames.ClearAndReserve((unsigned)rows.size());
    for (int r : rows)
      selNames.Add(_model->itemName(r));
  }

  refresh();   // re-list the FS folder (model reset)

  // Restore the multi-selection by name (rows whose names survived the change).
  QItemSelectionModel *sm = _view->selectionModel();
  if (sm)
  {
    sm->clearSelection();
    FOR_VECTOR (i, selNames)
    {
      const int srcRow = _model->rowForName(selNames[i]);
      if (srcRow < 0)
        continue;
      const QModelIndex src = _model->index(srcRow, 0, QModelIndex());
      const QModelIndex proxyIdx = _proxy->mapFromSource(src);
      if (proxyIdx.isValid())
        sm->select(proxyIdx,
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
  }
  // Restore the focus rectangle (and scroll it into view) without disturbing the
  // selection we just rebuilt — focusRowByName(..., false) only moves the current
  // index. No-op when the focused item is gone or nothing was focused.
  if (!focusName.IsEmpty())
    focusRowByName(focusName, false);

  emit selectionChanged(this);
}

// G.4c : Flat View. ----------------------------------------------------------

bool QtPanel::flatMode() const
{
  return _model->isFlatMode();
}

bool QtPanel::flatModeSupported() const
{
  return _model->flatModeSupported();
}

// CPanel::ChangeFlatMode (Panel.cpp:894-902): flip the flag, store it as THIS
// context's default (arc vs disk), then RefreshListCtrl_SaveFocused re-lists with
// SetFlatMode applied. The Qt model's setFlatMode() does the reset+re-list; we just
// own the per-context default and the focused-row save/restore around it.
void QtPanel::toggleFlatMode()
{
  if (!_model->flatModeSupported())
    return;   // faithful: nothing to toggle on a folder that can't go flat
  const bool on = !_model->isFlatMode();
  // RefreshListCtrl_SaveFocused : remember the focused item and restore it after the
  // re-list (here the kpidName of the focused row; flat<->non-flat keeps the same
  // leaf names, so the focus lands back on the same item when it is still listed).
  const UString focusName = _model->itemName(focusedSourceRow());

  if (!_model->setFlatMode(on))
    return;
  // CPanel::ChangeFlatMode stores into _flatModeForArc / _flatModeForDisk per
  // !_parentFolders.IsEmpty() (an archive level is open) — _inArchive is our mirror.
  _flatMode = on;
  if (_inArchive)
    _flatModeForArc = on;
  else
    _flatModeForDisk = on;

  // Re-select+scroll the focused item back into view (RefreshListCtrl_SaveFocused).
  if (!focusName.IsEmpty())
    focusRowByName(focusName, true);
  emit selectionChanged(this);
}

// On a context transition, re-seed the live flat flag from this context's default
// and push it onto the just-bound folder. Mirrors PanelItemOpen.cpp:508
// (_flatMode = _flatModeForArc) and PanelFolderChange.cpp:1013 (_flatMode =
// _flatModeForDisk). The model's setFlatMode() is idempotent, so this only re-lists
// when the bound folder's flat state actually differs from the wanted default.
void QtPanel::applyContextFlatMode()
{
  const bool want = _inArchive ? _flatModeForArc : _flatModeForDisk;
  _flatMode = want;
  _model->setFlatMode(want);
}

// B.8 : the Qt analogue of g_App.SetListSettings() (OptionsDialog.cpp:85). Push
// the Options view tweaks onto BOTH item views so a view-mode switch keeps them.
void QtPanel::applySettings(const QtFmSettings::CInfo &s)
{
  QAbstractItemView *views[2] = { _view, _listView };
  for (QAbstractItemView *v : views)
  {
    if (!v)
      continue;
    // AlternatingRowColors (currently hard-on at the ctor) — direct setter.
    v->setAlternatingRowColors(s.AlternatingColors);
  }

  // FullRow (CFmSettings::FullRow / LVS_EX_FULLROWSELECT). On Windows this flag
  // is PURELY VISUAL — it controls whether the highlight spans the whole row; the
  // selection UNIT is still the row either way. We MUST keep
  // SelectionBehavior::SelectRows unconditionally (the original hard-codes it at
  // the ctor, QtPanel.cpp:214) — switching to SelectItems would break
  // selectedSourceRows()/selectedFullPaths() (selectedRows() only returns rows
  // whose every column is selected), which every FS/archive operation relies on.
  // So FullRow is reflected only as the visual full-row highlight via a
  // stylesheet on the Details tree (the icon/list views are inherently full-item).
  // DATA-SAFETY: this deliberately does NOT touch selection semantics.
  if (_view)
  {
    _view->setSelectionBehavior(QAbstractItemView::SelectRows);
    if (_listView)
      _listView->setSelectionBehavior(QAbstractItemView::SelectRows);
  }

  // ShowGrid (LVS_EX_GRIDLINES, Details view only) + the FullRow visual. QTreeView
  // has no setShowGrid (that is a QTableView method), so emulate the gridlines via
  // a stylesheet on the tree — the faithful B.8 mirror. FullRow visual is the
  // default Qt behaviour (the highlight already spans the row with SelectRows), so
  // there is no extra rule needed; the stylesheet only adds gridlines when asked.
  // Both rules live in ONE setStyleSheet call so neither clobbers the other.
  if (_view)
    _view->setStyleSheet(s.ShowGrid
        ? QStringLiteral("QTreeView::item { border-right: 1px solid palette(mid);"
                         " border-bottom: 1px solid palette(mid); }")
        : QString());

  // SingleClick (CFmSettings::SingleClick). Qt has no per-view single-click
  // toggle, so connect/disconnect a clicked->open wiring. Idempotent: the stored
  // connections are torn down before (possibly) re-wiring.
  if (_singleClick != s.SingleClick || !_treeSingleClickConn)
  {
    QObject::disconnect(_treeSingleClickConn);
    QObject::disconnect(_listSingleClickConn);
    _treeSingleClickConn = QMetaObject::Connection();
    _listSingleClickConn = QMetaObject::Connection();
    if (s.SingleClick)
    {
      if (_view)
        _treeSingleClickConn = connect(_view, &QAbstractItemView::clicked,
            this, &QtPanel::onDoubleClicked);
      if (_listView)
        _listSingleClickConn = connect(_listView, &QAbstractItemView::clicked,
            this, &QtPanel::onDoubleClicked);
    }
    _singleClick = s.SingleClick;
  }

  // ShowDots (a ".." parent pseudo-entry). G.4f : push the Options flag onto the
  // model and re-list LIVE — the ".." row appears/disappears immediately when the
  // user toggles "Show '..' item" (the original re-lists on the same toggle).
  // setShowDots() resets the model (adding/removing ".." when a parent exists);
  // updateHasParentForDots() re-asserts the per-folder gate so ".." is suppressed at
  // the FS filesystem root even with the option on.
  if (_showDots != s.ShowDots)
  {
    _showDots = s.ShowDots;
    _model->setShowDots(_showDots);
    updateHasParentForDots();
  }

  // G.9d : ShowRealFileIcons — push onto the model, which repaints the Name-column
  // icons live (per-format when ON, generic when OFF). The model no-ops if unchanged.
  if (_model)
    _model->setShowRealIcons(s.ShowRealFileIcons);
}

void QtPanel::selectSourceRowForTest(int sourceRow)
{
  QItemSelectionModel *sm = _view->selectionModel();
  sm->clearSelection();
  const QModelIndex src = _model->index(sourceRow, 0, QModelIndex());
  if (!src.isValid())
    return;
  const QModelIndex proxyIdx = _proxy->mapFromSource(src);
  if (!proxyIdx.isValid())
    return;
  sm->select(proxyIdx,
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  _view->setCurrentIndex(proxyIdx);
}

void QtPanel::selectSourceRowsForTest(const QVector<int> &sourceRows)
{
  QItemSelectionModel *sm = _view->selectionModel();
  sm->clearSelection();
  bool first = true;
  for (int sourceRow : sourceRows)
  {
    const QModelIndex src = _model->index(sourceRow, 0, QModelIndex());
    if (!src.isValid())
      continue;
    const QModelIndex proxyIdx = _proxy->mapFromSource(src);
    if (!proxyIdx.isValid())
      continue;
    sm->select(proxyIdx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    if (first)
    {
      _view->setCurrentIndex(proxyIdx);
      first = false;
    }
  }
}

// --- B.7b view modes (CPanel::SetListViewMode, Panel.cpp:871) ----------------

QAbstractItemView *QtPanel::activeView() const
{
  return (_viewMode == ViewMode::Details)
      ? static_cast<QAbstractItemView *>(_view)
      : static_cast<QAbstractItemView *>(_listView);
}

void QtPanel::setViewMode(int mode)
{
  if (mode < 0 || mode > 3)   // SetListViewMode clamps index >= 4 -> return
    return;
  _viewMode = (ViewMode)mode;
  // G.4a : persist the per-panel view mode (CListMode::Save). Idempotent — restoring
  // the stored mode at startup just re-writes the same value. No-op when persistence
  // is disabled (panelIndex < 0).
  if (_panelIndex >= 0)
    QtFmSettings::SaveListMode(_panelIndex, mode);
  if (_viewMode == ViewMode::Details)
  {
    _viewStack->setCurrentWidget(_view);
    return;
  }
  // The three icon/list modes all use the one QListView; only its ViewMode +
  // iconSize/flow differ (the Win32 original swaps only the LVS_TYPEMASK bits:
  // LVS_ICON / LVS_SMALLICON / LVS_LIST).
  _viewStack->setCurrentWidget(_listView);
  switch (_viewMode)
  {
    case ViewMode::LargeIcons:   // LVS_ICON : big icons, label below, wrap grid
      _listView->setViewMode(QListView::IconMode);
      _listView->setIconSize(QSize(48, 48));
      _listView->setGridSize(QSize(110, 80));
      _listView->setFlow(QListView::LeftToRight);
      break;
    case ViewMode::SmallIcons:   // LVS_SMALLICON : small icons, label beside
      _listView->setViewMode(QListView::IconMode);
      _listView->setIconSize(QSize(16, 16));
      _listView->setGridSize(QSize());            // compact
      _listView->setFlow(QListView::LeftToRight);
      break;
    case ViewMode::List:         // LVS_LIST : small icons, column-wrapped list
      _listView->setViewMode(QListView::ListMode);
      _listView->setIconSize(QSize(16, 16));
      _listView->setGridSize(QSize());
      _listView->setFlow(QListView::TopToBottom);
      break;
    default:
      break;
  }
  _listView->setModelColumn(0);
}

// --- B.7b arrange-by (CPanel::SortItemsWithPropID, PanelSort.cpp:256) ---------

void QtPanel::arrangeBy(int propID, bool unsorted)
{
  const PROPID pid = unsorted ? kpidNoProperty : (PROPID)propID;

  // Toggle / default-direction rule, mirroring SortItemsWithPropID exactly:
  // re-choosing the active key flips ascending; a new key is ascending EXCEPT
  // Size/PackSize/CTime/ATime/MTime which default to descending.
  if (pid == _sortID)
    _ascending = !_ascending;
  else
  {
    _sortID = pid;
    _ascending = true;
    switch (pid)
    {
      case kpidSize:
      case kpidPackSize:
      case kpidCTime:
      case kpidATime:
      case kpidMTime:
        _ascending = false;
        break;
      default:
        break;
    }
  }

  const Qt::SortOrder order = _ascending ? Qt::AscendingOrder : Qt::DescendingOrder;

  if (pid == kpidNoProperty)
  {
    // "Unsorted" = raw load order (CompareItems2's kpidNoProperty branch ==
    // MyCompare(lParam1,lParam2)). The proxy's lessThan keeps folders-first and
    // compares the source row when unsorted mode is on.
    _proxy->setUnsortedMode(true);
    _proxy->setSortKey(kpidNoProperty);
    // Drive a re-sort on column 0 so ascending/descending is honored while the
    // per-row key is the source order.
    _view->setSortingEnabled(true);
    _view->sortByColumn(0, order);
    scrollToFocusedRow();   // G.4j : keep the focused item visible after the re-sort
    return;
  }

  // Drive the comparison key EXPLICITLY through the proxy (so Type/Date sort
  // faithfully by extension/mtime even when no such column is visible), then
  // re-sort. The driving column is the key's own column if it exists (keeps the
  // header indicator aligned), else column 0 — the proxy's key override decides
  // the actual order regardless.
  _proxy->setUnsortedMode(false);
  _proxy->setSortKey(pid);
  int col = _model->columnForPropID(pid);
  if (col < 0)
    col = 0;
  _view->setSortingEnabled(true);
  _view->sortByColumn(col, order);
  scrollToFocusedRow();   // G.4j : keep the focused item visible after the re-sort
}

// --- B.7b select / deselect by mask (CPanel::SelectSpec, PanelSelect.cpp:154) -

void QtPanel::selectByMask(const UString &mask, bool selectMode)
{
  QItemSelectionModel *sm = _view->selectionModel();
  const int n = _model->rowCount(QModelIndex());
  const QItemSelectionModel::SelectionFlags flag =
      (selectMode ? QItemSelectionModel::Select : QItemSelectionModel::Deselect)
      | QItemSelectionModel::Rows;
  for (int r = 0; r < n; r++)
  {
    // G.4f : the ".." pseudo-row is never an operation/selection target (CPanel's
    // SelectSpec iterates _selectedStatusVector, the real engine items only).
    if (_model->isParentRow(r))
      continue;
    // FOR_VECTOR(i,_selectedStatusVector): match name only, non-matching rows
    // are left untouched (we only ever call select()/deselect() on matches).
    if (!DoesWildcardMatchName(mask, _model->itemName(r)))
      continue;
    const QModelIndex p = _proxy->mapFromSource(_model->index(r, 0, QModelIndex()));
    if (p.isValid())
      sm->select(p, flag);
  }
}

// --- B.7b select / deselect by type (CPanel::SelectByType, PanelSelect.cpp:169)

void QtPanel::selectByType(bool selectMode)
{
  // The "type" is derived from the focused/current row (GetFocusedItem). The one
  // selection model is shared, so currentIndex() is the same on both views.
  const QModelIndex cur = _view->currentIndex();
  if (!cur.isValid())
    return;                                   // focusedItem < 0 -> no-op
  const int focusRow = _proxy->mapToSource(cur).row();
  if (focusRow < 0)
    return;
  // G.4f : the ".." pseudo-row has no "type" — CPanel's SelectByType guards
  // realIndex == kParentIndex (no-op). Skip it as the type source.
  if (_model->isParentRow(focusRow))
    return;
  const UString name = _model->itemName(focusRow);
  const bool isFolder = _model->isFolder(focusRow);

  // Precompute the file mask exactly as SelectByType: "*" + name.Ptr(pos) where
  // pos is the LAST dot (so report.tar.gz -> "*.gz").
  const int dot = name.ReverseFind_Dot();
  UString mask;
  if (!isFolder && dot >= 0)
  {
    mask = L"*";
    mask += name.Ptr((unsigned)dot);
  }

  QItemSelectionModel *sm = _view->selectionModel();
  const QItemSelectionModel::SelectionFlags flag =
      (selectMode ? QItemSelectionModel::Select : QItemSelectionModel::Deselect)
      | QItemSelectionModel::Rows;
  const int n = _model->rowCount(QModelIndex());
  for (int r = 0; r < n; r++)
  {
    // G.4f : never (de)select the ".." pseudo-row (it is not a real engine item).
    if (_model->isParentRow(r))
      continue;
    // The same-folder-flag guard (IsItem_Folder(i) == isItemFolder) is applied
    // for ALL three branches.
    if (_model->isFolder(r) != isFolder)
      continue;
    bool hit;
    if (isFolder)
      hit = true;                                              // all folders
    else if (dot < 0)
      hit = _model->itemName(r).ReverseFind_Dot() < 0;         // extensionless files
    else
      hit = DoesWildcardMatchName(mask, _model->itemName(r));  // *.<ext>
    if (!hit)
      continue;
    const QModelIndex p = _proxy->mapFromSource(_model->index(r, 0, QModelIndex()));
    if (p.isValid())
      sm->select(p, flag);
  }
}

// --- B.4b drag & drop -------------------------------------------------------

QMimeData *QtPanel::buildDragMimeData()
{
  // B.5c : drag-OUT from an archive panel = EAGER extract-to-temp. The faithful
  // Linux mirror of the Win32 FM's CopyFromPanelTo_Folder (PanelDrag.cpp:630):
  // extract the selected entries into a window-owned temp dir and hand back
  // file:// URIs of the extracted top-level entries. The lazy Windows-XDS
  // deferred-render protocol (CF_HDROP Pre/Final swap on QueryContinueDrag,
  // 7-Zip-private clipboard formats) is the documented deferred item.
  if (_inArchive)
    return extractSelectionToTempMime();
  // FS panel : text/uri-list of the selected items' absolute FS paths.
  const UStringVector paths = selectedFullPaths();
  return QtFsDnd::MakeUriListMime(paths); // nullptr if empty
}

// G.6d : initiate a RIGHT-button drag. Qt's QAbstractItemView::startDrag fires only
// for a left-button drag, so a right (or middle) press-and-drag is driven by hand:
// we build the SAME drag mime the left-button path uses (buildDragMimeData — FS uri
// -list, or archive extract-to-temp), STAMP the right-drag marker so the drop handler
// shows the Copy/Move/Add menu (the Win32 k_SourceFlags_RightButton bit set in
// DragEnter, PanelDrag.cpp:2401-2402/2424), and exec the QDrag. The offered actions
// match the left-button path (an archive drag-OUT stays Copy-only); the actual action
// is chosen from the drop-time menu, not the modifiers.
void QtPanel::startRightButtonDrag()
{
  QMimeData *mime = buildDragMimeData();
  if (!mime)
    return;   // empty selection, or archive extraction produced nothing
  QtFsDnd::MarkRightButtonDrag(mime);
  QDrag *drag = new QDrag(_rbuttonDragView ? (QWidget *)_rbuttonDragView : (QWidget *)this);
  drag->setMimeData(mime);
  // Same offered actions as PanelStartDrag: an archive drag-OUT is Copy-only
  // (move-OUT deferred); an FS panel offers both so the menu's Move entry is live.
  const Qt::DropActions acts = isInArchive()
      ? Qt::CopyAction
      : (Qt::CopyAction | Qt::MoveAction);
  g_dragSourcePanel = this;           // forbid drop-back onto the source panel
  drag->exec(acts, Qt::CopyAction);
  g_dragSourcePanel = nullptr;
}

QMimeData *QtPanel::extractSelectionToTempMime()
{
  // B.5c drag-OUT = extract-to-temp + a text/uri-list of the extracted entries.
  // The extract-to-temp body lives once in extractSelectionToTempPaths() (B.7a
  // factored it out so Open/View/Edit of an archive item can reuse it).
  const UStringVector tempPaths = extractSelectionToTempPaths();
  // text/uri-list of the extracted entries (nullptr if extraction produced
  // nothing => no drag).
  return QtFsDnd::MakeUriListMime(tempPaths);
}

UStringVector QtPanel::extractSelectionToTempPaths()
{
  UStringVector tempPaths;

  // (a) collect the archive item indices (already source-mapped). Empty => no
  // drag, exactly as before.
  const QVector<int> rows = selectedSourceRows();
  if (rows.isEmpty())
    return tempPaths;

  // (b) the archive folder's IFolderOperations (CAgentFolder QIs to it; CopyTo
  // extracts to an FS dir — the same path QtFsCopyWorker drives for FS copy).
  CMyComPtr<IFolderOperations> ops;
  IFolderFolder *cur = currentFolder();
  if (!cur)
    return tempPaths;
  cur->QueryInterface(IID_IFolderOperations, (void **)&ops);
  if (!ops)
    return tempPaths;

  // (c) mint a window-owned temp dir (registered for cleanup on FM close). The
  // factory also hands back the GUI-thread overwrite prompt, the GUI-thread
  // password prompt (Encrypted-FM: encrypted DATA), and the headless flag the
  // extraction worker needs. No factory (e.g. browser binary) => no drag.
  if (!_tempDirFactory)
    return tempPaths;
  QtOverwritePrompt *prompt = nullptr;
  QtPasswordPrompt *pwPrompt = nullptr;
  bool headless = false;
  FString tempDir = _tempDirFactory(&prompt, &pwPrompt, &headless);
  if (tempDir.IsEmpty())
    return tempPaths;
  NFile::NName::NormalizeDirPathPrefix(tempDir); // trailing sep => extract INTO dir

  // (d) run the extract on the modal archive-extract worker (CAgentFolder::
  // CopyTo, COPY/extract semantics — move-OUT is E_NOTIMPL in the engine and
  // deferred anyway). This drives QtExtractCallback (the archive-extract
  // callback), unlike the FS QtFsCopyWorker whose callback gives E_NOINTERFACE
  // for a CAgentFolder.
  QtArchiveExtractWorker worker;
  worker.FolderOperations = ops;
  worker.DestPath = fs2us(tempDir);
  worker.OverwritePrompt = prompt;
  // Encrypted-FM : wire the GUI-thread password prompt so extracting encrypted
  // DATA out of an archive (View/Edit/Open-Outside/drag-OUT) can ask for the
  // password. QtExtractCallback::CryptoGetTextPassword invokes PasswordPrompt
  // unconditionally on an encrypted entry, so it must be non-null here (the GUI
  // factory always supplies it; only a no-prompt browser binary leaves it null,
  // where an encrypted archive can't be entered to extract from anyway).
  worker.PasswordPrompt = pwPrompt;
  worker.DisableUserQuestions = headless;
  // G.4f : translate each selected MODEL row to the engine realIndex (identity when
  // ShowDots is off; the ".." row is already excluded from `rows`).
  for (int r : rows)
    worker.Indices.Add((UInt32)_model->modelRowToRealIndex(r));
  if (worker.Create(UString(L"Extracting"), this) != S_OK)
    return tempPaths;

  // (e) enumerate the extracted TOP-LEVEL entries (files AND folders) into
  // absolute temp paths. The Win32 original hands the shell the top-level
  // extracted names (the receiving app recurses folders itself); mirror that.
  {
    NFile::NFind::CEnumerator e;
    e.SetDirPrefix(tempDir);
    NFile::NFind::CDirEntry de;
    bool found;
    while (e.Next(de, found) && found)
    {
      if (de.IsDots())
        continue;
      tempPaths.Add(fs2us(tempDir + de.Name));
    }
  }

  return tempPaths;
}

bool QtPanel::handleDrop(const QMimeData *mime, Qt::DropAction action)
{
  // B.5b : a FS panel, or an updatable archive panel (add-into-archive on drop),
  // is a valid drop target. Read-only archives reject. The shell's drop handler
  // (dropOnto) decides FS-vs-archive dispatch.
  if (_inArchive && !_isUpdatableArchive)
    return false;
  if (!_dropHandler)
    return false;
  return _dropHandler(this, mime, action);
}

// --- navigation -------------------------------------------------------------

// G.4j : the leaf (last path component) of the CURRENT folder's path. The model's
// currentPath() is kpidPath, which ends with a path separator for both an FS dir
// and an archive sub-folder; we drop the trailing separator(s) and return the part
// after the next separator. Mirrors how OpenParentFolder peels focusedName off
// _currentFolderPrefix (PanelFolderChange.cpp:929-946). Empty at the root.
UString QtPanel::currentFolderLeafName() const
{
  UString p = _model->currentPath();
  // Strip trailing separators (the path is a folder prefix).
  while (!p.IsEmpty())
  {
    const wchar_t c = p.Back();
    if (c == L'/' || c == L'\\')
      p.DeleteBack();
    else
      break;
  }
  if (p.IsEmpty())
    return UString();
  // Leaf = the part after the last remaining separator.
  for (int i = (int)p.Len() - 1; i >= 0; i--)
  {
    const wchar_t c = p[(unsigned)i];
    if (c == L'/' || c == L'\\')
      return p.Ptr((unsigned)i + 1);
  }
  return p; // no separator: the whole thing is the leaf
}

// G.4j : focus + (optionally) select the source row named `name`, scrolling it into
// view. Mirrors RefreshListCtrl placing the focus on CSelectedState::FocusedName.
void QtPanel::focusRowByName(const UString &name, bool select)
{
  if (name.IsEmpty())
    return;
  const int srcRow = _model->rowForName(name);
  if (srcRow < 0)
    return;
  const QModelIndex src = _model->index(srcRow, 0, QModelIndex());
  if (!src.isValid())
    return;
  const QModelIndex proxyIdx = _proxy->mapFromSource(src);
  if (!proxyIdx.isValid())
    return;
  QItemSelectionModel *sm = _view->selectionModel();
  // Move the focus rectangle to the row (and select it so it is highlighted after
  // Up, matching the Win32 FM which both focuses and shows the returned-to child).
  sm->setCurrentIndex(proxyIdx,
      select ? (QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows)
             : QItemSelectionModel::NoUpdate);
  QAbstractItemView *v = activeView();
  v->scrollTo(proxyIdx, QAbstractItemView::EnsureVisible);
}

// G.4j : scroll the currently-focused row back into view (SortItemsWithPropID's
// EnsureVisible(GetFocusedItem) after a re-sort, PanelSort.cpp:277).
void QtPanel::scrollToFocusedRow()
{
  const QModelIndex cur = _view->currentIndex();
  if (!cur.isValid())
    return;
  activeView()->scrollTo(cur, QAbstractItemView::EnsureVisible);
}

void QtPanel::onUp()
{
  // G.4j : capture the leaf of the folder we are LEAVING, so that after going Up
  // we can re-focus + scroll to it in the parent listing (mirrors
  // CPanel::OpenParentFolder's focusedName -> RefreshListCtrl). Captured BEFORE the
  // model rebind, since currentPath() changes once we are in the parent.
  const UString leftLeaf = currentFolderLeafName();

  if (_model->goParent())
  {
    focusRowByName(leftLeaf, true);   // re-focus the subfolder we came from
    refreshAddress();
    emit pathChanged(this);
    return;
  }
  // At the root of the current folder. If it's an archive, pop one parent frame:
  // the Qt mirror of CPanel::OpenParentFolder popping _parentFolders. The popped
  // frame's returnFolder is restored — for a nested archive that's the OUTER
  // archive's root (we re-enter it seamlessly); for the bottom frame it's the FS
  // folder the archive was opened from. We release the popped frame's agentHolder
  // LAST (after the model no longer references its folder), which also drops the
  // now-unneeded inner agent + sub-stream.
  if (_inArchive && !_parentStack.isEmpty())
  {
    CParentFrame frame = _parentStack.last();   // copy keeps the agent alive past pop
    _parentStack.removeLast();

    CMyComPtr<IFolderFolder> returnFolder = frame.returnFolder;
    _model->setRootFolder(returnFolder);

    if (frame.returnIsFs)
    {
      // Exited the outermost archive back to the filesystem.
      _inArchive = false;
      _isUpdatableArchive = false;
    }
    else
    {
      // Returned to an OUTER archive level (G.2b). That level is now the top of
      // the stack; _isUpdatableArchive always tracks the top frame's flag.
      _inArchive = true;
      _isUpdatableArchive = _parentStack.last().updatable;
    }
    // G.4c : leaving the archive context switches the flat default — back to disk
    // (_flatMode = _flatModeForDisk, PanelFolderChange.cpp:1013) when we returned to
    // the filesystem, or to the per-arc default when we returned to an outer level.
    applyContextFlatMode();
    // frame (and its agentHolder) is released here, after the model was rebound.
    // G.4j : re-focus the archive (or inner-archive) entry we just exited, in the
    // restored parent listing — the same focusedName behaviour as the in-folder
    // Up path above.
    focusRowByName(leftLeaf, true);
    refreshAddress();
    emit pathChanged(this);
  }
}

void QtPanel::onAddressEntered()
{
  // Navigate to the typed path if it is an existing directory. (Archive paths
  // are reached by navigating into the containing dir and double-clicking.)
  navigateToFsPath(Q_toU(_address->text()));
}

bool QtPanel::navigateToFsPath(const UString &path)
{
  // B.7c : the shared body of address-bar navigation, also used by
  // CPanel::OpenBookmark (Favorites). Navigate to `path` if it is an existing
  // directory; restore the real path on a bad entry.
  if (path.IsEmpty())
    return false;
  const FString fpath = us2fs(path);
  NFile::NFind::CFileInfo fi;
  if (!fi.Find(fpath) || !fi.IsDir())
  {
    refreshAddress();
    return false;
  }
  CMyComPtr<IFolderFolder> folder;
  if (BindFsFolderPath(fpath, folder) != S_OK || !folder)
  {
    refreshAddress();
    return false;
  }
  _inArchive = false;
  _isUpdatableArchive = false;
  _parentStack.clear();   // G.2b : navigating to an FS dir drops the archive chain
  _model->setRootFolder(folder);
  // G.4c : an FS dir follows the per-disk flat default (_flatModeForDisk).
  applyContextFlatMode();
  refreshAddress();
  emit pathChanged(this);
  return true;
}

void QtPanel::onDoubleClicked(const QModelIndex &proxyIndex)
{
  if (!proxyIndex.isValid())
    return;
  const QModelIndex srcIndex = _proxy->mapToSource(proxyIndex);
  if (_model->enterItem(srcIndex.row()))
  {
    refreshAddress();
    emit pathChanged(this);
  }
}

bool QtPanel::openFsItemByName(const UString &name)
{
  // Honor a file passed on the command line (file-association / "Open with"): find
  // its row in the current FS folder and run the same activation a double-click does
  // (onLeafActivated -> try-as-archive, else open with the associated program).
  if (_inArchive || name.IsEmpty())
    return false;
  const int row = _model->rowForName(name);
  if (row < 0)
    return false;
  onLeafActivated(row);
  return true;
}

void QtPanel::onLeafActivated(int row)
{
  // CPanel::OpenItem / OpenItemInArchive (PanelItemOpen.cpp:950 / :1461): a folder is
  // entered, an archive is opened as a folder, and a regular file is opened with its
  // associated program (ShellExecute -> the registered handler; xdg-open on Linux).
  // The model already entered folders before emitting leafActivated, so here we only
  // see leaf items. A double-click == the "smart" mode (tryInternal && tryExternal):
  // try-as-archive UNLESS DoItemAlwaysStart(name), else open externally.
  const UString name = _model->itemName(row);

  // G.3e : IsVirus_Message guard. CPanel::OpenItem / OpenItemInArchive run this
  // BEFORE any external launch (PanelItemOpen.cpp:955-957, :1470-1472). A spoofed
  // name (U+202E RLO, or 5+ spaces) is BLOCKED — we warn and do NOT open anything.
  // The original checks `name` (GetItemRelPath2 / GetItemName); we use the item name.
  {
    UString sanitised;
    bool isSpaceError = false;
    if (Qt_IsVirusName(name, sanitised, isSpaceError))
    {
      emit openBlockedAsVirus(this, UStr_toQ(name), UStr_toQ(sanitised), isSpaceError);
      return;
    }
  }

  // G.3a : DoItemAlwaysStart short-circuit (PanelItemOpen.cpp:970 / :1480). For a
  // document-family extension (ZIP-based .docx/.odt/.epub ... or .pdf/.ps ...), do
  // NOT probe it as an archive — open it directly with its associated app, exactly
  // as the original skips the internal open when alwaysStart(name). This is what
  // keeps a .docx from being browsed as a zip folder.
  if (DoItemAlwaysStart_Document(name))
  {
    emit openExternallyRequested(this, row);
    return;
  }

  bool encrypted = false;

  if (_inArchive)
  {
    // G.2b : an item INSIDE an open archive. FIRST try to enter it seamlessly as a
    // NESTED sub-folder (CPanel::OpenItemInArchive tryAsArchive branch,
    // PanelItemOpen.cpp:1503). S_OK = entered (we're now inside the nested archive).
    // E_ABORT = the user cancelled the (encrypted) password prompt -> stay put, do
    // NOT extract+open externally.
    const HRESULT res = tryOpenNestedArchive(row, &encrypted);
    if (res == S_OK || res == E_ABORT)
      return;
    // G.3b : a REAL open error (encrypted sub-stream we couldn't open) -> show the
    // faithful error dialog and DO NOT extract+open externally. S_FALSE (not an
    // archive) falls through to the existing extract-then-open-externally path.
    // The display path mirrors the original's virtualFilePath (_currentFolderPrefix
    // + relPath, PanelItemOpen.cpp): currentPath() is the open archive's kpidPath.
    if (res != S_FALSE || encrypted)
    {
      emit openArchiveError(this, UStr_toQ(_model->currentPath() + name), (int)res, encrypted);
      return;
    }
    emit openExternallyRequested(this, row);
    return;
  }

  // An FS leaf: try to open it as an archive (enter it as a folder). Mirror
  // CPanel::OpenItem (PanelItemOpen.cpp:977-994): S_OK = opened; E_ABORT = the user
  // cancelled (e.g. the header-encrypted password prompt) -> stay put, do NOT hand
  // the raw archive to xdg-open.
  const UString folderPath = _model->currentPath();
  const UString fullPath = folderPath + name;
  const HRESULT res = tryOpenAsArchive(row, &encrypted);
  if (res == S_OK || res == E_ABORT)
    return;
  // G.3b : distinguish "not an archive" (S_FALSE -> external open, the common case)
  // from a REAL open ERROR (a corrupt / wrong-typed / unreadable archive). Mirror
  // CPanel::OpenAsArc_Msg (PanelItemOpen.cpp:546): show the error MessageBox when
  // (res != S_FALSE) OR the file was encrypted, and DO NOT xdg-open it. For a plain
  // S_FALSE non-encrypted file, fall through to open it with its associated program.
  if (res != S_FALSE || encrypted)
  {
    emit openArchiveError(this, UStr_toQ(fullPath), (int)res, encrypted);
    return;
  }
  emit openExternallyRequested(this, row);
}

HRESULT QtPanel::tryOpenAsArchive(int row, bool *outEncrypted)
{
  if (outEncrypted)
    *outEncrypted = false;
  const UString folderPath = _model->currentPath();
  const UString name = _model->itemName(row);
  if (name.IsEmpty())
    return E_ABORT;   // defensive: nothing to open -> treat as a no-op (no external)
  const UString fullPath = folderPath + name;

  // Capture the FS folder we'd return to BEFORE doing anything else. This makes
  // the "stay put on cancel/failure" invariant obvious: we only swap the model
  // AFTER a confirmed-successful open below.
  CMyComPtr<IFolderFolder> fsReturn = _model->currentFolder();
  if (!fsReturn)
    return E_ABORT;   // defensive: no FS folder to return to -> no-op

  CMyComPtr<IFolderFolder> archiveRoot;
  CMyComPtr<IUnknown> agentHolder;
  bool isUpdatable = false;

  // P.1 : keep small archives instant, run large ones off the GUI thread.
  // The open scan on a small file is sub-frame; threading it (and showing the
  // delayed progress dialog) would only add latency, so we open those
  // synchronously. Large archives go through the QtProgressThreadVirt worker so
  // the GUI stays responsive (and a Cancel works) during the slow format scan.
  // The delayed-show in QtProgressThreadVirt::Create() additionally guarantees a
  // "small file that turns out slow" never flashes a dialog within the delay.
  NFile::NFind::CFileInfo fi;
  const bool big = fi.Find(us2fs(fullPath)) && fi.Size >= ((UInt64)16 << 20);

  if (!big && !_forceThreadOpen)
  {
    // Synchronous fast-path: no worker, no dialog, no Sync (callback stays no-op).
    // Encrypted-FM : we ARE on the GUI thread here, so the password prompt (if a
    // header-encrypted archive needs it) runs via DirectConnection — the
    // OpenArchiveAsFolder callback picks that automatically. A Cancel comes back as
    // E_ABORT and we fall through to the "stay on the FS folder" return below.
    // There is no progress dialog on this path, so parent the password dialog to
    // the panel (and clear any stale parent a prior extract worker's now-destroyed
    // progress dialog left on the shared prompt).
    if (_passwordPrompt)
      _passwordPrompt->SetParentWidget(this);
    bool encrypted = false;
    const HRESULT res = OpenArchiveAsFolder(fullPath, UString(), false,
        archiveRoot, agentHolder, &isUpdatable, nullptr, _passwordPrompt, &encrypted);
    if (outEncrypted)
      *outEncrypted = encrypted;
    if (res != S_OK || !archiveRoot)
      return (res == S_OK) ? E_FAIL : res; // S_FALSE=not archive / E_ABORT=cancelled / error
  }
  else
  {
    // Threaded path: the open + BindToRootFolder run on the worker thread; the
    // open callback drives the dialog's Sync and honours Cancel. Create() blocks
    // the GUI thread (nested modal event loop) until the worker joins.
    QtArchiveOpenWorker worker;
    worker.ArcPath = fullPath;
    // Encrypted-FM : the open callback runs on the worker thread; it reaches this
    // GUI-thread prompt via BlockingQueuedConnection for a header-encrypted archive
    // (Cancel -> E_ABORT -> worker.RootFolder stays null -> we keep the FS folder).
    worker.PasswordPrompt = _passwordPrompt;
    worker.DisableUserQuestions = _headless;
    const HRESULT createRes = worker.Create(UString("Opening"), this);
    // G.3b : the worker now ALWAYS returns S_OK to the base (so the generic progress
    // dialog never pops its own error popup) and reports the TRUE open outcome via
    // worker.OpenResult / worker.Encrypted. `createRes != S_OK` means the worker
    // thread itself failed (e.g. an exception) — treat that as a hard error.
    const HRESULT res = (createRes != S_OK) ? createRes : worker.OpenResult;
    if (outEncrypted)
      *outEncrypted = worker.Encrypted;
    if (res != S_OK || !worker.RootFolder)
      return (res == S_OK) ? E_FAIL : res; // E_ABORT=cancelled (preserved by the worker) / else not-archive/error
    archiveRoot = worker.RootFolder;
    agentHolder = worker.AgentHolder;
    isUpdatable = worker.IsUpdatable;
  }

  // --- GUI-thread bind (only reached on a confirmed-successful open) ---------
  // setRootFolder -> QtFolderModel::setRootFolder does beginResetModel/
  // endResetModel, which MUST run on the GUI thread. We are on the GUI thread
  // here: the worker has already joined inside Create().
  // G.2b : this is the BOTTOM of the parent stack (FS -> first archive). The frame
  // holds the FS folder as its returnFolder so Up out of the archive root restores
  // the filesystem (the original B.2 seamless exit), and the agent so refresh()
  // re-binds it.
  CParentFrame frame;
  frame.agentHolder = agentHolder;
  frame.returnFolder = fsReturn;
  frame.returnIsFs = true;
  frame.updatable = isUpdatable;
  // G.6a : the host archive's FS path (the analog of CFolderLink ParentFolderPath +
  // RelPath, PanelFolderChange.cpp:362-364) so currentFolderPrefix() can identify the
  // archive in the drop-into-archive confirmation.
  frame.hostPath = fullPath;
  _parentStack.push_back(frame);

  _inArchive = true;
  _isUpdatableArchive = isUpdatable;
  _model->setRootFolder(archiveRoot);
  // G.4c : entering an archive switches to the per-arc flat default (the original
  // sets _flatMode = _flatModeForArc here, PanelItemOpen.cpp:508).
  applyContextFlatMode();
  refreshAddress();
  emit pathChanged(this);
  return S_OK;
}

HRESULT QtPanel::tryOpenNestedArchive(int row, bool *outEncrypted)
{
  if (outEncrypted)
    *outEncrypted = false;
  // Mirror CPanel::OpenItemInArchive's tryAsArchive branch (PanelItemOpen.cpp:1503).
  // We must be inside an archive with a live agent level on the stack.
  if (!_inArchive || _parentStack.isEmpty())
    return S_FALSE;

  IFolderFolder *cur = currentFolder();
  if (!cur)
    return S_FALSE;

  // G.4f : `row` is a MODEL row; the ".." row never reaches here (enterItem routes it
  // to parentActivated, not leafActivated). Map to the engine item index for GetStream.
  const int realIndex = _model->modelRowToRealIndex(row);
  if (realIndex < 0)
    return S_FALSE;   // the ".." pseudo-row is not an archive entry

  const UString name = _model->itemName(row);
  if (name.IsEmpty())
    return S_FALSE;   // defensive: no name -> not enterable, fall back to external

  // (a) QI the CURRENT CAgentFolder for IInArchiveGetStream and GetStream(index)
  // the sub-archive's stream. `realIndex` is the folder-relative item index — exactly
  // what CAgentFolder::GetStream maps (Agent.cpp:1098), and the same index the
  // original passes to getStream->GetStream(index).
  CMyComPtr<IInArchiveGetStream> getStream;
  cur->QueryInterface(IID_IInArchiveGetStream, (void **)&getStream);
  if (!getStream)
    return S_FALSE;   // this folder cannot vend sub-streams -> not enterable

  // GetStream returns S_OK with a NULL stream for an entry it cannot vend as a raw
  // seekable stream (a SOLID/compressed 7z member, a directory, etc.) — the exact
  // case CPanel::OpenItemInArchive handles by falling through to extract+external.
  CMyComPtr<ISequentialInStream> subSeqStream;
  getStream->GetStream((UInt32)realIndex, &subSeqStream);
  if (!subSeqStream)
    return S_FALSE;   // no stream for this entry -> external

  CMyComPtr<IInStream> subStream;
  subSeqStream.QueryInterface(IID_IInStream, &subStream);
  if (!subStream)
    return S_FALSE;   // not seekable -> can't open as an archive in place -> external

  // (b) the folder we'd return to on Up = the CURRENT (outer) archive folder.
  // Captured BEFORE the model swaps, so a failed/cancelled open leaves us put.
  CMyComPtr<IFolderFolder> outerReturn = cur;

  // (c) open the sub-stream as an archive (CAgent::Open from the stream). Reuse the
  // shared open/password infra; _passwordPrompt drives an encrypted NESTED archive
  // exactly as it drives an encrypted top-level one (we're on the GUI thread here,
  // so DirectConnection). Cancel -> E_ABORT; not-an-archive -> S_FALSE/error.
  if (_passwordPrompt)
    _passwordPrompt->SetParentWidget(this);
  CMyComPtr<IFolderFolder> nestedRoot;
  CMyComPtr<IUnknown> nestedAgent;
  bool isUpdatable = false;
  bool encrypted = false;
  const HRESULT res = OpenArchiveStreamAsFolder(subStream, name, UString(), false,
      nestedRoot, nestedAgent, &isUpdatable, _passwordPrompt, &encrypted);
  if (outEncrypted)
    *outEncrypted = encrypted;
  if (res != S_OK || !nestedRoot)
  {
    // E_ABORT (cancelled) is preserved for the caller (stay put, no external open).
    // S_FALSE / E_FAIL / other => not a (readable) archive => caller extracts+opens.
    return (res == S_OK) ? S_FALSE : res;
  }

  // (d) push the nested frame and rebind. The frame keeps the INNER agent alive AND
  // (because the OUTER agent(s) already sit in lower frames) keeps the whole chain
  // alive — the sub-stream is a view onto the outer archive, which must outlive the
  // nested browse. returnIsFs=false so Up restores the OUTER archive folder. A
  // stream archive is not writable in place, so isUpdatable comes back false here
  // and in-place Delete/Add stay refused (G.2b is read/browse/extract only).
  CParentFrame frame;
  frame.agentHolder = nestedAgent;
  frame.returnFolder = outerReturn;
  frame.returnIsFs = false;
  frame.updatable = isUpdatable;
  // G.6a : a nested archive contributes the entered sub-archive name to the full
  // prefix (CFolderLink RelPath); the parent in-archive path is already part of the
  // lower frame's contribution via currentFolderPrefix()'s walk.
  frame.hostPath = name;
  _parentStack.push_back(frame);

  _inArchive = true;
  _isUpdatableArchive = isUpdatable;
  _model->setRootFolder(nestedRoot);   // model reset, like setRootFolder (GUI thread)
  // G.4c : a nested archive is still an archive context -> the per-arc flat default.
  applyContextFlatMode();
  refreshAddress();
  emit pathChanged(this);
  return S_OK;
}

// --- selection / focus / model events ---------------------------------------

void QtPanel::onViewSelectionChanged()
{
  emit selectionChanged(this);
}

// G.4f : tell the model whether the current folder has a parent (Up is possible), so
// it knows whether to synthesize the ".." row. The !IsRootFolder() gate: a parent
// exists when the model can goParent (an FS non-root dir, or an archive sub-folder),
// OR when we are inside an archive with a frame to pop (the archive ROOT, where the
// model's own goParent returns false but Up still exits the archive). The FS
// filesystem root satisfies neither -> no ".." (mirrors CPanel suppressing it there).
// setHasParent() may trigger a model reset (the ".." row appears/disappears); the
// _updatingHasParent guard stops that reset re-entering onModelReset -> here.
void QtPanel::updateHasParentForDots()
{
  if (_updatingHasParent)
    return;
  const bool hasParent =
      !_model->isRoot() || (_inArchive && !_parentStack.isEmpty());
  _updatingHasParent = true;
  _model->setHasParent(hasParent);
  _updatingHasParent = false;
}

void QtPanel::onModelReset()
{
  // After a reset the selection model is unchanged but rows differ; recompute.
  rebindSelectionModel();   // B.7b : defensive re-share (no-op if still bound)
  // G.4f : recompute whether ".." should appear at the just-bound folder. This is
  // the single chokepoint after EVERY folder change (setRootFolder / enterItem /
  // goParent / archive enter-exit / refresh), so the ".." row is always in sync.
  updateHasParentForDots();
  // G.4a : the model just rebound (folder change / archive enter-exit / refresh).
  // Restore this list-type's persisted column layout + sort (CPanel::InitColumns ->
  // _listViewInfo.Read(_typeIDString)). A no-op when the type is unchanged.
  restoreViewSettingsForType();
  // G.4e : the single chokepoint after EVERY folder change — re-point the Auto
  // Refresh watcher to the now-current FS directory (or drop it inside an archive),
  // mirroring the original re-targeting its directory-change source each
  // RefreshListCtrl. No-op when auto-refresh is off.
  updateWatcher();
  refreshAddress();
  emit selectionChanged(this);
}

// G.4l : the model committed an inline rename. Re-list the folder so the new name
// (and, for an archive, the reopened-in-place agent state) shows. refresh() handles
// both cases: an ARCHIVE re-binds from the reopened agent's root (the in-place
// Close()+ReOpen() the engine already did inside Rename()), a plain FS folder
// re-scans disk. (Same code path doRename uses after its own ops->Rename.)
void QtPanel::onItemRenamed()
{
  refresh();
  emit selectionChanged(this);
}

// CPanel::OnContextMenu (PanelMenu.cpp:1081). A right-click first focuses this
// panel, then — mirroring the Win32 list-view behaviour where the clicked item
// becomes the operated item — if the item under the cursor is not part of the
// current selection it is selected alone (right-clicking an already-selected
// item leaves the multi-selection intact, matching the system list view). A
// click on empty space (no item) leaves an empty selection, which the shell
// renders as the background/no-selection menu variant. We then map the click to
// screen coordinates and hand off to the shell to assemble + exec() the QMenu.
void QtPanel::onCustomContextMenu(const QPoint &localPos)
{
  // The signal's QPoint is in the SENDER view's coordinate space; use that view
  // (the one actually right-clicked), not necessarily activeView().
  QAbstractItemView *v = qobject_cast<QAbstractItemView *>(sender());
  if (!v)
    v = activeView();

  emit focused(this);

  // QWidget::customContextMenuRequested delivers `localPos` already in VIEWPORT
  // coordinates for QAbstractScrollArea subclasses (QTreeView/QListView): Qt
  // remaps the context-menu event to the viewport before emitting the signal. So
  // indexAt() (which wants viewport coordinates) takes it DIRECTLY, with no extra
  // mapping, and the global position is mapped from the VIEWPORT (which sits below
  // the header), not the view widget. Mapping from the view added the header
  // height, which both mis-selected the row above the cursor and lifted the menu
  // above the click point.
  const QModelIndex idx = v->indexAt(localPos);
  QItemSelectionModel *sm = v->selectionModel();
  if (idx.isValid())
  {
    if (sm && !sm->isSelected(idx))
    {
      // Select the clicked row alone (CPanel selects the clicked item).
      sm->select(idx,
          QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      sm->setCurrentIndex(idx,
          QItemSelectionModel::NoUpdate);
    }
  }
  else
  {
    // Empty-space right-click: clear selection -> background menu variant.
    if (sm)
      sm->clearSelection();
  }

  const QPoint globalPos = v->viewport()->mapToGlobal(localPos);
  emit contextMenuRequested(this, globalPos);
}

// G.4b : the header column chooser. Faithful port of CPanel::ShowColumnsContextMenu
// (PanelItems.cpp:1373). A checkable popup of EVERY property column (checked ==
// visible); toggling shows/hides the column. The Name column (column 0) action is
// disabled + always-checked (MF_GRAYED on item 0); de-selecting the ACTIVE SORT
// column resets the sort to Name (ShowColumnsContextMenu:1417-1421).
void QtPanel::onHeaderContextMenu(const QPoint &localPos)
{
  emit focused(this);

  QMenu menu(this);
  // One checkable action per column (CPanel iterates _columns, AppendItem with
  // MF_CHECKED for IsVisible, MF_GRAYED for i==0). We map each action back to its
  // column PROPID so the toggle is robust against column-order changes.
  const int n = _model->columnCount(QModelIndex());
  for (int c = 0; c < n; c++)
  {
    const PROPID propID = _model->columnPropID(c);
    QAction *act = menu.addAction(_model->columnName(c));
    act->setCheckable(true);
    act->setChecked(_model->isColumnVisible(c));
    act->setData((uint)propID);
    if (propID == kpidName)
      act->setEnabled(false);   // the locked main column (MF_GRAYED on item 0)
  }

  QAction *chosen = menu.exec(_view->header()->viewport()->mapToGlobal(localPos));
  if (!chosen)
    return;
  const PROPID propID = (PROPID)chosen->data().toUInt();
  // The new visibility is the action's (just-toggled) checked state.
  const bool nowVisible = chosen->isChecked();

  // De-selecting the active SORT column resets the sort to Name, exactly as
  // ShowColumnsContextMenu does (_sortID == prop.ID -> _sortID = kpidName) BEFORE
  // the column is removed. _proxy->sortKey() is the explicit arrange-by key; when
  // it is kpidNoProperty the effective sort is the clicked HEADER column's PROPID.
  if (!nowVisible)
  {
    const PROPID effectiveSort =
        (_proxy->sortKey() != kpidNoProperty) ? _proxy->sortKey() : activeSortColumnPropID();
    if (effectiveSort == propID)
    {
      _sortID = kpidName;
      _ascending = true;
      _proxy->setUnsortedMode(false);
      _proxy->setSortKey(kpidNoProperty);   // sort by the Name column header
      const int nameCol = _model->columnForPropID(kpidName);
      _view->setSortingEnabled(true);
      _view->sortByColumn(nameCol >= 0 ? nameCol : 0, Qt::AscendingOrder);
    }
  }

  // Toggle the model flag (Name stays locked-visible); applyColumnVisibility()
  // re-hides/shows the section via columnVisibilityChanged().
  _model->setColumnVisibleByPropID(propID, nowVisible);

  // G.4a : a column hide/show is a CListViewInfo change — persist this type's layout
  // (setSectionHidden does not fire the sectionResized hook, so save explicitly).
  scheduleSaveViewSettings();
}

// G.4b : the PROPID the view is currently sorted by via the clicked header column
// (when the proxy has no explicit arrange-by key override). QHeaderView's
// sortIndicatorSection() is the column the Name/click sort uses.
PROPID QtPanel::activeSortColumnPropID() const
{
  if (!_view->header())
    return kpidName;
  const int sec = _view->header()->sortIndicatorSection();
  const PROPID pid = _model->columnPropID(sec);
  return pid ? pid : kpidName;
}

// G.4b : re-apply QHeaderView::setSectionHidden so the view's hidden sections match
// the model's per-column IsVisible flags. The Qt analogue of CPanel rebuilding the
// _visibleColumns list (AddColumn/DeleteColumn). Only the Details QTreeView has a
// header; the icon/list view shows a single column and is unaffected.
void QtPanel::applyColumnVisibility()
{
  QHeaderView *hdr = _view->header();
  if (!hdr)
    return;
  const int n = _model->columnCount(QModelIndex());
  for (int c = 0; c < n && c < hdr->count(); c++)
    hdr->setSectionHidden(c, !_model->isColumnVisible(c));
}

QVector<QPair<PROPID, bool>> QtPanel::visibleColumnPropIDs() const
{
  QVector<QPair<PROPID, bool>> out;
  const int n = _model->columnCount(QModelIndex());
  out.reserve(n);
  for (int c = 0; c < n; c++)
    out.push_back(qMakePair(_model->columnPropID(c), _model->isColumnVisible(c)));
  return out;
}

void QtPanel::setColumnVisible(PROPID propID, bool visible)
{
  // Mirror the header-menu path's sort-column reset when hiding the active sort
  // column, so a persistence-driven hide behaves identically to a user toggle.
  if (!visible && propID != kpidName)
  {
    const PROPID effectiveSort =
        (_proxy->sortKey() != kpidNoProperty) ? _proxy->sortKey() : activeSortColumnPropID();
    if (effectiveSort == propID)
    {
      _sortID = kpidName;
      _ascending = true;
      _proxy->setUnsortedMode(false);
      _proxy->setSortKey(kpidNoProperty);
      const int nameCol = _model->columnForPropID(kpidName);
      _view->setSortingEnabled(true);
      _view->sortByColumn(nameCol >= 0 ? nameCol : 0, Qt::AscendingOrder);
    }
  }
  _model->setColumnVisibleByPropID(propID, visible);
  // G.4a : persist this type's layout after a visibility toggle (same as the
  // header-menu path).
  scheduleSaveViewSettings();
}

// === G.4a : per-list-type view-settings persistence ==========================
// The Qt mirror of ViewSettings.cpp's CListViewInfo (per-type column layout + sort)
// + CListMode (per-panel view mode) + SavePanelPath/ReadPanelPath (per-panel path).
// CListViewInfo's {Width, IsVisible, order} + {SortID, Ascending} serialization is
// captured idiomatically by QHeaderView::saveState()/restoreState() (one QByteArray
// blob), stored under the list-type key; the model-level column-visibility set is
// stored alongside so a hidden column survives the model's column rebuild (which
// re-applies the GetColumnVisible defaults on every folder change).

void QtPanel::restoreViewSettingsForType()
{
  // Persistence disabled (a browser/test binary that never assigned a panel index)
  // => keep the old reset-on-restart behaviour.
  if (_panelIndex < 0)
    return;

  // The list-type id of the just-bound folder (CPanel::GetFolderTypeID), reduced to
  // its settings key ("fs" for the filesystem, the kpidType BSTR for an archive).
  const UString newKey = QtFmSettings::ListTypeKey(_model->folderTypeId());

  // Only (re)restore when the TYPE actually changed since the last bind, so an
  // in-place archive refresh (same type) or a plain re-list does NOT clobber the
  // live column widths/sort the user just adjusted (mirrors CPanel::InitColumns
  // skipping the Read when _typeIDString == oldType, PanelItems.cpp:110-111).
  if (!_currentTypeKey.IsEmpty() && _currentTypeKey == newKey)
    return;
  _currentTypeKey = newKey;

  // Guard the save hooks so the programmatic restore below (restoreState +
  // setSectionHidden + sortByColumn) does not immediately re-save what it just read.
  _restoringView = true;

  // (a) re-seed the model's column-visibility set from this type's stored set, then
  // drop the previous type's overrides so they do not leak across types. The model
  // re-applies these IsVisible flags in rebuildColumns(); applyColumnVisibility()
  // (below + the model's post-reset emit) maps them onto the header sections.
  const QVector<QPair<PROPID, bool>> savedCols =
      QtFmSettings::ReadColumnVisible(newKey);
  _model->clearSavedVisible();
  if (!savedCols.isEmpty())
    _model->setVisibleColumnsFromSaved(savedCols);  // emits columnVisibilityChanged

  // (b) restore the header geometry blob (section sizes, visual order, hidden set,
  // sort indicator). QHeaderView::restoreState is a no-op for an empty/foreign blob.
  const QByteArray state = QtFmSettings::ReadListViewState(newKey);
  if (!state.isEmpty() && _view->header())
    _view->header()->restoreState(state);

  // (c) keep the header's hidden sections in sync with the model flags after the
  // restore (the model may carry a column the saved header state predates).
  applyColumnVisibility();

  _restoringView = false;
}

void QtPanel::scheduleSaveViewSettings()
{
  // Suppress saves driven by our own restore, and when persistence is off.
  if (_restoringView || _panelIndex < 0)
    return;
  if (_saveViewTimer)
    _saveViewTimer->start();   // (re)arm the debounce; saveViewSettingsNow on timeout
}

void QtPanel::saveViewSettingsNow()
{
  if (_panelIndex < 0)
    return;
  // No type bound yet (no folder) => nothing to key on.
  if (_currentTypeKey.IsEmpty())
    return;
  if (_view->header())
    QtFmSettings::SaveListViewState(_currentTypeKey, _view->header()->saveState());
  // Persist the model-level column-visibility set too (saveState records the header's
  // hidden sections, but the model rebuilds columns on a folder change and would
  // otherwise re-apply the GetColumnVisible defaults — this set re-seeds them).
  QtFmSettings::SaveColumnVisible(_currentTypeKey, visibleColumnPropIDs());
}

void QtPanel::restorePersistedViewMode()
{
  if (_panelIndex < 0)
    return;
  // CListMode::Read — the per-panel stored view mode (default Details=3).
  setViewMode(QtFmSettings::ReadListMode(_panelIndex, 3));
}

bool QtPanel::eventFilter(QObject *obj, QEvent *ev)
{
  // G.6d : right-button drag tracking on either view's viewport. The Win32 FM treats
  // a drag that began with the right (or middle) button as m_IsRightButton (DragEnter,
  // PanelDrag.cpp:2401-2402); Qt's startDrag() handles only the left button, so we
  // detect the right-button press-and-drag here and run startRightButtonDrag() once
  // the cursor passes the start-drag distance.
  const bool onViewport =
      (obj == _view->viewport() || obj == _listView->viewport());
  if (onViewport && ev->type() == QEvent::MouseButtonPress)
  {
    QMouseEvent *me = static_cast<QMouseEvent *>(ev);
    if (me->button() == Qt::RightButton || me->button() == Qt::MiddleButton)
    {
      // Only arm a right-drag when the press lands on a SELECTED row that can be a
      // drag source — otherwise let the press fall through so a plain right-click
      // still raises the context menu (onCustomContextMenu) on a non-drag release.
      _rbuttonPressPos = me->position().toPoint();
      _rbuttonDragView = qobject_cast<QAbstractItemView *>(obj->parent());
      // Do NOT consume: the view still gets the press (selection/context-menu logic).
    }
  }
  else if (onViewport && ev->type() == QEvent::MouseMove
           && _rbuttonPressPos != QPoint(-1, -1))
  {
    QMouseEvent *me = static_cast<QMouseEvent *>(ev);
    if (me->buttons() & (Qt::RightButton | Qt::MiddleButton))
    {
      const QPoint now = me->position().toPoint();
      if ((now - _rbuttonPressPos).manhattanLength()
          >= QApplication::startDragDistance())
      {
        // Disarm BEFORE exec (it blocks on the nested drag loop); the release that
        // ends the drag must not re-trigger or pop the context menu.
        _rbuttonPressPos = QPoint(-1, -1);
        startRightButtonDrag();
        _rbuttonDragView = nullptr;
        return true;   // consume: this move belongs to the drag we just started
      }
    }
  }
  else if (onViewport && ev->type() == QEvent::MouseButtonRelease)
  {
    QMouseEvent *me = static_cast<QMouseEvent *>(ev);
    if (me->button() == Qt::RightButton || me->button() == Qt::MiddleButton)
    {
      // A right-button release WITHOUT having crossed the drag threshold is a plain
      // right-click: clear the armed state and let the view raise its context menu.
      _rbuttonPressPos = QPoint(-1, -1);
      _rbuttonDragView = nullptr;
    }
  }

  if (ev->type() == QEvent::FocusIn ||
      (ev->type() == QEvent::MouseButtonPress &&
       (obj == _view->viewport() || obj == _listView->viewport())))
  {
    emit focused(this);
  }
  else if (ev->type() == QEvent::KeyPress && (obj == _view || obj == _listView))
  {
    QKeyEvent *ke = static_cast<QKeyEvent *>(ev);
    const Qt::KeyboardModifiers mods = ke->modifiers();
    const bool alt   = mods.testFlag(Qt::AltModifier);
    const bool ctrl  = mods.testFlag(Qt::ControlModifier);
    const bool shift = mods.testFlag(Qt::ShiftModifier);

    if (ke->key() == Qt::Key_Backspace)
    {
      // CPanel::OnKeyDown VK_BACK -> OpenParentFolder (PanelKey.cpp:261).
      onUp();
      return true;
    }
    if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
    {
      // Enter on the current row == CPanel "Open" (enter folder / open item).
      // currentIndex() is shared via the one selection model, so reading it from
      // _view is correct regardless of which view has focus.
      const QModelIndex cur = _view->currentIndex();
      if (cur.isValid())
        onDoubleClicked(cur);
      return true;
    }

    // G.4k : the dual-pane panel-local accelerators that are NOT already covered
    // by the window's menu shortcuts. Mirrors CPanel::OnKeyDown (PanelKey.cpp).

    // Tab -> OnTab : move focus to the OTHER panel (PanelKey.cpp:41-44 ->
    // CPanelCallbackImp::OnTab). The shell focuses otherPanel()'s active view.
    if (ke->key() == Qt::Key_Tab && !alt && !ctrl && !shift)
    {
      emit tabToOtherPanel(this);
      return true;
    }

    // Alt+F1 / Alt+F2 -> SetFocusToPath(0 / 1) : focus the LEFT / RIGHT panel's
    // path field (PanelKey.cpp:70-75; F1 => index 0, F2 => index 1). Require Alt
    // only (no Ctrl/Shift), exactly as the original guards (&& alt && !ctrl &&
    // !shift).
    if ((ke->key() == Qt::Key_F1 || ke->key() == Qt::Key_F2)
        && alt && !ctrl && !shift)
    {
      emit setFocusToPathRequested(this, ke->key() == Qt::Key_F1 ? 0 : 1);
      return true;
    }

    // Alt+Up -> OnSetSameFolder : open the SAME folder in the OTHER panel
    // (PanelKey.cpp:202-203). Alt+Right / Alt+Left -> OnSetSubFolder : open the
    // focused sub-folder (or parent) in the OTHER panel (PanelKey.cpp:208-219;
    // both arrow directions call the same OnSetSubFolder).
    if (alt && !ctrl && !shift)
    {
      if (ke->key() == Qt::Key_Up)
      {
        emit setSameFolderRequested(this);
        return true;
      }
      if (ke->key() == Qt::Key_Right || ke->key() == Qt::Key_Left)
      {
        emit setSubFolderRequested(this);
        return true;
      }
    }

    // Shift+Up/Down/Home/End incremental range-select (CPanel::OnArrowWithShift,
    // PanelKey.cpp:194-222 / PanelSelect.cpp:43): NOT intercepted here. The Win32
    // original only runs OnArrowWithShift in its special _mySelectMode; the port
    // does not use that mode, and the QTreeView/QListView run in Qt's native
    // ExtendedSelection, which already extends the selection by one row on
    // Shift+Arrow / Shift+Home / Shift+End. We let that default handling proceed.
  }
  return QWidget::eventFilter(obj, ev);
}

void QtPanel::refreshAddress()
{
  const UString p = _model->currentPath();
  QString qp = UStr_toQ(p);
  if (_inArchive)
    _address->setText(QStringLiteral("[archive] ") + qp);
  else
    _address->setText(qp);
  // In-archive paths are not directly editable as FS paths.
  _address->setReadOnly(_inArchive);
}

// G.4d : the ancestor folders of the current FS path, root-first, each with a
// trailing separator — the Linux mirror of CPanel::OnComboBoxCommand's CBN_DROPDOWN
// ancestor walk (PanelFolderChange.cpp:632-763). The original splits the path below
// the ROOT PREFIX into parts and accumulates them (indent++ per level); on Linux the
// root prefix is just "/", so we emit "/" then each cumulative prefix. The Windows-
// only drive-roots / Documents / Computer / Network items are NOT emitted (no Linux
// substitute invented; the Open Root Folder task owns "/"). Empty inside an archive.
UStringVector QtPanel::addressAncestors() const
{
  UStringVector out;
  if (_inArchive)
    return out;
  UString path = _model->currentPath();   // trailing separator for a directory
  if (path.IsEmpty())
    return out;
  // Walk from the leaf upward, collecting each ancestor (incl. the current dir and
  // the "/" root), then reverse so the result is root-first like the indented combo.
  // IS_PATH_SEPAR matches the engine's separator test ('/' on Linux).
  UStringVector rev;
  // Drop a single trailing separator so the current dir is its own first entry with
  // a clean prefix; we re-append a separator on every emitted entry below.
  if (path.Len() > 1 && IS_PATH_SEPAR(path.Back()))
    path.DeleteBack();
  for (;;)
  {
    UString withSep = path;
    if (withSep.IsEmpty() || !IS_PATH_SEPAR(withSep.Back()))
      withSep.Add_PathSepar();
    rev.Add(withSep);
    if (path.Len() <= 1)   // reached "/" (or "") — stop
      break;
    const int pos = path.ReverseFind_PathSepar();
    if (pos < 0)
      break;
    if (pos == 0)
      path = L"/";         // the root itself is the next (and last) ancestor
    else
      path.DeleteFrom((unsigned)pos);
  }
  // rev is leaf-first; emit root-first to match the indented top-down breadcrumb.
  for (int i = (int)rev.Size() - 1; i >= 0; i--)
    out.Add(rev[(unsigned)i]);
  return out;
}

void QtPanel::onAddressDropdown()
{
  // CPanel::OnComboBoxCommand CBN_DROPDOWN (PanelFolderChange.cpp:627): build the
  // dropdown from the current path's ancestor folders (indented breadcrumb) plus the
  // recent-folders history. Inside an archive the address path is not an FS path, so
  // there is nothing to navigate to — do nothing (the field is read-only there too).
  if (_inArchive)
    return;
  QMenu menu(this);

  // --- the ancestor breadcrumb, root-first, indented by depth (the original's
  //     indent++ per accumulated path part). Each action navigates to that folder.
  const UStringVector ancestors = addressAncestors();
  unsigned indent = 0;
  FOR_VECTOR (i, ancestors)
  {
    const UString &full = ancestors[i];
    // The display leaf: the last path component (or "/" for the root). The full path
    // is carried in the action's data for the navigation.
    UString leaf = full;
    if (leaf.Len() > 1 && IS_PATH_SEPAR(leaf.Back()))
      leaf.DeleteBack();
    const int pos = leaf.ReverseFind_PathSepar();
    if (pos >= 0)
      leaf.DeleteFrontal((unsigned)(pos + 1));
    if (leaf.IsEmpty())
      leaf = L"/";
    QString text;
    for (unsigned k = 0; k < indent; k++)
      text += QStringLiteral("    ");
    text += UStr_toQ(leaf);
    QAction *act = menu.addAction(text);
    act->setData(UStr_toQ(full));
    indent++;
  }

  // --- the app-level recent-folders history (AppState FolderHistory), most-recent-
  //     first, after a separator (the original lists Documents/Computer/drives at
  //     the bottom; on Linux we keep ONLY the faithful, non-Windows recent list).
  if (_historyProvider)
  {
    const UStringVector hist = _historyProvider();
    if (hist.Size() != 0)
    {
      if (!menu.isEmpty())
        menu.addSeparator();
      FOR_VECTOR (i, hist)
      {
        const UString &full = hist[i];
        QAction *act = menu.addAction(UStr_toQ(full));
        act->setData(UStr_toQ(full));
      }
    }
  }

  if (menu.isEmpty())
    return;
  QAction *chosen = menu.exec(_addressDropButton->mapToGlobal(
      QPoint(0, _addressDropButton->height())));
  if (!chosen)
    return;
  const UString target = Q_toU(chosen->data().toString());
  navigateToFsPath(target);   // BindToPathAndRefresh analogue
}
