// QtArchiveOpenWorker.h
// ----------------------------------------------------------------------------
// P.1 : move the archive OPEN (CAgent::Open + BindToRootFolder, the slow format
// scan) off the GUI thread onto a QtProgressThreadVirt worker, so the FM stays
// responsive on large archives — faithful to the Win32 FM's open-progress.
//
// This mirrors the FS-op workers (QtFsDeleteWorker, QtFsOperations.h): the
// inputs are set on the GUI thread before Create(), ProcessVirt() runs the pure
// COM open on the worker thread and stashes the result into plain members, and
// the caller reads those members back ON THE GUI THREAD after Create() returns
// (where the model bind / beginResetModel must happen).
//
// THREADING RULE (inherited): ProcessVirt() touches only COM + Sync() (the open
// callback drives Sync); it never touches a QWidget. The opened CMyComPtr
// outputs are plain refcount holders, safe to move to the panel after the join.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_ARCHIVE_OPEN_WORKER_H
#define ZIP7_INC_QT_FM_ARCHIVE_OPEN_WORKER_H

#include "../../../../Common/MyCom.h"
#include "../../../../Common/MyString.h"

#include "../../FileManager/IFolder.h"   // IFolderFolder

#include "../QtProgressThreadVirt.h"

class QtArchiveOpenWorker Z7_final : public QtProgressThreadVirt
{
public:
  // --- inputs (set on the GUI thread before Create()) ---
  UString ArcPath;
  UString Password;
  bool    PasswordDefined = false;
  // Encrypted-FM : optional GUI-thread password prompt for a header-encrypted
  // archive. ProcessVirt() runs on the WORKER thread, so the open callback inside
  // OpenArchiveAsFolder reaches this prompt via BlockingQueuedConnection (it has
  // GUI-thread affinity). Null on the headless / no-prompt path.
  class QtPasswordPrompt *PasswordPrompt = nullptr;

  // --- outputs (read on the GUI thread AFTER Create() returns S_OK) ---
  CMyComPtr<IFolderFolder> RootFolder;   // bind into the model here (GUI thread)
  CMyComPtr<IUnknown>      AgentHolder;  // -> _agentKeepAlive (B.5b in-place ops)
  bool                     IsUpdatable = false;

  // G.3b : the TRUE result of the archive open (S_OK / S_FALSE / E_ABORT / error
  // HRESULT) and whether it was header-encrypted. ProcessVirt() stashes both here
  // and returns S_OK to the base CProgressThreadVirt so the GENERIC progress dialog
  // never pops its own error popup for a "not an archive" (S_FALSE) or open error —
  // the panel reads OpenResult/Encrypted and shows the SINGLE faithful dialog
  // (CPanel::OpenAsArc_Msg analogue), or falls through to xdg-open for S_FALSE.
  HRESULT OpenResult = E_FAIL;
  bool    Encrypted = false;

protected:
  HRESULT ProcessVirt() Z7_override;     // runs on the worker thread
};

#endif
