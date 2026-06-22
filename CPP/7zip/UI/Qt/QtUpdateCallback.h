// QtUpdateCallback.h
//
// Qt/Linux update (compression) callback (milestone C.3a). This is the Qt-backed
// sibling of:
//   GUI/UpdateCallbackGUI.{h,cpp} + UpdateCallbackGUI2.cpp : CUpdateCallbackGUI
//   Console/UpdateCallbackConsole.{h,cpp}                  : CUpdateCallbackConsole
// trimmed to exactly what Common/Update.h : UpdateArchive() needs.
//
// UpdateArchive() takes ONE object as BOTH its openCallback (IOpenCallbackUI) and
// its callback (IUpdateCallbackUI2) parameters, exactly like CThreadUpdating does
// (see UpdateGUI.cpp ProcessVirt: UpdateArchive(..., UpdateCallbackGUI,
// UpdateCallbackGUI, ...)). So this class implements both roles, mirroring
// CUpdateCallbackGUI which derives from IOpenCallbackUI + IUpdateCallbackUI2.
//
// IUpdateCallbackUI2 == IUpdateCallbackUI + IDirItemsCallback + the UI2 methods
// (OpenResult / StartScanning / StartArchive / MoveArc_* etc). These are NON-COM
// pure interfaces (no QI/refcount); UpdateArchive reaches them by direct pointer,
// exactly like the console/GUI templates. So this object is NOT COM-owned; the
// worker holds it by plain pointer and the GUI owns its lifetime (stack), mirror
// of how CThreadUpdating holds CUpdateCallbackGUI* by pointer.
//
// Routing (mirrors CUpdateCallbackGUI -> ProgressDialog->Sync):
//   StartScanning/ScanProgress/FinishScanning -> Sync scan progress
//   SetNumItems/SetTotal/SetCompleted/SetRatioInfo -> Sync byte/file progress
//   GetStream/ReportUpdateOperation (the per-file op) -> Sync.Set_Status2
//   ScanError/OpenFileError/ReadingFileError/OpenResult -> Sync.AddError_*
//   CheckBreak / Open_CheckBreak -> Sync.CheckStop()  (E_ABORT on cancel)
//   CryptoGetTextPassword2 -> QtPasswordPrompt (GUI thread, BlockingQueued)

#ifndef ZIP7_INC_QT_UPDATE_CALLBACK_H
#define ZIP7_INC_QT_UPDATE_CALLBACK_H

#include "../../../Common/MyString.h"

#include "../Common/Update.h"            // IUpdateCallbackUI2, CUpdateOptions
#include "../Common/ArchiveOpenCallback.h" // IOpenCallbackUI

class QtProgressDialog;
class QtPasswordPrompt;

// Mirror of CUpdateCallbackGUI (UpdateCallbackGUI.h). Implements IOpenCallbackUI
// + IUpdateCallbackUI2 (== IUpdateCallbackUI + IDirItemsCallback + UI2). The
// CUpdateCallbackGUI2 base (the _arcMoving_* / _lang_* state + MoveArc/SetOperation
// helpers) is folded in here directly to keep this a single Qt TU.
class QtUpdateCallback Z7_final:
  public IOpenCallbackUI,
  public IUpdateCallbackUI2
{
  Z7_IFACE_IMP(IOpenCallbackUI)
  Z7_IFACE_IMP(IUpdateCallbackUI)
  Z7_IFACE_IMP(IDirItemsCallback)
  Z7_IFACE_IMP(IUpdateCallbackUI2)

public:
  // === wiring (set by QtCompressGUI before the worker runs) =================
  QtProgressDialog *ProgressDialog;
  QtPasswordPrompt *PasswordPrompt;

  // === password state (mirror CUpdateCallbackGUI2) ==========================
  bool PasswordIsDefined;
  bool PasswordWasAsked;
  bool AskPassword;
  UString Password;

  UInt64 NumFiles;
  FStringVector FailedFiles;

  QtUpdateCallback():
      ProgressDialog(nullptr),
      PasswordPrompt(nullptr),
      PasswordIsDefined(false),
      PasswordWasAsked(false),
      AskPassword(false),
      NumFiles(0)
    {
      _arcMoving_percents = 0;
      _arcMoving_total = 0;
      _arcMoving_current = 0;
      _arcMoving_updateMode = 0;
    }

  // Mirror CUpdateCallbackGUI::Init() + CUpdateCallbackGUI2::Init().
  void Init();

private:
  // === CUpdateCallbackGUI2 state (folded in) ================================
  UString _arcMoving_name1;
  UString _arcMoving_name2;
  UInt64  _arcMoving_percents;
  UInt64  _arcMoving_total;
  UInt64  _arcMoving_current;
  Int32   _arcMoving_updateMode;

  UStringVector _lang_Ops;
  UString _lang_Removing;
  UString _lang_Moving;
  UString _emptyString;

  HRESULT SetOperation_Base(UInt32 notifyOp, const wchar_t *name, bool isDir);
  HRESULT ShowAskPasswordDialog();        // -> QtPasswordPrompt (GUI thread)
  HRESULT MoveArc_UpdateStatus();
  HRESULT MoveArc_Start_Base(const wchar_t *srcTempPath, const wchar_t *destFinalPath, UInt64 totalSize, Int32 updateMode);
  HRESULT MoveArc_Progress_Base(UInt64 totalSize, UInt64 currentSize);
  HRESULT MoveArc_Finish_Base();
};

#endif
