// QtProgressThreadVirt.cpp
//
// Qt/Linux port of CProgressThreadVirt (CPP/7zip/UI/FileManager/ProgressDialog2.cpp).
// The Process() exception-handling block and the message assembly are ported
// faithfully from the original; only the thread primitive (QThread vs
// NWindows::CThread) and the dialog "close" notification (queued slot call vs
// PostMsg(kCloseMessage)) differ.

#include "QtProgressThreadVirt.h"

#include <QtCore/QMetaObject>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtWidgets/QMessageBox>   // G.8d : Progress Error fallback box

#include "../../../Common/StringConvert.h"
#include "../../../Windows/ErrorMsg.h"

using namespace NWindows;

// P.1 : faithful equivalent of CProgressDialog's kCreateDelay (ProgressDialog2.cpp).
// The original WaitCreating() blocks on _dialogCreatedEvent for kCreateDelay ms,
// so a fast op finishes before the window ever materialises. We mirror that: the
// dialog is shown only if the worker has not signalled "finished" within this
// delay — so a small/fast archive open (or any fast op) never flashes a dialog.
static const int kCreateDelayMs = 500;

void QtProgressWorkerThread::run()
{
  // MyThreadFunction equivalent: run Process(), flag ThreadFinishedOK on a
  // clean return, swallow truly-fatal exceptions into Result = E_FAIL.
  try
  {
    _owner->Process();
    _owner->ThreadFinishedOK = true;
  }
  catch (...) { _owner->Result = E_FAIL; }
}


// AddMessageToString : verbatim from ProgressDialog2.cpp
static void AddMessageToString(UString &dest, const UString &src)
{
  if (!src.IsEmpty())
  {
    if (!dest.IsEmpty())
      dest.Add_LF();
    dest += src;
  }
}

// CProgressThreadVirt::Process() : ported faithfully. The original wraps the
// call in a CProgressCloser whose destructor posts kCloseMessage; here the
// "close" is posted explicitly at the end via a queued slot call so it runs on
// the GUI thread.
void QtProgressThreadVirt::Process()
{
  CProgressSync &Sync = ProgressDialog->Sync;
  UString m;
  try { Result = ProcessVirt(); }
  catch(const wchar_t *s) { m = s; }
  catch(const UString &s) { m = s; }
  catch(const char *s) { m = GetUnicodeString(s); }
  catch(int v)
  {
    m = "Error #";
    m.Add_UInt32((unsigned)v);
  }
  catch(...) { m = "Error"; }
  if (Result != E_ABORT)
  {
    if (m.IsEmpty() && Result != S_OK)
      m = HResultToMessage(Result);
  }
  AddMessageToString(m, FinalMessage.ErrorMessage.Message);

  {
    FOR_VECTOR(i, ErrorPaths)
    {
      if (i >= 32)
        break;
      AddMessageToString(m, fs2us(ErrorPaths[i]));
    }
  }

  {
    CProgressSync &sync = Sync;
    NSynchronization::CCriticalSectionLock lock(sync._cs);
    if (m.IsEmpty())
    {
      if (!FinalMessage.OkMessage.Message.IsEmpty())
        sync.FinalMessage.OkMessage = FinalMessage.OkMessage;
    }
    else
    {
      sync.FinalMessage.ErrorMessage.Message = m;
      if (Result == S_OK)
        Result = E_FAIL;
    }
  }

  // == CProgressCloser::~CProgressCloser() / ProcessWasFinished() / PostMsg(kCloseMessage)
  // Marshal the "finished" notification onto the GUI thread so the dialog
  // closes there. The worker thread must not touch the dialog widgets directly.
  QMetaObject::invokeMethod(ProgressDialog, "ProcessWasFinished", Qt::QueuedConnection);
}


// CProgressThreadVirt::Create() : ported. Creates the worker thread, then shows
// the modal progress dialog (exec() replaces CModalDialog::Create()). Returns
// Result after the dialog closes and the worker has joined.
QtProgressDialog *QtProgressThreadVirt::CreateProgressDialog(QWidget *parentWindow)
{
  return new QtProgressDialog(parentWindow);
}

HRESULT QtProgressThreadVirt::Create(const UString &title, QWidget *parentWindow)
{
  ProgressDialog = CreateProgressDialog(parentWindow);
  ProgressDialog->SetTitle(title);
  ProgressDialog->DisableUserQuestions = DisableUserQuestions;
  ProgressDialog->setAttribute(Qt::WA_DeleteOnClose, false);
  ProgressDialog->setModal(true);   // application-modal, like exec() (WaitCreating)

  QtProgressWorkerThread thread(this);
  thread.start();

  // CProgressDialog::Create() + WaitCreating(): the original delays the window by
  // kCreateDelay so a fast op never shows. Mirror that here: instead of exec()ing
  // immediately (which would flash the dialog on every op), run a nested event
  // loop and only show the dialog if the worker is still running after the delay.
  //
  // ProcessWasFinished() (queued onto the GUI thread from the worker) calls
  // accept() on the dialog, which emits QDialog::finished — that quits the loop
  // whether or not the dialog was ever shown. In the interactive "messages"
  // case ProcessWasFinished() keeps the dialog open with a Close button that is
  // re-wired to accept(), so finished still ends the loop after the user reads.
  QEventLoop loop;
  QObject::connect(ProgressDialog, &QDialog::finished, &loop, &QEventLoop::quit);

  QtProgressDialog *dlg = ProgressDialog;
  QTimer::singleShot(kCreateDelayMs, dlg, [dlg, &loop]() {
    // Only show if the work has NOT already finished. On a fast op the loop has
    // already quit (or is about to) and we must not flash.
    //
    // The loop.isRunning() check alone is NOT enough: ProcessWasFinished() for a
    // results-bearing op (e.g. hash) shows its RESULTS window MODALLY via a
    // NESTED event loop (dlg.exec()) from inside ProcessWasFinished_GuiVirt().
    // While that nested modal runs, the OUTER `loop` is still running, so a fast
    // op's singleShot would fire and pop the finished progress dialog at 100% on
    // top of the results. _externalCloseHandled is set at the very START of
    // ProcessWasFinished (before any results window), so wasExternallyClosed() is
    // the correct "work is done" guard. (mirror: CProgressThreadVirt::Create's
    // WaitCreating never creates the window when the worker finishes within
    // kCreateDelay.)
    if (loop.isRunning() && !dlg->isVisible() && !dlg->wasExternallyClosed())
      dlg->show();
  });

  loop.exec();

  // thread.Wait_Close() equivalent.
  thread.wait();

  // If the dialog was shown (slow op), close/hide it now; finished already fired.
  if (ProgressDialog->isVisible())
    ProgressDialog->hide();

  // G.8d : Progress Error fallback box — ported from CProgressDialog::Create()
  // (ProgressDialog2.cpp:985-987). After the worker thread closes, if NOTHING was
  // ever displayed to the user (no final OK/Error box, no message list shown) and
  // user-questions are enabled, surface a generic "Progress Error" so a silent
  // failure never goes unreported. Capture MessagesDisplayed BEFORE deleting the
  // dialog. (The original's `wndParent` parent is omitted: the dialog is being
  // torn down, so the box is shown parentless, like the Win32 box once the
  // progress HWND is gone.)
  const bool messagesDisplayed = ProgressDialog->MessagesDisplayed;

  HRESULT res = Result;
  delete ProgressDialog;
  ProgressDialog = nullptr;

  if (!messagesDisplayed)
  if (!DisableUserQuestions)
    QMessageBox::critical(parentWindow, QStringLiteral("7-Zip"),
        QStringLiteral("Progress Error"));

  return res;
}


// HResultToMessage : ported from ProgressDialog2.cpp. The original returns
// LangString(IDS_MEM_ERROR) for E_OUTOFMEMORY; there is no lang table in this
// isolated build, so a plain equivalent string is used.
UString HResultToMessage(HRESULT errorCode)
{
  if (errorCode == E_OUTOFMEMORY)
    return UString("There is not enough memory");
  else
    return NError::MyFormatMessage(errorCode);
}
