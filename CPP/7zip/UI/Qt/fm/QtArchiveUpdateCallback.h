// QtArchiveUpdateCallback.h
// ----------------------------------------------------------------------------
// Milestone B.5b : the Qt mirror of FileManager/UpdateCallback100.h's
// CUpdateCallback100Imp — the ONE IProgress object the FM passes to every
// IFolderOperations archive WRITE op (Delete / CopyFrom / CreateFolder /
// Rename / SetProperty) on a CAgentFolder.
//
// Why a whole new callback (vs reusing the B.4a QtFsOperationCallback): the
// engine's CAgent::DeleteItems (AgentOut.cpp) calls, UNCONDITIONALLY,
//   updateCallback100->DeleteOperation(deletePath)
// where updateCallback100 = progress->QueryInterface(IID_IFolderArchiveUpdate-
// Callback). The B.4a FS callback only QI-succeeds to IProgress +
// IFolderOperationsExtractCallback, so passing it to an archive Delete
// NULL-derefs. This object QI-succeeds to IFolderArchiveUpdateCallback (and the
// ...2 / scan / open / crypto / compress-progress interfaces the engine's
// CUpdateCallbackAgent bridge QIs), exactly like CUpdateCallback100Imp, so the
// same single object survives every QI the archive write-path performs.
//
// It is the faithful (minimal) subset of CUpdateCallback100Imp: all method
// bodies route to ProgressDialog->Sync (the same CProgressSync sink the extract
// / FS-op callbacks use), and the encrypted-repack password request marshals to
// the existing QtPasswordPrompt on the GUI thread via Qt::BlockingQueued
// Connection (the QtExtractCallback pattern). No method may crash when the
// engine calls it — esp. DeleteOperation, derefed unconditionally.
//
// Style mirrors QtFsOperationCallback / QtExtractCallback: Z7_COM_QI_*,
// CMyComPtr, UString, route-to-Sync.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_ARCHIVE_UPDATE_CALLBACK_H
#define ZIP7_INC_QT_FM_ARCHIVE_UPDATE_CALLBACK_H

#include "../../../../Common/MyCom.h"
#include "../../../../Common/MyString.h"

#include "../../../IPassword.h"
#include "../../../IProgress.h"
#include "../../../ICoder.h"               // ICompressProgressInfo
#include "../../../Archive/IArchive.h"     // IArchiveOpenCallback
#include "../../Agent/IFolderArchive.h"    // IFolderArchiveUpdateCallback(+2/_MoveArc), IFolderScanProgress

class QtProgressDialog;
class QtPasswordPrompt;


// Mirror of CUpdateCallback100Imp (UpdateCallback100.h:16-47). The full faithful
// interface set so a single object survives every QI the engine performs on the
// archive write-path.
class QtArchiveUpdateCallback Z7_final:
  public IFolderArchiveUpdateCallback,           // sub-IProgress, 0x0B
  public IFolderArchiveUpdateCallback2,          // 0x10
  public IFolderArchiveUpdateCallback_MoveArc,   // 0x14
  public IFolderScanProgress,                    // 0x11
  public ICryptoGetTextPassword2,                // 0x11 (password ns)
  public ICryptoGetTextPassword,                 // 0x10 (password ns)
  public IArchiveOpenCallback,                   // 0x10 (archive ns)
  public ICompressProgressInfo,                  // 0x04
  public CMyUnknownImp
{
  // IFolderArchiveUpdateCallback is declared as a sub-interface OF IProgress, so
  // this object IS-A IProgress through that base: the worker hands it to
  // IFolderOperations::Delete/CopyFrom(...) directly as the IProgress* arg (a
  // plain upcast, no QI). The engine then QIs IID_IFolderArchiveUpdateCallback,
  // which the table below resolves — satisfying the unconditional DeleteOperation
  // deref. IID_IProgress is intentionally NOT a QI entry here, exactly like
  // CUpdateCallback100Imp (the engine never QIs IProgress on this object), so the
  // QI surface is identical to the original's Z7_COM_UNKNOWN_IMP_8.
  Z7_COM_QI_BEGIN2(IFolderArchiveUpdateCallback)
  Z7_COM_QI_ENTRY(IFolderArchiveUpdateCallback2)
  Z7_COM_QI_ENTRY(IFolderArchiveUpdateCallback_MoveArc)
  Z7_COM_QI_ENTRY(IFolderScanProgress)
  Z7_COM_QI_ENTRY(ICryptoGetTextPassword2)
  Z7_COM_QI_ENTRY(ICryptoGetTextPassword)
  Z7_COM_QI_ENTRY(IArchiveOpenCallback)
  Z7_COM_QI_ENTRY(ICompressProgressInfo)
  Z7_COM_QI_END
  Z7_COM_ADDREF_RELEASE

  // IFolderArchiveUpdateCallback is declared with Z7_IFACE_CONSTR_FOLDERARC_SUB
  // (IProgress, 0x0B), so the macro does NOT itself emit the IProgress methods —
  // they are listed separately, exactly as CUpdateCallback100Imp does.
  Z7_IFACE_COM7_IMP(IProgress)
  Z7_IFACE_COM7_IMP(IFolderArchiveUpdateCallback)
  Z7_IFACE_COM7_IMP(IFolderArchiveUpdateCallback2)
  Z7_IFACE_COM7_IMP(IFolderArchiveUpdateCallback_MoveArc)
  Z7_IFACE_COM7_IMP(IFolderScanProgress)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword2)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
  Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
  Z7_IFACE_COM7_IMP(ICompressProgressInfo)

public:
  // Like CUpdateCallback100Imp::ProgressDialog : the source of the shared Sync
  // and the modal parent for the password prompt. Set by the worker before Create().
  QtProgressDialog *ProgressDialog;

  // GUI-thread password prompt (for encrypted re-pack). nullptr => no prompt
  // (CryptoGetTextPassword then returns E_ABORT, mirroring a cancelled dialog).
  QtPasswordPrompt *PasswordPrompt;

  bool   PasswordIsDefined;          // seeded from the panel's open password
  UString Password;
  UInt64 NumFiles;                   // OperationResult counter (cpp:57)
  bool   ThereAreMessageErrors;

  QtArchiveUpdateCallback():
      ProgressDialog(nullptr),
      PasswordPrompt(nullptr),
      PasswordIsDefined(false),
      NumFiles(0),
      ThereAreMessageErrors(false)
    {}
  ~QtArchiveUpdateCallback() {}
};

#endif
