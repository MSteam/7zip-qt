// QtProgressThreadVirt.h
//
// Qt/Linux port of CProgressThreadVirt (CPP/7zip/UI/FileManager/ProgressDialog2.h).
//
// Mirrors the original worker-thread base: a client inherits this class and
// implements ProcessVirt() (the real work), then calls Create(title, parent).
// Create() spawns a worker thread that runs Process() (== ProcessVirt() wrapped
// in the original's exception-handling block), and shows a modal QtProgressDialog
// on the GUI thread. When the worker finishes it marshals a queued call to
// QtProgressDialog::ProcessWasFinished() (mirrors PostMsg(kCloseMessage)).
//
// THREADING RULE (mirrors the original): the worker thread touches ONLY `Sync`
// (internally locked) and its own data; it never touches QWidgets directly.

#ifndef ZIP7_INC_QT_PROGRESS_THREAD_VIRT_H
#define ZIP7_INC_QT_PROGRESS_THREAD_VIRT_H

#include <QtCore/QThread>

#include "../../../Common/MyString.h"

#include "ProgressSync.h"
#include "QtProgressDialog.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

UString HResultToMessage(HRESULT errorCode);

class QtProgressThreadVirt;

// Worker thread: its run() simply calls owner->Process() — the closest analogue
// to the original NWindows::CThread created with MyThreadFunction.
class QtProgressWorkerThread : public QThread
{
  Q_OBJECT
  QtProgressThreadVirt *_owner;
public:
  explicit QtProgressWorkerThread(QtProgressThreadVirt *owner) : _owner(owner) {}
protected:
  void run() override;
};


class QtProgressThreadVirt
{
protected:
  FStringVector ErrorPaths;
  CProgressFinalMessage FinalMessage;

  // error if any of HRESULT, ErrorMessage, ErrorPath
  virtual HRESULT ProcessVirt() = 0;

  // The dialog (GUI object). Created on the GUI thread in Create().
  // It owns the shared CProgressSync state (ProgressDialog->Sync).
  QtProgressDialog *ProgressDialog;

  // Factory used by Create() to construct the progress dialog. The default
  // makes a plain QtProgressDialog; subclasses (e.g. the hash worker) override
  // it to return a QtProgressDialog subclass whose ProcessWasFinished_GuiVirt()
  // shows a results window — mirroring how the original CProgressThreadVirt
  // subclass IS the dialog and overrides ProcessWasFinished_GuiVirt() directly.
  virtual QtProgressDialog *CreateProgressDialog(QWidget *parentWindow);

  // Convenience accessor so ProcessVirt() implementations can read/write the
  // shared progress state the same way the original code uses `Sync.Set_...`.
  // Valid only while running under Create() (i.e. inside ProcessVirt()).
  CProgressSync &Sync() { return ProgressDialog->Sync; }

public:
  HRESULT Result;
  bool ThreadFinishedOK; // if there is no fatal exception

  // Propagated to the dialog in Create() (mirrors the original global
  // g_DisableUserQuestions). When true the run is fully non-interactive: no
  // message boxes, and the dialog auto-closes at the end. Used for headless runs.
  bool DisableUserQuestions;

  void Process();
  void AddErrorPath(const FString &path) { ErrorPaths.Add(path); }

  HRESULT Create(const UString &title, QWidget *parentWindow = nullptr);

  QtProgressThreadVirt(): ProgressDialog(nullptr), Result(E_FAIL),
      ThreadFinishedOK(false), DisableUserQuestions(false) {}
  virtual ~QtProgressThreadVirt() {}

  // Accessor mirroring the original, plus a hook for subclasses that need the
  // dialog (e.g. to set CompressingMode / a results virt) before Create().
  CProgressMessageBoxPair &GetMessagePair(bool isError) { return isError ? FinalMessage.ErrorMessage : FinalMessage.OkMessage; }
};

#endif
