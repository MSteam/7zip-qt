// QtLinkDialog.h
// ----------------------------------------------------------------------------
// B.7c : Qt/Linux mirror of FileManager/LinkDialog.{h,cpp}'s CLinkDialog.
//
// The Win32 original supports HARD / SYM_FILE / SYM_DIR / JUNCTION / WSL link
// types via NTFS reparse points. On Linux there is no reparse machinery, so per
// the B.7c brief this collapses to two POSIX primitives:
//   HARD     -> link(2)     (via NDir::MyCreateHardLink, the engine's portable wrap)
//   SYMBOLIC -> symlink(2)  (target = "to", linkpath = "from")
// (file/dir symbolic collapse to one option; junction/WSL are Windows-only.)
//
// Public fields mirror CLinkDialog (LinkDialog.h:26-28): CurDirPrefix / FilePath /
// AnotherPath. OnInit's non-reparse branch sets from = AnotherPath (the new link
// in the other panel) and to = FilePath (the existing focused item).
//
// Widgets:
//   _from   QLineEdit   (IDC_LINK_PATH_FROM : the link to CREATE)
//   _to     QLineEdit   (IDC_LINK_PATH_TO   : the target it points AT)
//   radios  Symbolic / Hard  (the Linux-relevant subset of the type group)
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_LINK_DIALOG_H
#define ZIP7_INC_QT_FM_LINK_DIALOG_H

#include "../../../../Common/MyString.h"

#include <QtWidgets/QDialog>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QRadioButton;
class QLabel;
QT_END_NAMESPACE


class QtLinkDialog : public QDialog
{
  Q_OBJECT

public:
  // Link kinds (the Linux subset of CLinkDialog's radio group).
  enum LinkKind { Symbolic = 0, Hard = 1 };

  explicit QtLinkDialog(QWidget *parent = nullptr);

  // OnInit mirror: caller sets the fields, then calls fillFromState().
  void fillFromState();

  // === public data : MIRRORS CLinkDialog (LinkDialog.h:26-28) =================
  UString CurDirPrefix;   // the focused panel's FS dir (prefix for a relative from)
  UString FilePath;       // the existing focused item (default link TARGET = to)
  UString AnotherPath;    // the other panel's dir   (default link to CREATE = from)

  // Results read by the window after accept() (mirror OnButton_Link's locals).
  UString From;           // the link path to create
  UString To;             // the target
  int Kind;               // LinkKind

  // LINK-TARGET (LinkDialog.cpp:97-176) : when FilePath is ALREADY a symlink, OnInit
  // displays its CURRENT target (IDT_LINK_PATH_TO_CUR). fillFromState() reads it via
  // readlink(2) (the POSIX analogue of CReparseAttr::GetPath) and stores it here for
  // the read-only "current target" label and the headless read-back. Empty when the
  // focused item is not an existing symlink.
  UString CurrentTarget;

private slots:
  void onBrowseFrom();
  void onBrowseTo();
  void onAccept();        // mirrors OnButton_Link (reads from/to/kind)

private:
  QLineEdit *_from;
  QLineEdit *_to;
  QRadioButton *_symbolic;
  QRadioButton *_hard;
  QLabel *_curTarget;     // LINK-TARGET : the existing symlink's current target display
};

#endif
