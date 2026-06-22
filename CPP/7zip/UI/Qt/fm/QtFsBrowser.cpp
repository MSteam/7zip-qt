// QtFsBrowser.cpp
// ----------------------------------------------------------------------------
// See QtFsBrowser.h.
// ----------------------------------------------------------------------------

#include "QtFsBrowser.h"

#include <QtWidgets/QTreeView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtGui/QAction>

#include "../../../../Common/MyString.h"
#include "../../../PropID.h"          // kpidName / kpidPath

#include "QtArchiveBrowser.h"         // reuse QtFolderSortProxy (folders-first)
#include "ArchiveOpenHelper.h"

// ---------------------------------------------------------------------------

static QString UStr_toQ(const UString &s)
{
  return QString::fromWCharArray(s.Ptr(), (int)s.Len());
}

QtFsBrowser::QtFsBrowser(IFolderFolder *rootFolder, bool isFsRoot,
    const QString &title, QWidget *parent)
  : QMainWindow(parent)
  , _baseTitle(title)
  , _inArchive(!isFsRoot)
{
  resize(900, 600);

  _model = new QtFolderModel(this);
  _proxy = new QtFolderSortProxy(_model, this);

  // Toolbar: "Up" -> onUp (goParent, or exit an archive back to its FS folder).
  QToolBar *tb = addToolBar(QStringLiteral("nav"));
  QAction *upAct = tb->addAction(QStringLiteral("Up"));
  upAct->setShortcut(QKeySequence(Qt::Key_Backspace));
  connect(upAct, &QAction::triggered, this, &QtFsBrowser::onUp);

  QWidget *central = new QWidget(this);
  QVBoxLayout *lay = new QVBoxLayout(central);
  lay->setContentsMargins(4, 4, 4, 4);

  _pathLabel = new QLabel(central);
  _pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  lay->addWidget(_pathLabel);

  _view = new QTreeView(central);
  _view->setModel(_proxy);
  _view->setRootIsDecorated(false);
  _view->setSortingEnabled(true);
  _view->setUniformRowHeights(true);
  _view->setSelectionBehavior(QAbstractItemView::SelectRows);
  _view->setAlternatingRowColors(true);
  _view->sortByColumn(0, Qt::AscendingOrder);
  lay->addWidget(_view);

  setCentralWidget(central);

  connect(_view, &QTreeView::doubleClicked,
      this, &QtFsBrowser::onDoubleClicked);
  connect(_model, &QtFolderModel::leafActivated,
      this, &QtFsBrowser::onLeafActivated);
  connect(_model, &QAbstractItemModel::modelReset,
      this, &QtFsBrowser::refreshPathLabel);

  _model->setRootFolder(rootFolder);

  if (_view->header()->count() > 0)
    _view->header()->resizeSection(0, 320);
  refreshPathLabel();
  updateTitle();
}

void QtFsBrowser::onUp()
{
  // First try a normal parent bind inside the current folder tree.
  if (_model->goParent())
  {
    refreshPathLabel();
    updateTitle();
    return;
  }

  // goParent returned false: we are at the root of the current folder. If that
  // root is an archive we entered FROM a filesystem folder, exit the archive and
  // restore that FS folder (the seamless transition's "Up exits the archive").
  if (_inArchive && _fsReturnFolder)
  {
    CMyComPtr<IFolderFolder> fsFolder = _fsReturnFolder;
    _model->setRootFolder(fsFolder);
    _inArchive = false;
    _fsReturnFolder.Release();
    _agentKeepAlive.Release(); // archive no longer shown; release the CAgent
    refreshPathLabel();
    updateTitle();
  }
}

void QtFsBrowser::onDoubleClicked(const QModelIndex &proxyIndex)
{
  if (!proxyIndex.isValid())
    return;
  const QModelIndex srcIndex = _proxy->mapToSource(proxyIndex);
  // enterItem() navigates into a subdirectory; for a leaf file it emits
  // leafActivated() (-> onLeafActivated), which attempts the archive open.
  if (_model->enterItem(srcIndex.row()))
  {
    refreshPathLabel();
    updateTitle();
  }
}

void QtFsBrowser::onLeafActivated(int row)
{
  // A file (leaf) was activated. If we are in the FS view, try to open it as an
  // archive (the seamless filesystem -> archive transition). Inside an archive,
  // leaf activation would mean "open the contained file"; that is out of scope
  // for B.2 (deferred), so we ignore it there.
  if (_inArchive)
    return;
  tryOpenAsArchive(row);
}

bool QtFsBrowser::tryOpenAsArchive(int row)
{
  // Build the leaf's absolute filesystem path: current FS folder path + name.
  // currentPath() reads the CFSFolder's kpidPath (always ends with a separator).
  const UString folderPath = _model->currentPath();
  const UString name = _model->itemName(row);
  if (name.IsEmpty())
    return false;
  const UString fullPath = folderPath + name;

  CMyComPtr<IFolderFolder> archiveRoot;
  CMyComPtr<IUnknown> agentHolder;
  const HRESULT res = OpenArchiveAsFolder(fullPath, UString(), false,
      archiveRoot, agentHolder);
  if (res != S_OK || !archiveRoot)
    return false; // not an archive: a plain file double-click is a no-op (B.2)

  // Remember the FS folder to return to BEFORE swapping the model root, so "Up"
  // out of the archive lands back in this directory. We capture a strong ref to
  // the model's current folder; setRootFolder() will drop the model's own ref,
  // but ours keeps the FS CFSFolder alive (and at the same directory) for the
  // return. (This is exactly the IFolderFolder the model is currently showing.)
  CMyComPtr<IFolderFolder> fsReturn = _model->currentFolder();
  if (!fsReturn)
    return false;

  _fsReturnFolder = fsReturn;
  _agentKeepAlive = agentHolder;
  _inArchive = true;
  _model->setRootFolder(archiveRoot);
  refreshPathLabel();
  updateTitle();
  return true;
}

void QtFsBrowser::refreshPathLabel()
{
  const UString p = _model->currentPath();
  QString qp = UStr_toQ(p);
  if (qp.isEmpty())
    qp = QStringLiteral("(root)");
  _pathLabel->setText((_inArchive ? QStringLiteral("Archive: ")
                                   : QStringLiteral("Path: ")) + qp);
}

void QtFsBrowser::updateTitle()
{
  const UString p = _model->currentPath();
  QString loc = UStr_toQ(p);
  QString prefix = _inArchive ? QStringLiteral("[archive] ") : QString();
  if (loc.isEmpty())
    setWindowTitle(_baseTitle);
  else
    setWindowTitle(prefix + loc + QStringLiteral("  -  ") + _baseTitle);
}
