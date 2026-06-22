// QtFsBrowser.h
// ----------------------------------------------------------------------------
// Milestone B.2 : a navigable file-manager window that binds QtFolderModel to a
// UNIFIED IFolderFolder seam - either the real FILESYSTEM (CFSFolder, the B.2
// Linux-ported engine folder) OR an archive (CAgent's root, the B.0/B.1 seam) -
// and wires the SEAMLESS filesystem -> archive transition the original CPanel
// performs (PanelFolderChange.cpp / ::OpenItemAsArchive):
//
//   * double-click a SUBDIRECTORY  -> QtFolderModel::enterItem (BindToFolder),
//   * double-click a FILE that is an ARCHIVE -> open it with CAgent and SWAP the
//     SAME model's root folder to the archive's root (uniform IFolderFolder),
//   * "Up" from the archive root    -> return to the containing FS directory
//     (the transition is tracked so Up exits the archive back to its folder).
//
// This reuses QtFolderModel UNCHANGED except for the new leafActivated(row)
// signal (the controller hook for "a leaf file was activated"). The folders-first
// sort proxy is the same QtFolderSortProxy used by the archive-only browser.
//
// It is a sibling of QtArchiveBrowser (B.1, archive-only) rather than a
// modification of it, so the existing archive-only 7zqt_browser keeps working
// verbatim.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_FS_BROWSER_H
#define ZIP7_INC_QT_FM_FS_BROWSER_H

#include <QtWidgets/QMainWindow>

#include "../../../../Common/MyCom.h"
#include "../../../../Common/MyString.h"
#include "../../FileManager/IFolder.h"

#include "QtFolderModel.h"

class QTreeView;
class QLabel;
class QtFolderSortProxy;

class QtFsBrowser Z7_final : public QMainWindow
{
  Q_OBJECT
public:
  // rootFolder : an already-bound root IFolderFolder. For B.2 this is normally a
  // CFSFolder rooted at a directory; it may also be an archive root (then the
  // browser starts inside an archive, with no FS folder to return to).
  // isFsRoot   : true if rootFolder is a filesystem CFSFolder (enables the
  //              seamless FS->archive open on leaf activation).
  explicit QtFsBrowser(IFolderFolder *rootFolder, bool isFsRoot,
      const QString &title, QWidget *parent = nullptr);

private Q_SLOTS:
  void onUp();
  void onDoubleClicked(const QModelIndex &proxyIndex);
  void onLeafActivated(int row);
  void refreshPathLabel();

private:
  // Try to open the FS leaf at source-row `row` as an archive. On success swaps
  // the model root to the archive root and records the return point. Returns true
  // if the leaf was an archive (and we transitioned into it).
  bool tryOpenAsArchive(int row);
  void updateTitle();

  QtFolderModel *_model;
  QtFolderSortProxy *_proxy;
  QTreeView *_view;
  QLabel *_pathLabel;

  QString _baseTitle;

  // --- seamless FS->archive transition state ----------------------------
  // When we open an archive from the FS view we keep the CAgent alive and remember
  // the FS folder to return to when the user goes Up out of the archive root.
  CMyComPtr<IFolderFolder> _fsReturnFolder; // FS folder containing the archive
  CMyComPtr<IUnknown> _agentKeepAlive;      // holds the CAgent while in-archive
  bool _inArchive;                          // currently showing an archive?
};

#endif
