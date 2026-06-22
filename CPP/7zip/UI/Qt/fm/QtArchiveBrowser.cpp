// QtArchiveBrowser.cpp
// ----------------------------------------------------------------------------
// See QtArchiveBrowser.h.
// ----------------------------------------------------------------------------

#include "QtArchiveBrowser.h"

#include <QtWidgets/QTreeView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtGui/QAction>

#include "../../../../Common/MyString.h"
#include "../../../../Common/Defs.h"          // G.4m : MyCompare (vt tie-break)
#include "../../../../Windows/PropVariant.h"  // G.4m : NCOM::CPropVariant raw sort key
#include "../../../PropID.h"          // kpidName / kpidSize / kpidExtension / kpidMTime

using namespace NWindows;

// Natural-order folder-list name compare (reused from agent/FmCompareCompat.cpp,
// itself copied verbatim from FileManager/PanelSort.cpp).
int CompareFileNames_ForFolderList(const wchar_t *s1, const wchar_t *s2);

// B.7b : GetExtensionPtr (PanelSort.cpp:77) — pointer from the last dot (incl.
// the dot), or end-of-string if no dot. Used by the Type (kpidExtension) sort.
static const wchar_t *GetExtensionPtr_b7b(const UString &name)
{
  const int dotPos = name.ReverseFind_Dot();
  return name.Ptr(dotPos < 0 ? name.Len() : (unsigned)dotPos);
}

// ---------------------------------------------------------------------------
// QtFolderSortProxy : folders-first ordering (mirrors CPanel::CompareItems,
// PanelSort.cpp): directories always sort before files regardless of the sort
// column/order; within a group the active column compares, falling back to the
// natural-order name compare.
// ---------------------------------------------------------------------------

QtFolderSortProxy::QtFolderSortProxy(QtFolderModel *src, QObject *parent)
  : QSortFilterProxyModel(parent)
  , _src(src)
{
  setSourceModel(src);
}

bool QtFolderSortProxy::lessThan(const QModelIndex &a, const QModelIndex &b) const
{
  const int rowA = a.row();
  const int rowB = b.row();

  // 0) G.4f : the synthetic ".." parent pseudo-row ALWAYS sorts first (model row 0
  //    at the TOP of the view), regardless of the active sort column / direction —
  //    a faithful port of CompareItems' kParentIndex short-circuit (PanelSort.cpp:
  //    183-184: `if (lParam1==kParentIndex) return -1; if (lParam2==kParentIndex)
  //    return 1;`). Qt negates lessThan() for a descending sort, so we return a
  //    value relative to the requested order to pin ".." on top in BOTH directions.
  const bool parentA = _src->isParentRow(rowA);
  const bool parentB = _src->isParentRow(rowB);
  if (parentA != parentB)
  {
    // The parent must come first. In ascending, "parent < other" == (parentA).
    // In descending Qt flips the result, so we flip our answer too so ".." stays
    // visually first.
    return (sortOrder() == Qt::AscendingOrder) ? parentA : parentB;
  }
  // (parentA && parentB can't happen: there is only one ".." row.)

  // 1) folders before files (independent of ascending/descending, exactly as
  //    CompareItems returns before applying the _ascending negation).
  const bool dirA = _src->isFolder(rowA);
  const bool dirB = _src->isFolder(rowB);
  if (dirA != dirB)
  {
    // Qt negates lessThan() for descending sort; to keep folders pinned on top
    // in BOTH orders we compare against the requested sort order.
    const bool foldersFirst = (sortOrder() == Qt::AscendingOrder) ? dirA : dirB;
    return foldersFirst;
  }

  // 1b) B.7b "Unsorted" (kpidNoProperty): within a group, compare by SOURCE row
  //     (the engine's load order, == CompareItems2's MyCompare(lParam1,lParam2)).
  //     Folders-first above still holds; Qt negates this for descending.
  if (_unsorted)
    return rowA < rowB;

  // 2) within a group: compare by the effective key (CPanel's CompareItems2).
  const PROPID propID = effectiveKey(a);

  // kpidName : natural-order CompareFileNames_ForFolderList (CPanel's path).
  if (propID == kpidName)
  {
    const UString n1 = _src->itemName(rowA);
    const UString n2 = _src->itemName(rowB);
    return CompareFileNames_ForFolderList(n1.Ptr(), n2.Ptr()) < 0;
  }

  // kpidExtension (Type) : natural compare of GetExtensionPtr (from last dot),
  //   name as tie-break (CompareItems2, PanelSort.cpp).
  if (propID == kpidExtension)
  {
    const UString n1 = _src->itemName(rowA);
    const UString n2 = _src->itemName(rowB);
    const int c = CompareFileNames_ForFolderList(
        GetExtensionPtr_b7b(n1), GetExtensionPtr_b7b(n2));
    if (c != 0)
      return c < 0;
    return CompareFileNames_ForFolderList(n1.Ptr(), n2.Ptr()) < 0;
  }

  // kpidSize : numeric compare on the raw kpidSize value, name as tie-break.
  if (propID == kpidSize)
  {
    bool d1 = false, d2 = false;
    const UInt64 s1 = _src->itemSize(rowA, d1);
    const UInt64 s2 = _src->itemSize(rowB, d2);
    if (s1 != s2)
      return s1 < s2;
    return CompareFileNames_ForFolderList(
        _src->itemName(rowA).Ptr(), _src->itemName(rowB).Ptr()) < 0;
  }

  // G.4m : every OTHER key (Date/time, CRC, numeric props, …) compares the RAW
  // PropVariant by its native VARTYPE — a faithful port of CPanel::CompareItems2
  // (PanelSort.cpp:168-176), NOT the formatted column text. The previous lexical
  // QString::compare of the displayed cell only happened to be correct for FS
  // ISO-8601 dates (lexical == chronological); an archive's locale/short-form
  // Modified column (or any prop that doesn't format monotonically) sorted wrong.
  //
  //   if (prop1.vt != prop2.vt) return MyCompare(prop1.vt, prop2.vt);   // mixed types
  //   if (prop1.vt == VT_BSTR)  return MyStringCompareNoCase(...);      // genuine strings
  //   return prop1.Compare(prop2);   // VT_FILETIME->CompareFileTime, numeric->MyCompare
  //
  // Name is the tie-break (CompareItems's iter loop falls back to kpidName).
  {
    NCOM::CPropVariant prop1, prop2;
    _src->getRawProperty(rowA, propID, prop1);
    _src->getRawProperty(rowB, propID, prop2);
    int c;
    if (prop1.vt != prop2.vt)
      c = MyCompare(prop1.vt, prop2.vt);
    else if (prop1.vt == VT_BSTR)
      c = MyStringCompareNoCase(prop1.bstrVal, prop2.bstrVal);
    else
      c = prop1.Compare(prop2);
    if (c != 0)
      return c < 0;
  }
  return CompareFileNames_ForFolderList(
      _src->itemName(rowA).Ptr(), _src->itemName(rowB).Ptr()) < 0;
}

PROPID QtFolderSortProxy::effectiveKey(const QModelIndex &a) const
{
  // arrange-by sets _sortKey explicitly; a header click leaves it kpidNoProperty
  // and we use the clicked column's PROPID (the original click-to-sort path).
  if (_sortKey != kpidNoProperty)
    return _sortKey;
  return _src->columnPropID(a.column());
}

// ---------------------------------------------------------------------------

QtArchiveBrowser::QtArchiveBrowser(IFolderFolder *rootFolder,
    const QString &title, QWidget *parent)
  : QMainWindow(parent)
{
  setWindowTitle(title.isEmpty() ? QStringLiteral("7zqt archive browser") : title);
  resize(840, 560);

  _model = new QtFolderModel(this);
  _proxy = new QtFolderSortProxy(_model, this);

  // Toolbar: "Up" -> goParent (mirrors CPanel's parent-folder toolbar button).
  QToolBar *tb = addToolBar(QStringLiteral("nav"));
  QAction *upAct = tb->addAction(QStringLiteral("Up"));
  upAct->setShortcut(QKeySequence(Qt::Key_Backspace));
  connect(upAct, &QAction::triggered, this, &QtArchiveBrowser::onUp);

  // Central widget: address/path label above the tree view.
  QWidget *central = new QWidget(this);
  QVBoxLayout *lay = new QVBoxLayout(central);
  lay->setContentsMargins(4, 4, 4, 4);

  _pathLabel = new QLabel(central);
  _pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  lay->addWidget(_pathLabel);

  _view = new QTreeView(central);
  _view->setModel(_proxy);
  _view->setRootIsDecorated(false);     // flat list, not a nested tree
  _view->setSortingEnabled(true);
  _view->setUniformRowHeights(true);
  _view->setSelectionBehavior(QAbstractItemView::SelectRows);
  _view->setAlternatingRowColors(true);
  _view->sortByColumn(0, Qt::AscendingOrder); // default: sort by Name
  lay->addWidget(_view);

  setCentralWidget(central);

  connect(_view, &QTreeView::doubleClicked,
      this, &QtArchiveBrowser::onDoubleClicked);
  connect(_model, &QAbstractItemModel::modelReset,
      this, &QtArchiveBrowser::refreshPathLabel);
  // G.4f : a ".." double-click activates the parent navigation (the model emits
  // parentActivated rather than binding a child). Route it to Up.
  connect(_model, &QtFolderModel::parentActivated,
      this, &QtArchiveBrowser::onUp);

  // Hand the model the already-opened root folder (controller-supplied).
  _model->setRootFolder(rootFolder);

  if (_view->header()->count() > 0)
    _view->header()->resizeSection(0, 280);
  refreshPathLabel();
}

void QtArchiveBrowser::onUp()
{
  _model->goParent();
}

void QtArchiveBrowser::onDoubleClicked(const QModelIndex &proxyIndex)
{
  if (!proxyIndex.isValid())
    return;
  const QModelIndex srcIndex = _proxy->mapToSource(proxyIndex);
  // enterItem() into a folder; a leaf file double-click is a no-op in this minimal
  // standalone B.1 viewer. (Seamless nested-archive entry IS implemented in the FM's
  // QtPanel::tryOpenNestedArchive, G.2b — this lightweight browser just doesn't reuse it.)
  _model->enterItem(srcIndex.row());
}

void QtArchiveBrowser::refreshPathLabel()
{
  const UString p = _model->currentPath();
  const QString qp = QString::fromWCharArray(p.Ptr(), (int)p.Len());
  _pathLabel->setText(QStringLiteral("Path: ") + qp);
}
