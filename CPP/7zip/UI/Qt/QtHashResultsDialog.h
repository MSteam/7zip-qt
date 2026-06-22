// QtHashResultsDialog.h
//
// Qt/Linux analogue of GUI/HashGUI.cpp's ShowHashResults() (the CListViewDialog
// used to display the name/value result pairs). Mirrors that two-column
// (Property | Value) read-only list: one row per result pair built by
// AddHashBundleRes (Folders/Files/Size + each method's "for data" /
// "for data and names" digest, uppercase hex).
//
// A "Copy" button copies the full "<Name>: <Value>" listing to the clipboard
// (the textual form AddHashBundleRes(UString&, ...) produces in the original).

#ifndef ZIP7_INC_QT_HASH_RESULTS_DIALOG_H
#define ZIP7_INC_QT_HASH_RESULTS_DIALOG_H

#include <QtWidgets/QDialog>

#include "QtHashGUI.h"   // CPropNameValPairs

QT_BEGIN_NAMESPACE
class QTableWidget;
QT_END_NAMESPACE

class QtHashResultsDialog : public QDialog
{
  Q_OBJECT
public:
  // pairs: the AddHashBundleRes output (name -> value rows). Copied in.
  explicit QtHashResultsDialog(const CPropNameValPairs &pairs, QWidget *parent = nullptr);

private slots:
  void onCopy();

private:
  QTableWidget *_table;
};

#endif
