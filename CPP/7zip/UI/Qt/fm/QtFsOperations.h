// QtFsOperations.h
// ----------------------------------------------------------------------------
// Milestone B.4a : the filesystem OPERATIONS (Copy / Move / Delete / Rename /
// Create Folder / Create File) driven through the engine's IFolderOperations
// (CFSFolder), on the Qt threaded modal-progress core.
//
// This is the Qt analogue of CPanel's operation control flow (PanelCopy.cpp /
// PanelOperations.cpp):
//
//   * CPanel::CopyTo runs a CPanelCopyThread (a CProgressThreadVirt) whose
//     ProcessVirt() calls _folderOperations->CopyTo(moveMode, indices, numItems,
//     ..., destPath, ExtractCallback). Here QtFsCopyWorker (a
//     QtProgressThreadVirt) does exactly that, with QtFsOperationCallback as the
//     IFolderOperationsExtractCallback.
//   * CPanel::DeleteItems runs the delete in a progress thread calling
//     _folder->Delete(indices, ...). Here QtFsDeleteWorker does that.
//   * CreateFolder / CreateFile / Rename are synchronous one-shot IFolderOperations
//     calls (no progress thread needed; the originals call them directly), wired
//     in the window.
//
// QtFsOperationCallback is the minimal Qt/Sync-backed IFolderOperationsExtractCallback
// — the sibling of QtExtractCallback, trimmed to exactly the four
// IFolderOperationsExtractCallback methods FSFolderCopy.cpp uses (AskWrite,
// ShowMessage, SetCurrentFilePath, SetNumFiles) plus IProgress (SetTotal,
// SetCompleted). It mirrors CExtractCallbackImp::AskWrite's overwrite state
// machine (OverwriteMode + the NOverwriteAnswer mapping) and routes the actual
// question to the GUI-thread QtOverwritePrompt (the same prompt the extract flow
// uses), so the FM's copy/move overwrite prompt is the existing one.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_FS_OPERATIONS_H
#define ZIP7_INC_QT_FM_FS_OPERATIONS_H

#include "../../../../Common/MyCom.h"
#include "../../../../Common/MyString.h"

#include "../../FileManager/IFolder.h"          // IFolderOperations*, IFolderOperationsExtractCallback
#include "../../Common/ExtractMode.h"           // NExtract::NOverwriteMode
#include "../../Common/HashCalc.h"              // G.2d : CHashBundle (in-archive CRC)

#include "../QtProgressThreadVirt.h"

class QtOverwritePrompt;


// === the Qt IFolderOperationsExtractCallback ================================
// Routes progress to ProgressDialog->Sync (like QtExtractCallback) and the
// overwrite question to the GUI-thread QtOverwritePrompt.
class QtFsOperationCallback Z7_final:
  public IFolderOperationsExtractCallback,
  public CMyUnknownImp
{
  Z7_COM_QI_BEGIN2(IProgress)
  Z7_COM_QI_ENTRY(IFolderOperationsExtractCallback)
  Z7_COM_QI_END
  Z7_COM_ADDREF_RELEASE

  Z7_IFACE_COM7_IMP(IProgress)
  Z7_IFACE_COM7_IMP(IFolderOperationsExtractCallback)

public:
  // Like CExtractCallbackImp::ProgressDialog : the shared Sync + the modal parent
  // for the overwrite prompt. Set by the worker before Create().
  QtProgressDialog *ProgressDialog;
  QtOverwritePrompt *OverwritePrompt;

  bool MoveMode;                                 // for the "...onto itself" wording
  bool ThereAreMessageErrors;
  NExtract::NOverwriteMode::EEnum OverwriteMode; // the same kAsk/kSkip/... state machine

  QtFsOperationCallback():
      ProgressDialog(nullptr),
      OverwritePrompt(nullptr),
      MoveMode(false),
      ThereAreMessageErrors(false),
      OverwriteMode(NExtract::NOverwriteMode::kAsk)
    {}
  ~QtFsOperationCallback() {}

  HRESULT MessageError(const wchar_t *message, const FString &path);
};


// === Copy / Move worker (CPanelCopyThread analogue) =========================
class QtFsCopyWorker Z7_final : public QtProgressThreadVirt
{
public:
  // Inputs (set before Create()):
  CMyComPtr<IFolderOperations> FolderOperations; // the SOURCE panel's folder
  CRecordVector<UInt32> Indices;                 // selected source rows
  UString DestPath;                              // OTHER panel dir, with trailing sep
  bool MoveMode;
  QtOverwritePrompt *OverwritePrompt;

  QtFsCopyWorker(): MoveMode(false), OverwritePrompt(nullptr) {}

protected:
  HRESULT ProcessVirt() Z7_override;
};


// === Archive extract-to-temp worker (B.5c, drag-OUT) ========================
// CPanel::CopyTo on an ARCHIVE source calls CAgentFolder::CopyTo, which QIs the
// callback to IFolderArchiveExtractCallback and dispatches the engine Extract.
// QtFsOperationCallback only QIs to IFolderOperationsExtractCallback, so it
// gives E_NOINTERFACE for archives. This worker instead drives a QtExtractCallback
// (which now implements BOTH interfaces, like CExtractCallbackImp) — the same
// archive-extract callback QtExtractGUI uses. COPY/extract semantics only
// (MoveMode is unsupported by CAgentFolder::CopyTo: it returns E_NOTIMPL).
class QtArchiveExtractWorker Z7_final : public QtProgressThreadVirt
{
public:
  CMyComPtr<IFolderOperations> FolderOperations; // the ARCHIVE panel's CAgentFolder
  CRecordVector<UInt32> Indices;                 // selected archive item indices
  UString DestPath;                              // temp dir, with trailing sep
  QtOverwritePrompt *OverwritePrompt;
  class QtPasswordPrompt *PasswordPrompt;        // encrypted archives

  QtArchiveExtractWorker(): OverwritePrompt(nullptr), PasswordPrompt(nullptr) {}

protected:
  HRESULT ProcessVirt() Z7_override;
};


// === In-archive hash worker (G.2d : CRC/checksum of archived streams) ========
// Mirrors PanelCrc.cpp's !Is_IO_FS_Folder branch (CApp::CalculateCrc2):
//   CCopyToOptions options; options.streamMode=true; options.showErrorMessages=true;
//   options.hashMethods.Add(methodName); options.NeedRegistryZone=false;
//   srcPanel.CopyTo(options, indices, &messages);
// i.e. CPanel::CopyTo on an ARCHIVE source -> CAgentFolder::CopyTo -> engine
// Extract, with the extract callback in stream-mode + a CHashBundle attached. The
// digests are read out of Hash after the run (Qt_AddHashBundleRes -> the same
// QtHashResultsDialog the FS hash path shows). This worker is the Qt analogue of
// CPanelCopyThread::ProcessVirt's hashMethods configuration, driving QtExtractCallback
// (which now implements IFolderExtractToStreamCallback, like CExtractCallbackImp).
class QtArchiveHashWorker Z7_final : public QtProgressThreadVirt
{
public:
  CMyComPtr<IFolderOperations> FolderOperations; // the ARCHIVE panel's CAgentFolder
  CRecordVector<UInt32> Indices;                 // selected archive item indices
  UStringVector HashMethods;                     // e.g. {"CRC32"} or {"*"}
  UString FirstFileName;                          // == GetItemRelPath(indices[0]) if size 1
  class QtPasswordPrompt *PasswordPrompt;         // encrypted archives
  CHashBundle Hash;                               // accumulates the digests

  QtArchiveHashWorker(): PasswordPrompt(nullptr) {}

protected:
  HRESULT ProcessVirt() Z7_override;
};


// === Delete worker (CPanel::DeleteItems analogue) ===========================
// B.5b : routes BOTH FS and archive Delete through QtArchiveUpdateCallback (the
// faithful mirror of the original, which builds one CUpdateCallback100Imp for
// both). The archive Delete REQUIRES the IProgress to QI-succeed to
// IFolderArchiveUpdateCallback (CAgent::DeleteItems derefs DeleteOperation
// unconditionally); CFSFolder::Delete only uses IProgress::SetTotal/SetCompleted
// and ignores the extra interface — so one callback serves both.
class QtFsDeleteWorker Z7_final : public QtProgressThreadVirt
{
public:
  CMyComPtr<IFolderOperations> FolderOperations;
  CRecordVector<UInt32> Indices;

protected:
  HRESULT ProcessVirt() Z7_override;
};


// === Add-into-archive worker (CThreadUpdate / CPanel::CopyFrom analogue) =====
// PanelCopy.cpp's CThreadUpdate::Process calls
//   FolderOperations->CopyFrom(moveMode, FolderPrefix, FileNamePointers, n, cb)
// passing the CUpdateCallback100Imp as the IProgress. Here the dest panel's
// CAgentFolder IFolderOperations gets the FS items added at its current dir; the
// callback is QtArchiveUpdateCallback (QI-succeeds to IFolderArchiveUpdate-
// Callback, drives progress through ReOpen()).
//
//   FromFolderPrefix : common on-disk parent dir (trailing separator); each
//                      ItemNames[i] is a name relative to / under it. The
//                      in-archive destination is implicit (the agent folder's
//                      current dir). For the FM drop/copy-into default we use the
//                      empty-prefix / full-path form (CopyFromNoAsk): FromFolder
//                      Prefix = L"" and each ItemNames[i] is a full FS path.
class QtArchiveAddWorker Z7_final : public QtProgressThreadVirt
{
public:
  CMyComPtr<IFolderOperations> FolderOperations;  // the ARCHIVE panel's ops
  UString FromFolderPrefix;                        // common parent (or empty)
  UStringVector ItemNames;                         // names under the prefix (or full paths)
  bool MoveMode;
  // Optional encrypted-repack password prompt (GUI-thread affinity).
  class QtPasswordPrompt *PasswordPrompt;

  QtArchiveAddWorker(): MoveMode(false), PasswordPrompt(nullptr) {}

protected:
  HRESULT ProcessVirt() Z7_override;
};


// === Edit-writeback worker (G.2a : CThreadCopyFrom / CopyFromFile analogue) ===
// PanelItemOpen.cpp's CThreadCopyFrom::ProcessVirt calls, after the user edited
// an extracted-to-temp copy of an in-archive item and chose to update it:
//   FolderOperations->CopyFromFile(ItemIndex, FullPath, UpdateCallback)
// where ItemIndex = the item's realIndex in the CAgentFolder and FullPath = the
// edited temp file. This is the LIVE B.5a write path (CAgentFolder::CopyFromFile
// -> CommonUpdateOperation(AGENT_OP_CopyFromFile)). Like Delete/CopyFrom it does a
// Close()+ReOpen() in place, so the GUI panel must be suspended around Create().
// The callback is QtArchiveUpdateCallback (the SAME object Delete/Add use), which
// QI-succeeds to IFolderArchiveUpdateCallback and marshals the encrypted-repack
// password to the GUI-thread QtPasswordPrompt.
class QtArchiveCopyFromWorker Z7_final : public QtProgressThreadVirt
{
public:
  CMyComPtr<IFolderOperations> FolderOperations;  // the ARCHIVE panel's ops
  UInt32  Index;                                   // realIndex of the entry to overwrite
  UString FullPath;                                // edited temp file (full FS path)
  // Optional encrypted-repack password prompt (GUI-thread affinity).
  class QtPasswordPrompt *PasswordPrompt;

  QtArchiveCopyFromWorker(): Index(0), PasswordPrompt(nullptr) {}

protected:
  HRESULT ProcessVirt() Z7_override;
};

#endif
