// QtExtractPrompts.cpp
//
// GUI-thread prompt dialogs for the Qt extraction callback. See QtExtractPrompts.h
// for the threading contract. The slots here ALWAYS run on the GUI thread; they
// build a real modal QDialog (the genuine prompt path) and return the user's
// answer through out-parameters.
//
// Headless fallback: when there is no usable display (offscreen / minimal QPA, or
// no DISPLAY/WAYLAND_DISPLAY), showing a modal dialog would block forever with no
// human to answer. In that case the password prompt falls back to the
// SEVENZQT_TEST_PASSWORD environment variable (empty => cancel) and the overwrite
// prompt auto-answers kYes. This keeps the headless test path deterministic while
// preserving the real dialog path whenever a display is present.

#include "QtExtractPrompts.h"

#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include <QtCore/QByteArray>

#include "../Common/IFileExtractCallback.h" // NOverwriteAnswer

#include "QtLang.h"
// Original numeric langIDs (so a loaded Lang/*.txt translates these strings,
// byte-equivalent to upstream LangString(id) with the .rc fallback):
#include "../FileManager/PasswordDialogRes.h"   // IDD_PASSWORD, IDT_PASSWORD_ENTER, IDX_PASSWORD_SHOW
#include "../FileManager/OverwriteDialogRes.h"  // IDD_OVERWRITE, IDT_OVERWRITE_*, IDB_YES_TO_ALL/NO_TO_ALL/AUTO_RENAME

namespace {

// Mirrors main_progress_demo.cpp's headless detection (and the original
// g_DisableUserQuestions intent): true when no interactive display is available.
bool IsHeadless()
{
  const QByteArray qpa = qgetenv("QT_QPA_PLATFORM");
  if (qpa.contains("offscreen") || qpa.contains("minimal"))
    return true;
  return qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty();
}

} // namespace


// === QtPasswordPrompt::Ask =================================================
// GUI-thread analogue of:
//   CPasswordDialog dialog;
//   dialog.ShowPassword = ...;
//   if (dialog.Create(*ProgressDialog) != IDOK) return E_ABORT;  // -> *accepted=false
//   Password = dialog.Password;                                  // -> *password
void QtPasswordPrompt::Ask(QString *password, bool *accepted)
{
  if (IsHeadless())
  {
    // No display: fall back to the test env var so the encrypted-archive path
    // is still exercised. Absent/empty var == user pressed Cancel.
    const QByteArray pw = qgetenv("SEVENZQT_TEST_PASSWORD");
    if (pw.isEmpty())
    {
      *accepted = false;
      return;
    }
    *password = QString::fromUtf8(pw);
    *accepted = true;
    return;
  }

  QDialog dlg(_parentWidget);
  // Caption: original CPasswordDialog::OnInit -> LangSetWindowText(*this, IDD_PASSWORD).
  dlg.setWindowTitle(FmLang(IDD_PASSWORD, QStringLiteral("Enter password")));
  dlg.setModal(true);

  QVBoxLayout *lay = new QVBoxLayout(&dlg);
  // kLangIDs[0] = IDT_PASSWORD_ENTER.
  lay->addWidget(new QLabel(FmLang(IDT_PASSWORD_ENTER, QStringLiteral("Enter password:")), &dlg));

  QLineEdit *edit = new QLineEdit(&dlg);
  edit->setEchoMode(QLineEdit::Password);
  lay->addWidget(edit);

  // kLangIDs[1] = IDX_PASSWORD_SHOW.
  QCheckBox *show = new QCheckBox(FmLang(IDX_PASSWORD_SHOW, QStringLiteral("Show password")), &dlg);
  lay->addWidget(show);
  QObject::connect(show, &QCheckBox::toggled, edit, [edit](bool on) {
    edit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
  });

  QDialogButtonBox *box = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  // OK/Cancel use langIDs 401/402 (kLangPairs, LangUtils.cpp:65-66); English from
  // the dialog-template literal. Override std-button text so a loaded txt translates.
  if (QPushButton *ok = box->button(QDialogButtonBox::Ok))
    ok->setText(FmLang(401, QStringLiteral("OK")));
  if (QPushButton *cancel = box->button(QDialogButtonBox::Cancel))
    cancel->setText(FmLang(402, QStringLiteral("Cancel")));
  lay->addWidget(box);
  QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  edit->setFocus();

  if (dlg.exec() == QDialog::Accepted)
  {
    *password = edit->text();
    *accepted = true;
  }
  else
    *accepted = false;
}


// === QtOverwritePrompt::Ask ================================================
// GUI-thread analogue of COverwriteDialog: show existing vs new file info and the
// six buttons; write the NOverwriteAnswer code into *answer. The button->answer
// mapping mirrors CExtractCallbackImp::AskOverwrite's IDYES/IDNO/IDB_*/IDCANCEL
// switch exactly.
void QtOverwritePrompt::Ask(
    QString existName, QString existSizeStr, QString existTimeStr,
    QString newName,   QString newSizeStr,   QString newTimeStr,
    int *answer)
{
  if (IsHeadless())
  {
    // No display: deterministically overwrite (kYes), so a headless run that hits
    // a conflict still completes rather than hanging on an unanswerable dialog.
    *answer = NOverwriteAnswer::kYes;
    return;
  }

  QDialog dlg(_parentWidget);
  // Caption: original COverwriteDialog::OnInit -> LangSetWindowText(*this, IDD_OVERWRITE).
  dlg.setWindowTitle(FmLang(IDD_OVERWRITE, QStringLiteral("Confirm File Replace")));
  dlg.setModal(true);

  QVBoxLayout *top = new QVBoxLayout(&dlg);
  // Two original strings (kLangIDs[0..1]); .rc text is the English fallback:
  //   IDT_OVERWRITE_HEADER         "Destination folder already contains processed file."
  //   IDT_OVERWRITE_QUESTION_BEGIN "Would you like to replace the existing file"
  top->addWidget(new QLabel(
      FmLang(IDT_OVERWRITE_HEADER,
             QStringLiteral("Destination folder already contains processed file.")), &dlg));
  top->addWidget(new QLabel(
      FmLang(IDT_OVERWRITE_QUESTION_BEGIN,
             QStringLiteral("Would you like to replace the existing file")), &dlg));

  QGridLayout *grid = new QGridLayout();
  int row = 0;
  grid->addWidget(new QLabel(QStringLiteral("Existing file:"), &dlg), row++, 0);
  grid->addWidget(new QLabel(existName, &dlg), row, 0);
  grid->addWidget(new QLabel(existSizeStr, &dlg), row, 1);
  grid->addWidget(new QLabel(existTimeStr, &dlg), row++, 2);
  // kLangIDs[2] = IDT_OVERWRITE_QUESTION_END.
  grid->addWidget(new QLabel(
      FmLang(IDT_OVERWRITE_QUESTION_END, QStringLiteral("with this one?")), &dlg), row++, 0);
  grid->addWidget(new QLabel(newName, &dlg), row, 0);
  grid->addWidget(new QLabel(newSizeStr, &dlg), row, 1);
  grid->addWidget(new QLabel(newTimeStr, &dlg), row++, 2);
  top->addLayout(grid);

  // The six answers, in the order of the Win32 overwrite dialog. langIDs from the
  // .rc PUSHBUTTON ids: IDYES/IDNO/IDCANCEL -> kLangPairs 406/407/402 (LangUtils.cpp),
  // IDB_YES_TO_ALL/IDB_NO_TO_ALL/IDB_AUTO_RENAME -> kLangIDs (OverwriteDialog.cpp:23).
  QPushButton *bYes      = new QPushButton(FmLang(406, QStringLiteral("&Yes")), &dlg);
  QPushButton *bYesToAll = new QPushButton(FmLang(IDB_YES_TO_ALL, QStringLiteral("Yes to &All")), &dlg);
  QPushButton *bNo       = new QPushButton(FmLang(407, QStringLiteral("&No")), &dlg);
  QPushButton *bNoToAll  = new QPushButton(FmLang(IDB_NO_TO_ALL, QStringLiteral("No to A&ll")), &dlg);
  QPushButton *bRename   = new QPushButton(FmLang(IDB_AUTO_RENAME, QStringLiteral("A&uto Rename")), &dlg);
  QPushButton *bCancel   = new QPushButton(FmLang(402, QStringLiteral("&Cancel")), &dlg);

  QVBoxLayout *btns = new QVBoxLayout();
  btns->addWidget(bYes);
  btns->addWidget(bYesToAll);
  btns->addWidget(bNo);
  btns->addWidget(bNoToAll);
  btns->addWidget(bRename);
  btns->addWidget(bCancel);
  top->addLayout(btns);

  // Each button stores its NOverwriteAnswer code and accepts the dialog.
  int chosen = NOverwriteAnswer::kCancel;
  auto bind = [&dlg, &chosen](QPushButton *b, int code) {
    QObject::connect(b, &QPushButton::clicked, &dlg, [&dlg, &chosen, code]() {
      chosen = code;
      dlg.accept();
    });
  };
  bind(bYes,      NOverwriteAnswer::kYes);
  bind(bYesToAll, NOverwriteAnswer::kYesToAll);
  bind(bNo,       NOverwriteAnswer::kNo);
  bind(bNoToAll,  NOverwriteAnswer::kNoToAll);
  bind(bRename,   NOverwriteAnswer::kAutoRename);
  bind(bCancel,   NOverwriteAnswer::kCancel);

  if (dlg.exec() != QDialog::Accepted)
    chosen = NOverwriteAnswer::kCancel; // window closed == cancel

  *answer = chosen;
}
