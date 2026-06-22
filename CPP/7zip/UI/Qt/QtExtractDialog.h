// QtExtractDialog.h
//
// Qt/Linux mirror of GUI/ExtractDialog.h's CExtractDialog (milestone C.2).
//
// FIDELITY: the public data members reproduce CExtractDialog's exactly (same
// names, same UString/enum/CBoolPair types). OnInit()/OnOK() are mirrored by
// fillFromState() (run in the constructor) and accept() (Qt's OK path), which
// load/store the SAME fields and drive the SAME NExtractQt::CInfo persistence
// that CExtractDialog drives via NExtract::CInfo.
//
// Widgets mirror the Win32 controls:
//   _path          editable QComboBox  (IDC_EXTRACT_PATH, the output dir + MRU)
//   browse "..."   QToolButton         (IDB_EXTRACT_SET_PATH -> OnButtonSetPath)
//   _splitDest     QCheckBox           (IDX_EXTRACT_NAME_ENABLE -> toggle name edit)
//   _pathName      QLineEdit           (IDE_EXTRACT_NAME, the sub-folder name)
//   _arcPath       read-only QLabel    (the archive path, shown in the title)
//   _pathMode      QComboBox           (IDC_EXTRACT_PATH_MODE)
//   _overwriteMode QComboBox           (IDC_EXTRACT_OVERWRITE_MODE)
//   _password      QLineEdit (Password)(IDE_EXTRACT_PASSWORD)
//   _showPassword  QCheckBox           (IDX_PASSWORD_SHOW -> UpdatePasswordControl)
//   _ntSecurity    QCheckBox           (IDX_EXTRACT_NT_SECUR)
//   _elimDup       QCheckBox           (IDX_EXTRACT_ELIM_DUP)

#ifndef ZIP7_INC_QT_EXTRACT_DIALOG_H
#define ZIP7_INC_QT_EXTRACT_DIALOG_H

#include "../../../Common/MyTypes.h"
#include "../../../Common/MyString.h"

#include "../Common/ExtractMode.h"

#include "QtExtractSettings.h"

#include <QtWidgets/QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QCheckBox;
QT_END_NAMESPACE


class QtExtractDialog : public QDialog
{
  Q_OBJECT

public:
  explicit QtExtractDialog(QWidget *parent = nullptr);

  // OnInit mirror. The caller populates the public fields (DirPath, ArcPath,
  // PathMode, etc.) AFTER construction — exactly as ExtractGUI.cpp does before
  // dialog.Create() — then calls fillFromState() to load _info and populate the
  // widgets, then exec(). (CExtractDialog runs OnInit() implicitly from Create();
  // here it is an explicit call so the field-copy-in step can run first.)
  void fillFromState();        // mirrors OnInit()

  // === public data : MIRRORS CExtractDialog (ExtractDialog.h lines 71-92) =====
  UString DirPath;   // output directory (with trailing separator after OnOK)
  UString ArcPath;   // archive path (read-only, shown in window title)
  UString Password;

  bool PathMode_Force;
  bool OverwriteMode_Force;
  NExtract::NPathMode::EEnum PathMode;
  NExtract::NOverwriteMode::EEnum OverwriteMode;

  // NtSecurity mirrors the Win32 "Restore file security" checkbox. It is a
  // Windows NTFS ACL concept; on Linux the engine treats it as a no-op (there is
  // no NT security descriptor to restore), so the checkbox here is kept purely
  // for fidelity / round-tripping of the setting and has limited effect.
  CBoolPair NtSecurity;
  CBoolPair ElimDup;

private slots:
  void onBrowse();             // mirrors OnButtonSetPath()
  void onShowPasswordToggled();// mirrors UpdatePasswordControl()
  void onSplitDestToggled();   // mirrors OnButtonClicked(IDX_EXTRACT_NAME_ENABLE)
  void onAccept();             // mirrors OnOK()

private:
  bool isShowPasswordChecked() const; // mirrors IsShowPasswordChecked()

  // CheckButton_TwoBools / GetButton_Bools helpers (ExtractDialog.cpp 121-133).
  void checkBox_TwoBools(QCheckBox *box, const CBoolPair &b1, const CBoolPair &b2);
  void getBox_Bools(QCheckBox *box, CBoolPair &b1, CBoolPair &b2);

  QComboBox *_path;
  QCheckBox *_splitDest;   // IDX_EXTRACT_NAME_ENABLE (sub-folder name enable)
  QLineEdit *_pathName;    // IDE_EXTRACT_NAME (the sub-folder name)
  QLabel    *_arcPath;
  QComboBox *_pathMode;
  QComboBox *_overwriteMode;
  QLineEdit *_password;
  QCheckBox *_showPassword;
  QCheckBox *_ntSecurity;
  QCheckBox *_elimDup;

  // The persisted settings (mirror CExtractDialog::_info of type NExtract::CInfo).
  NExtractQt::CInfo _info;
};

#endif
