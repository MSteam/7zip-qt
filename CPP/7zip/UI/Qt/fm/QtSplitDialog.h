// QtSplitDialog.h
// ----------------------------------------------------------------------------
// B.7c : Qt/Linux mirror of FileManager/SplitDialog.{h,cpp}'s CSplitDialog.
//
// FIDELITY: the public fields reproduce CSplitDialog's (FilePath, Path,
// VolumeSizes). OnInit() is mirrored by the constructor (it fills the volume
// combo from k_Sizes via AddVolumeItems, selects index 0). OnOK() is mirrored by
// accept(): trim the volume text, ParseVolumeSizes() into VolumeSizes, and reject
// (keep open) on an empty/invalid result — exactly SplitDialog.cpp:102-114.
//
// Widgets mirror the Win32 controls:
//   _path     QLineEdit   (IDE_SPLIT_PATH, the output dir; "..." browses)
//   _volume   QComboBox   (IDC_SPLIT_VOLUME, editable, seeded from k_Sizes)
//   "Split file: <name>"  QLabel (the read-only source-file label)
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_SPLIT_DIALOG_H
#define ZIP7_INC_QT_FM_SPLIT_DIALOG_H

#include "../../../../Common/MyString.h"
#include "../../../../Common/MyVector.h"

#include <QtWidgets/QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
QT_END_NAMESPACE

// B.7c : the re-lifted SplitUtils.cpp ParseVolumeSizes, exposed so the headless
// split path (QtFileManagerWindow::doSplit) can parse a volume-size string
// without constructing a dialog. Same canonical byte-for-byte logic.
bool QtSplit_ParseVolumeSizes(const UString &s, CRecordVector<UInt64> &values);


class QtSplitDialog : public QDialog
{
  Q_OBJECT

public:
  explicit QtSplitDialog(QWidget *parent = nullptr);

  // OnInit mirror: the caller sets FilePath / Path AFTER construction, then calls
  // fillFromState() (mirrors CSplitDialog::OnInit, which sets the label + dir).
  void fillFromState();

  // === public data : MIRRORS CSplitDialog (SplitDialog.h:11-26) ===============
  UString FilePath;                  // source file (label only)
  UString Path;                      // output directory
  CRecordVector<UInt64> VolumeSizes; // produced by OnOK / accept()

private slots:
  void onBrowse();                   // mirrors OnButton -> MyBrowseForFolder
  void onAccept();                   // mirrors OnOK()

private:
  QLineEdit *_path;
  QComboBox *_volume;
  class QLabel *_fileLabel;
};

#endif
