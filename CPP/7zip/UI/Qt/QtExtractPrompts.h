// QtExtractPrompts.h
//
// GUI-thread prompt objects for the Qt extraction callback (milestone C.1b).
//
// These mirror the SEMANTICS of the Win32 prompts used by CExtractCallbackImp:
//   * CPasswordDialog  (FileManager/PasswordDialog.h)  -> QtPasswordPrompt
//   * COverwriteDialog (FileManager/OverwriteDialog.h)  -> QtOverwritePrompt
// (not the Win32 layout). The answer sets are identical: the overwrite prompt
// returns one of NOverwriteAnswer::{kYes,kYesToAll,kNo,kNoToAll,kAutoRename,kCancel};
// the password prompt returns a string + accepted/cancelled, exactly like
// CPasswordDialog::Password and its IDOK/IDCANCEL result.
//
// THREADING (the critical, deadlock-prone pattern):
//   The worker thread (running Extract()) must NEVER touch a QWidget. When the
//   engine asks for a password or an overwrite decision, the callback (on the
//   worker thread) calls QMetaObject::invokeMethod(prompt, "...",
//   Qt::BlockingQueuedConnection, ...). Qt queues the slot onto the GUI thread's
//   event loop and BLOCKS the worker until the slot returns. The slot runs on
//   the GUI thread, shows the modal dialog there (parented to the progress
//   dialog), and writes the user's answer into out-parameters the worker reads
//   after the blocking call returns. This is the synchronous re-entrancy the
//   architecture requires: the worker is suspended while the GUI thread handles
//   the question, just as the Win32 build's worker thread blocks inside
//   dialog.Create(*ProgressDialog).
//
// The prompt objects are created on, and have thread-affinity to, the GUI
// thread (they are moved there in the GUI-wiring code). Their invokable slots
// are therefore always executed on the GUI thread.

#ifndef ZIP7_INC_QT_EXTRACT_PROMPTS_H
#define ZIP7_INC_QT_EXTRACT_PROMPTS_H

#include <QtCore/QObject>

#include "../../../Common/MyString.h"
#include "../../../Common/MyWindows.h" // FILETIME

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

// ---------------------------------------------------------------------------
// QtPasswordPrompt : GUI-thread analogue of CPasswordDialog usage in
// CExtractCallbackImp::CryptoGetTextPassword.
// ---------------------------------------------------------------------------
class QtPasswordPrompt : public QObject
{
  Q_OBJECT
public:
  explicit QtPasswordPrompt(QObject *parent = nullptr) : QObject(parent), _parentWidget(nullptr) {}

  // The progress dialog acts as the modal parent (mirrors dialog.Create(*ProgressDialog)).
  void SetParentWidget(QWidget *w) { _parentWidget = w; }

  // Invoked on the GUI thread (BlockingQueuedConnection from the worker).
  // Shows the modal password dialog; on OK writes *password and sets *accepted
  // to true; on Cancel sets *accepted to false. Mirrors the IDOK/IDCANCEL of
  // CPasswordDialog::Create() and the read of CPasswordDialog::Password.
  Q_INVOKABLE void Ask(QString *password, bool *accepted);

private:
  QWidget *_parentWidget;
};


// ---------------------------------------------------------------------------
// QtOverwritePrompt : GUI-thread analogue of COverwriteDialog usage in
// CExtractCallbackImp::AskOverwrite.
//
// The answer codes are exactly NOverwriteAnswer::EEnum values (kYes=0, kYesToAll,
// kNo, kNoToAll, kAutoRename, kCancel), passed as Int32 out-parameter, just like
// the original writes *answer.
// ---------------------------------------------------------------------------
class QtOverwritePrompt : public QObject
{
  Q_OBJECT
public:
  explicit QtOverwritePrompt(QObject *parent = nullptr) : QObject(parent), _parentWidget(nullptr) {}

  void SetParentWidget(QWidget *w) { _parentWidget = w; }

  // Invoked on the GUI thread (BlockingQueuedConnection from the worker).
  // Shows the existing-vs-new file info and the six buttons; writes the chosen
  // NOverwriteAnswer code into *answer. existTimeStr/newTimeStr may be empty.
  Q_INVOKABLE void Ask(
      QString existName, QString existSizeStr, QString existTimeStr,
      QString newName,   QString newSizeStr,   QString newTimeStr,
      int *answer);

private:
  QWidget *_parentWidget;
};

#endif
