// QtArchiveBrowser.h
// ----------------------------------------------------------------------------
// Milestone B.1 : a minimal navigable viewer that proves QtFolderModel.
//
// A QMainWindow with:
//   * a QTreeView (flat, sortable, with a header) bound to a folders-first
//     QSortFilterProxyModel over a QtFolderModel,
//   * an address/path label (kpidPath of the current folder),
//   * an "Up" toolbar action  -> QtFolderModel::goParent(),
//   * double-click a row       -> QtFolderModel::enterItem(row) (into folders).
//
// It is handed an already-opened root IFolderFolder (from a CAgent the harness
// creates); it does NOT open the archive itself.
//
// The proxy's lessThan() implements CPanel's "folders first, then by the sort
// column" ordering (PanelSort.cpp): directories sort before files regardless of
// column/order, and within a group names compare via the natural-order
// CompareFileNames_ForFolderList (reused from agent/FmCompareCompat.cpp).
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_ARCHIVE_BROWSER_H
#define ZIP7_INC_QT_FM_ARCHIVE_BROWSER_H

#include <QtWidgets/QMainWindow>
#include <QtCore/QSortFilterProxyModel>

#include "../../../PropID.h"          // B.7b : PROPID / kpidNoProperty
#include "QtFolderModel.h"

class QTreeView;
class QLabel;

// Folders-first proxy (mirrors CPanel's "is folder first" ordering).
class QtFolderSortProxy Z7_final : public QSortFilterProxyModel
{
  Q_OBJECT
public:
  explicit QtFolderSortProxy(QtFolderModel *src, QObject *parent = nullptr);

  // B.7b : "Unsorted" arrange-by (CPanel kpidNoProperty / CompareItems2's
  // MyCompare(lParam1,lParam2) raw-load-order branch). When enabled, lessThan()
  // ignores the active column's key and compares by SOURCE row (the engine's
  // natural load order), still folders-first, still ascending/descending-aware.
  void setUnsortedMode(bool on)
  {
    if (_unsorted == on)
      return;
    _unsorted = on;
    invalidate();
  }
  bool unsortedMode() const { return _unsorted; }

  // B.7b : arrange-by drives the sort KEY explicitly (CPanel's _sortID), so Type
  // (kpidExtension) and Date (kpidMTime) sort faithfully even when no such column
  // is visible. kpidNoProperty (the default) means "use the active column's
  // PROPID", preserving the original click-the-header behavior.
  void setSortKey(PROPID propID)
  {
    if (_sortKey == propID)
      return;
    _sortKey = propID;
    invalidate();   // re-run lessThan with the new key even if column/order match
  }
  PROPID sortKey() const { return _sortKey; }

protected:
  bool lessThan(const QModelIndex &a, const QModelIndex &b) const override;
private:
  // The effective per-row key PROPID: the explicit _sortKey if set, else the
  // active column's PROPID (header-click compatibility).
  PROPID effectiveKey(const QModelIndex &a) const;

  QtFolderModel *_src;
  bool _unsorted = false;
  PROPID _sortKey = kpidNoProperty;   // kpidNoProperty == "use the column"
};

class QtArchiveBrowser Z7_final : public QMainWindow
{
  Q_OBJECT
public:
  // rootFolder : an already-opened archive root IFolderFolder.
  // title      : usually the archive file path (for the window title).
  explicit QtArchiveBrowser(IFolderFolder *rootFolder,
      const QString &title, QWidget *parent = nullptr);

private Q_SLOTS:
  void onUp();
  void onDoubleClicked(const QModelIndex &proxyIndex);
  void refreshPathLabel();

private:
  QtFolderModel *_model;
  QtFolderSortProxy *_proxy;
  QTreeView *_view;
  QLabel *_pathLabel;
};

#endif
