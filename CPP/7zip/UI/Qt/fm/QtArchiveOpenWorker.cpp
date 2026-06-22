// QtArchiveOpenWorker.cpp
// ----------------------------------------------------------------------------
// See QtArchiveOpenWorker.h. ProcessVirt() runs the existing OpenArchiveAsFolder
// open sequence on the worker thread, passing the dialog's CProgressSync so the
// open callback (inside the helper) drives progress + honours Cancel.
// ----------------------------------------------------------------------------

#include "../../Agent/StdAfx.h"

#include <QtCore/QThread>

#include "QtArchiveOpenWorker.h"
#include "ArchiveOpenHelper.h"

#include "../QtExtractPrompts.h"   // Encrypted-FM : QtPasswordPrompt::SetParentWidget

HRESULT QtArchiveOpenWorker::ProcessVirt()
{
  // Proof-of-thread instrumentation: this MUST be a different QThread than the
  // GUI thread (the headless test asserts the two ids differ). Gated behind an
  // env flag so a production GUI open stays silent on stderr.
  if (getenv("Z7_OPEN_THREAD_TRACE"))
    fprintf(stderr, "OPEN_THREAD worker=%p\n", (void *)QThread::currentThread());

  // Encrypted-FM : parent the (GUI-thread) password dialog to the progress dialog,
  // mirroring QtArchiveExtractWorker's PasswordPrompt->SetParentWidget. Storing the
  // pointer is a plain assignment (no GUI op), safe from the worker thread.
  if (PasswordPrompt)
    PasswordPrompt->SetParentWidget(ProgressDialog);

  // Sync() is the base accessor onto ProgressDialog->Sync — the same shared
  // progress/cancel state the GUI-thread timer reads. The open callback inside
  // OpenArchiveAsFolder ticks it and returns CheckStop() (E_ABORT on cancel),
  // which agent->Open propagates straight back out to here. The same callback
  // prompts via PasswordPrompt (BlockingQueuedConnection) on a header-encrypted
  // archive.
  OpenResult = OpenArchiveAsFolder(ArcPath, Password, PasswordDefined,
      RootFolder, AgentHolder, &IsUpdatable, &Sync(), PasswordPrompt, &Encrypted);

  // G.3b : DON'T return OpenResult to the base CProgressThreadVirt. A non-S_OK
  // return makes the generic progress dialog assemble an error message and pop its
  // OWN error popup (ProgressDialog2 / QtProgressThreadVirt::Process). The original
  // open-progress in the Win32 FM does NOT show open errors there — CPanel::OpenItem
  // -> OpenAsArc_Msg decides whether (and which) error dialog to show, and whether
  // a "not an archive" (S_FALSE) should silently fall through to the external open.
  // So we report the true outcome via OpenResult/Encrypted (read on the GUI thread
  // by the panel) and return S_OK here so the worker's dialog just closes cleanly.
  return S_OK;
}
