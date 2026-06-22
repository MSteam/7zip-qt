// QtLinkDialog.cpp
// ----------------------------------------------------------------------------
// See QtLinkDialog.h. The dialog only COLLECTS from/to/kind (mirroring
// CLinkDialog's controls + OnInit prefill); the actual link(2)/symlink(2) is done
// by the window's doLink() (QtFileManagerWindow), mirroring how CApp::Link drives
// CLinkDialog then the creation runs in OnButton_Link.
// ----------------------------------------------------------------------------

#include "QtLinkDialog.h"
#include "../QtLang.h"                              // P.2 : FmLang
#include "../../FileManager/LinkDialogRes.h"        // P.2 : IDD_LINK=7700, IDT_LINK_PATH_*, IDR_LINK_TYPE_HARD, IDB_LINK_LINK
#include "../../FileManager/CopyDialogRes.h"        // G.1 : IDS_SET_FOLDER=6007 (browse title)

#include "../../../../Windows/FileName.h"   // NName::IsAbsolutePath

#include "../../../../Windows/FileFind.h"   // LINK-TARGET : CFileInfo::Find + IsOsSymLink
#include "../../../../Common/StringConvert.h" // LINK-TARGET : GetUnicodeString/GetAnsiString

#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <unistd.h>   // LINK-TARGET : readlink(2)
#include <climits>    // LINK-TARGET : PATH_MAX

using namespace NWindows;
using namespace NWindows::NFile;

namespace {

UString QStr_to_UString(const QString &s)
{
  UString u;
  const int n = s.size();
  for (int i = 0; i < n; i++)
    u += (wchar_t)s.at(i).unicode();
  return u;
}

QString UString_to_QStr(const UString &u)
{
  QString s;
  for (unsigned i = 0; i < u.Len(); i++)
    s.append(QChar((char16_t)(unsigned)u[i]));
  return s;
}

} // namespace


QtLinkDialog::QtLinkDialog(QWidget *parent)
  : QDialog(parent)
  , From()
  , To()
  , Kind(Symbolic)
  , _from(nullptr)
  , _to(nullptr)
  , _symbolic(nullptr)
  , _hard(nullptr)
  , _curTarget(nullptr)
{
  // P.2 : fallback = the upstream .rc CAPTION verbatim ("Link", LinkDialog.rc:11), not "Create Link".
  setWindowTitle(FmLang(IDD_LINK, QStringLiteral("Link")));   // P.2 : 7700 caption

  QVBoxLayout *top = new QVBoxLayout(this);
  QFormLayout *form = new QFormLayout();

  // from : the link to create (IDC_LINK_PATH_FROM) + browse.
  QHBoxLayout *fromRow = new QHBoxLayout();
  _from = new QLineEdit(this);
  fromRow->addWidget(_from);
  QToolButton *bFrom = new QToolButton(this);
  bFrom->setText(QStringLiteral("..."));
  connect(bFrom, &QToolButton::clicked, this, &QtLinkDialog::onBrowseFrom);
  fromRow->addWidget(bFrom);
  // G.1 : IDT_LINK_PATH_FROM=7702 ("Link from:" label, LinkDialog.cpp kLangIDs)
  form->addRow(FmLang(IDT_LINK_PATH_FROM, QStringLiteral("Link path:")), fromRow);

  // to : the target it points at (IDC_LINK_PATH_TO) + browse.
  QHBoxLayout *toRow = new QHBoxLayout();
  _to = new QLineEdit(this);
  toRow->addWidget(_to);
  QToolButton *bTo = new QToolButton(this);
  bTo->setText(QStringLiteral("..."));
  connect(bTo, &QToolButton::clicked, this, &QtLinkDialog::onBrowseTo);
  toRow->addWidget(bTo);
  // G.1 : IDT_LINK_PATH_TO=7703 ("Link to:" label, LinkDialog.cpp kLangIDs)
  form->addRow(FmLang(IDT_LINK_PATH_TO, QStringLiteral("Link target:")), toRow);

  // LINK-TARGET : the existing symlink's CURRENT target (IDT_LINK_PATH_TO_CUR). The
  // upstream control is an empty LTEXT filled at OnInit; it has no label langID of its
  // own, so use a plain port-specific row label (mirroring the "Symbolic link" literal).
  // Empty/hidden unless fillFromState() finds the focused item is already a symlink.
  _curTarget = new QLabel(this);
  _curTarget->setTextInteractionFlags(Qt::TextSelectableByMouse);
  _curTarget->setVisible(false);
  form->addRow(QStringLiteral("Current target:"), _curTarget);

  top->addLayout(form);

  // Link-type radios (the Linux subset of CLinkDialog's group).
  QButtonGroup *group = new QButtonGroup(this);
  // "Symbolic link" is a port-specific collapsed label: upstream splits symbolic
  // into IDR_LINK_TYPE_SYM_FILE ("File Symbolic Link") and IDR_LINK_TYPE_SYM_DIR
  // ("Directory Symbolic Link"); there is no single "Symbolic link" radio langID,
  // so keep a plain literal (do not invent an id).
  _symbolic = new QRadioButton(QStringLiteral("Symbolic link"), this);
  // G.1 : IDR_LINK_TYPE_HARD=7711 ("Hard Link" radio, LinkDialog.cpp kLangIDs)
  _hard = new QRadioButton(FmLang(IDR_LINK_TYPE_HARD, QStringLiteral("Hard link")), this);
  group->addButton(_symbolic, Symbolic);
  group->addButton(_hard, Hard);
  _symbolic->setChecked(true);
  top->addWidget(_symbolic);
  top->addWidget(_hard);

  QDialogButtonBox *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // G.1 : upstream's accept button is the DEFPUSHBUTTON "Link" (IDB_LINK_LINK=7701,
  //        LinkDialog.cpp kLangIDs); Cancel uses langID 402 (kLangPairs). Override
  //        the standard-button text so a loaded txt translates them.
  if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
    ok->setText(FmLang(IDB_LINK_LINK, QStringLiteral("Link")));
  if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
    cancel->setText(FmLang(402, QStringLiteral("Cancel")));
  connect(buttons, &QDialogButtonBox::accepted, this, &QtLinkDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  top->addWidget(buttons);
}

void QtLinkDialog::fillFromState()
{
  // LINK-TARGET : OnInit (LinkDialog.cpp:97-176). When the focused item is ALREADY a
  // symlink, the Windows path reads its target via CReparseAttr/GetSymLink and shows it
  // in IDT_LINK_PATH_TO_CUR, prefilling from = FilePath / to = the existing target. The
  // junction/WSL/reparse machinery is Windows-only; the POSIX analogue is lstat (Find,
  // followLink=false) + readlink(2). When NOT a symlink we keep the non-reparse branch:
  // from = AnotherPath (the new link in the other panel), to = FilePath (the focused item).
  CurrentTarget.Empty();
  bool isSymlink = false;
  if (!FilePath.IsEmpty())
  {
    NWindows::NFile::NFind::CFileInfo fi;
    if (fi.Find(us2fs(FilePath), /*followLink*/ false) && fi.IsOsSymLink())
    {
      char buf[PATH_MAX + 1];
      const AString pathA = GetAnsiString(FilePath);
      const ssize_t n = ::readlink(pathA.Ptr(), buf, sizeof(buf) - 1);
      if (n >= 0)
      {
        buf[n] = 0;
        CurrentTarget = GetUnicodeString(AString(buf));
        isSymlink = true;
      }
    }
  }

  if (isSymlink)
  {
    // LinkDialog.cpp:138-140 : from = FilePath (the existing link), to = its target.
    _from->setText(UString_to_QStr(FilePath));
    _to->setText(UString_to_QStr(CurrentTarget));
    if (_curTarget)
    {
      _curTarget->setText(UString_to_QStr(CurrentTarget));   // IDT_LINK_PATH_TO_CUR
      _curTarget->setVisible(true);
    }
    // A POSIX symlink maps to the Symbolic radio (file/dir symbolic collapsed).
    if (_symbolic)
      _symbolic->setChecked(true);
  }
  else
  {
    // OnInit's non-reparse branch: create a new link in the OTHER panel (from =
    // AnotherPath) pointing at the focused file (to = FilePath).
    _from->setText(UString_to_QStr(AnotherPath));
    _to->setText(UString_to_QStr(FilePath));
    if (_curTarget)
      _curTarget->setVisible(false);
  }
}

void QtLinkDialog::onBrowseFrom()
{
  // G.1 : both browse buttons use IDS_SET_FOLDER=6007 in upstream OnButton_SetPath.
  const QString p = QFileDialog::getSaveFileName(this,
      FmLang(IDS_SET_FOLDER, QStringLiteral("Specify a location for output folder")),
      _from->text(), QString(), nullptr, QFileDialog::DontConfirmOverwrite);
  if (!p.isEmpty())
    _from->setText(p);
}

void QtLinkDialog::onBrowseTo()
{
  // G.1 : both browse buttons use IDS_SET_FOLDER=6007 in upstream OnButton_SetPath.
  const QString p = QFileDialog::getOpenFileName(this,
      FmLang(IDS_SET_FOLDER, QStringLiteral("Specify a location for output folder")),
      _to->text());
  if (!p.isEmpty())
    _to->setText(p);
}

void QtLinkDialog::onAccept()
{
  // OnButton_Link prologue (LinkDialog.cpp:262-269): read from/to; if `from` is
  // not absolute, prepend CurDirPrefix.
  From = QStr_to_UString(_from->text());
  To = QStr_to_UString(_to->text());
  if (From.IsEmpty())
    return;
  if (!NName::IsAbsolutePath(From))
    From.Insert(0, CurDirPrefix);

  Kind = _hard->isChecked() ? Hard : Symbolic;
  accept();
}
