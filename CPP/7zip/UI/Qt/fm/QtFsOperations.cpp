// QtFsOperations.cpp
// ----------------------------------------------------------------------------
// See QtFsOperations.h. The callback methods mirror CExtractCallbackImp's
// IFolderOperationsExtractCallback bodies (ExtractCallback.cpp) and the worker
// ProcessVirt()s mirror CPanelCopyThread::ProcessVirt / CPanel::DeleteItems.
// ----------------------------------------------------------------------------

#include "QtFsOperations.h"

#include <QtCore/QMetaObject>
#include <QtCore/QString>
#include <QtCore/Qt>

#include "../../../../Common/ComTry.h"
#include "../../../../Common/IntToString.h"      // ConvertUInt64ToString
#include "../../../../Common/StringConvert.h"

#include "../../../../Windows/FileFind.h"
#include "../../../../Windows/FileDir.h"
#include "../../../../Windows/PropVariantConv.h"
#ifndef _WIN32
#include "../../../../Windows/TimeUtils.h"       // FiTime_To_FILETIME (CFiTime -> FILETIME)
#endif

#include "../../Common/IFileExtractCallback.h"   // NOverwriteAnswer
#include "../../Common/ExtractMode.h"
#include "../../../Common/FilePathAutoRename.h"   // AutoRenamePath

#include "../QtProgressDialog.h"
#include "../QtExtractPrompts.h"

#include "QtArchiveUpdateCallback.h"   // B.5b : IFolderArchiveUpdateCallback for archive ops
#include "../QtExtractCallback.h"      // B.5c : archive extract-to-temp callback (drag-OUT)

using namespace NWindows;
using namespace NWindows::NFile;


// === helpers (mirror QtExtractCallback.cpp's marshalling) ===================
static QString WcsToQString(const wchar_t *s)
{
  if (!s) return QString();
  return QString::fromWCharArray(s);
}

static QString SizeToQString(const UInt64 *size)
{
  if (!size || *size == (UInt64)(Int64)-1)
    return QString();
  char temp[32];
  ConvertUInt64ToString(*size, temp);
  return QString::fromLatin1(temp) + QStringLiteral(" bytes");
}

static QString TimeToQString(const FILETIME *ft)
{
  if (!ft || (ft->dwHighDateTime == 0 && ft->dwLowDateTime == 0))
    return QString();
  char temp[64];
  if (ConvertUtcFileTimeToString(*ft, temp, kTimestampPrintLevel_SEC))
    return QString::fromLatin1(temp);
  return QString();
}


// === IProgress : mirror CExtractCallbackImp::SetTotal/SetCompleted ==========
Z7_COM7F_IMF(QtFsOperationCallback::SetTotal(UInt64 total))
{
  ProgressDialog->Sync.Set_NumBytesTotal(total);
  return S_OK;
}

Z7_COM7F_IMF(QtFsOperationCallback::SetCompleted(const UInt64 *value))
{
  return ProgressDialog->Sync.Set_NumBytesCur(value);
}


// === IFolderOperationsExtractCallback =======================================
// SetNumFiles / ShowMessage / SetCurrentFilePath mirror CExtractCallbackImp.
Z7_COM7F_IMF(QtFsOperationCallback::SetNumFiles(UInt64 numFiles))
{
  ProgressDialog->Sync.Set_NumFilesTotal(numFiles);
  return S_OK;
}

Z7_COM7F_IMF(QtFsOperationCallback::ShowMessage(const wchar_t *message))
{
  ThereAreMessageErrors = true;
  ProgressDialog->Sync.AddError_Message(message);
  return S_OK;
}

Z7_COM7F_IMF(QtFsOperationCallback::SetCurrentFilePath(const wchar_t *filePath))
{
  ProgressDialog->Sync.Set_FilePath(filePath);
  return S_OK;
}

HRESULT QtFsOperationCallback::MessageError(const wchar_t *message, const FString &path)
{
  ThereAreMessageErrors = true;
  ProgressDialog->Sync.AddError_Message_Name(message, fs2us(path));
  return S_OK;
}


// === AskWrite : faithful mirror of CExtractCallbackImp::AskWrite ============
// The overwrite-policy state machine (OverwriteMode + NOverwriteAnswer mapping +
// kRename auto-rename) is identical to the GUI template; only the question is
// routed to the Qt overwrite prompt (GUI thread, BlockingQueuedConnection) and
// the "already exists -> delete first" path uses NDir::DeleteFileAlways.
Z7_COM7F_IMF(QtFsOperationCallback::AskWrite(
    const wchar_t *srcPath, Int32 srcIsFolder,
    const FILETIME *srcTime, const UInt64 *srcSize,
    const wchar_t *destPath,
    BSTR *destPathResult,
    Int32 *writeAnswer))
{
  UString destPathResultTemp = destPath;

  *destPathResult = NULL;
  *writeAnswer = BoolToInt(false);

  FString destPathSys = us2fs(destPath);
  const bool srcIsFolderSpec = IntToBool(srcIsFolder);
  NFind::CFileInfo destFileInfo;

  if (destFileInfo.Find(destPathSys))
  {
    if (srcIsFolderSpec)
    {
      if (!destFileInfo.IsDir())
      {
        RINOK(MessageError(L"Cannot replace file with folder with same name", destPathSys))
        return E_ABORT;
      }
      *writeAnswer = BoolToInt(false);
      return S_OK;
    }

    if (destFileInfo.IsDir())
    {
      RINOK(MessageError(L"Cannot replace folder with file with same name", destPathSys))
      *writeAnswer = BoolToInt(false);
      return S_OK;
    }

    switch ((int)OverwriteMode)
    {
      case NExtract::NOverwriteMode::kSkip:
        return S_OK;
      case NExtract::NOverwriteMode::kAsk:
      {
        // existing-file name (mirror: build the dest path's leaf with the real
        // on-disk case from destFileInfo.Name)
        UString destPathSpec = destPath;
        const int slashPos = destPathSpec.ReverseFind_PathSepar();
        destPathSpec.DeleteFrom((unsigned)(slashPos + 1));
        destPathSpec += fs2us(destFileInfo.Name);

#ifdef _WIN32
        const FILETIME *existMTimePtr = &destFileInfo.MTime;
#else
        // [B.4 Linux port] CFileInfo::MTime is a CFiTime (timespec) on Linux;
        // convert to FILETIME for the prompt's time string.
        FILETIME existMTime_ft;
        FiTime_To_FILETIME(destFileInfo.MTime, existMTime_ft);
        const FILETIME *existMTimePtr = &existMTime_ft;
#endif

        int qtAnswer = NOverwriteAnswer::kCancel;
        // BlockingQueuedConnection: worker blocks here until the GUI thread shows
        // the modal overwrite dialog and returns the chosen NOverwriteAnswer.
        QMetaObject::invokeMethod(OverwritePrompt, "Ask", Qt::BlockingQueuedConnection,
            Q_ARG(QString, WcsToQString(destPathSpec)),
            Q_ARG(QString, SizeToQString(&destFileInfo.Size)),
            Q_ARG(QString, TimeToQString(existMTimePtr)),
            Q_ARG(QString, WcsToQString(srcPath)),
            Q_ARG(QString, SizeToQString(srcSize)),
            Q_ARG(QString, TimeToQString(srcTime)),
            Q_ARG(int *, &qtAnswer));

        switch (qtAnswer)
        {
          case NOverwriteAnswer::kCancel: return E_ABORT;
          case NOverwriteAnswer::kNo: return S_OK;
          case NOverwriteAnswer::kNoToAll: OverwriteMode = NExtract::NOverwriteMode::kSkip; return S_OK;
          case NOverwriteAnswer::kYes: break;
          case NOverwriteAnswer::kYesToAll: OverwriteMode = NExtract::NOverwriteMode::kOverwrite; break;
          case NOverwriteAnswer::kAutoRename: OverwriteMode = NExtract::NOverwriteMode::kRename; break;
          default: return E_FAIL;
        }
        break;
      }
      default:
        break;
    }

    if (OverwriteMode == NExtract::NOverwriteMode::kRename)
    {
      if (!AutoRenamePath(destPathSys))
      {
        RINOK(MessageError(L"Cannot create name for file", destPathSys))
        return E_ABORT;
      }
      destPathResultTemp = fs2us(destPathSys);
    }
    else
    {
      if (NFind::DoesFileExist_Raw(destPathSys))
      if (!NDir::DeleteFileAlways(destPathSys))
      if (GetLastError() != ERROR_FILE_NOT_FOUND)
      {
        RINOK(MessageError(L"Cannot delete output file", destPathSys))
        return E_ABORT;
      }
    }
  }
  *writeAnswer = BoolToInt(true);
  return StringToBstr(destPathResultTemp, destPathResult);
}


// === Copy / Move worker (CPanelCopyThread::ProcessVirt analogue) ============
HRESULT QtFsCopyWorker::ProcessVirt()
{
  // Build the Qt IFolderOperationsExtractCallback exactly as CPanelCopyThread
  // builds its CExtractCallbackImp, then call CopyTo (PanelCopy.cpp:122).
  QtFsOperationCallback *cbSpec = new QtFsOperationCallback;
  CMyComPtr<IFolderOperationsExtractCallback> cb = cbSpec;
  cbSpec->ProgressDialog = ProgressDialog;
  cbSpec->OverwritePrompt = OverwritePrompt;
  cbSpec->MoveMode = MoveMode;
  cbSpec->OverwriteMode = NExtract::NOverwriteMode::kAsk;

  // Parent the GUI-thread overwrite prompt to the progress dialog Create() just
  // built (mirrors QtThreadExtracting::ProcessVirt). Storing a pointer / calling
  // SetParentWidget does not touch a widget from the worker thread.
  if (OverwritePrompt)
    OverwritePrompt->SetParentWidget(ProgressDialog);

  const HRESULT res = FolderOperations->CopyTo(
      BoolToInt(MoveMode),
      Indices.ConstData(), Indices.Size(),
      BoolToInt(false),   // includeAltStreams (no alt streams on Linux)
      BoolToInt(false),   // replaceAltStreamChars
      DestPath,
      cb);

  if (res == S_OK && cbSpec->ThereAreMessageErrors)
    return E_FAIL;
  return res;
}


// === Delete worker (CPanel::DeleteItems analogue) ===========================
HRESULT QtFsDeleteWorker::ProcessVirt()
{
  // B.5b : the original FM builds ONE CUpdateCallback100Imp and passes it to both
  // FS and archive Delete. We do the same with QtArchiveUpdateCallback: it is an
  // IProgress (so CFSFolder::Delete's SetTotal/SetCompleted work) AND QI-succeeds
  // to IFolderArchiveUpdateCallback (so CAgent::DeleteItems' unconditional
  // DeleteOperation deref does not NULL-crash). One callback serves both.
  QtArchiveUpdateCallback *cbSpec = new QtArchiveUpdateCallback;
  CMyComPtr<IProgress> progress = cbSpec;   // QIs to IFolderArchiveUpdateCallback
  cbSpec->ProgressDialog = ProgressDialog;

  return FolderOperations->Delete(Indices.ConstData(), Indices.Size(), progress);
}


// === Add-into-archive worker (CThreadUpdate::Process analogue) ==============
HRESULT QtArchiveAddWorker::ProcessVirt()
{
  // Mirror CThreadUpdate::Process (PanelCopy.cpp): build the callback, build the
  // const wchar_t* pointer array into ItemNames, call CopyFrom. The IProgress
  // must QI to IFolderArchiveUpdateCallback (the engine's CUpdateCallbackAgent
  // bridge QIs it for progress / crypto / errors).
  QtArchiveUpdateCallback *cbSpec = new QtArchiveUpdateCallback;
  CMyComPtr<IProgress> progress = cbSpec;
  cbSpec->ProgressDialog = ProgressDialog;
  cbSpec->PasswordPrompt = PasswordPrompt;
  if (PasswordPrompt)
    PasswordPrompt->SetParentWidget(ProgressDialog);

  CRecordVector<const wchar_t *> ptrs;
  FOR_VECTOR (i, ItemNames)
    ptrs.Add(ItemNames[i].Ptr());

  const HRESULT res = FolderOperations->CopyFrom(
      BoolToInt(MoveMode),
      FromFolderPrefix.Ptr(),
      ptrs.ConstData(), ptrs.Size(),
      progress);

  if (res == S_OK && cbSpec->ThereAreMessageErrors)
    return E_FAIL;
  return res;
}


// === Edit-writeback worker (G.2a : CThreadCopyFrom::ProcessVirt analogue) =====
HRESULT QtArchiveCopyFromWorker::ProcessVirt()
{
  // Mirror CThreadCopyFrom::ProcessVirt (PanelItemOpen.cpp:1009-1011):
  //   FolderOperations->CopyFromFile(ItemIndex, FullPath, UpdateCallback)
  // The IProgress must QI to IFolderArchiveUpdateCallback (CAgent's CopyFromFile
  // -> CommonUpdateOperation -> CUpdateCallbackAgent bridge QIs it for progress /
  // crypto / errors). Same single callback the Delete/Add workers build.
  QtArchiveUpdateCallback *cbSpec = new QtArchiveUpdateCallback;
  CMyComPtr<IProgress> progress = cbSpec;   // QIs to IFolderArchiveUpdateCallback
  cbSpec->ProgressDialog = ProgressDialog;
  cbSpec->PasswordPrompt = PasswordPrompt;
  if (PasswordPrompt)
    PasswordPrompt->SetParentWidget(ProgressDialog);

  const HRESULT res = FolderOperations->CopyFromFile(Index, FullPath.Ptr(), progress);

  if (res == S_OK && cbSpec->ThereAreMessageErrors)
    return E_FAIL;
  return res;
}


// === In-archive hash worker (G.2d : CRC/checksum of archived streams) ========
HRESULT QtArchiveHashWorker::ProcessVirt()
{
  // Faithful mirror of CPanel::CopyTo's stream-mode hash wiring (PanelCopy.cpp:
  // 239/255-268), with CAgentFolder::CopyTo as the operation (PanelCrc.cpp's
  // !Is_IO_FS_Folder branch). Build the same QtExtractCallback the drag-OUT path
  // uses, but turn ON StreamMode and attach the CHashBundle so the engine drives
  // GetStream7 (hash, no on-disk write). Own the ref through the QI-primary base
  // (IFolderArchiveExtractCallback), exactly like QtArchiveExtractWorker.
  QtExtractCallback *cbSpec = new QtExtractCallback;
  CMyComPtr<IFolderArchiveExtractCallback> cbHold = cbSpec;
  cbSpec->Init();
  cbSpec->ProgressDialog = ProgressDialog;
  cbSpec->PasswordPrompt = PasswordPrompt;
  cbSpec->OverwritePrompt = nullptr;   // stream-mode hash writes nothing -> no AskWrite
  cbSpec->OverwriteMode = NExtract::NOverwriteMode::kAsk;
  cbSpec->Src_Is_IO_FS_Folder = false; // source is an archive
  if (PasswordPrompt)
    PasswordPrompt->SetParentWidget(ProgressDialog);

  // CCopyToOptions.streamMode == true : make UseExtractToStream() return true so
  // the engine hands per-item output to the hash stream.
  cbSpec->StreamMode = true;
  cbSpec->SetHashMethods(&Hash);

  // FirstFileName/MainName : the original sets these from GetItemRelPath(indices[0])
  // when exactly one item is selected (PanelCopy.cpp:242). Marshalled in from the GUI.
  if (Indices.Size() == 1 && !FirstFileName.IsEmpty())
  {
    Hash.FirstFileName = FirstFileName;
    Hash.MainName = FirstFileName;
  }

  // dest path is unused for stream-mode hashing (nothing is written); pass empty.
  const HRESULT res = FolderOperations->CopyTo(
      BoolToInt(false),   // moveMode : CAgentFolder::CopyTo returns E_NOTIMPL for move
      Indices.ConstData(), Indices.Size(),
      BoolToInt(false),   // includeAltStreams
      BoolToInt(false),   // replaceAltStreamChars
      UString(),          // dest folder (unused in stream mode)
      cbSpec);            // IS-A IFolderOperationsExtractCallback (CopyTo param type)

  if (res == S_OK && cbSpec->ThereAreMessageErrors)
    return E_FAIL;
  return res;
}


// === Archive extract-to-temp worker (B.5c, drag-OUT) ========================
HRESULT QtArchiveExtractWorker::ProcessVirt()
{
  // Drive the archive extract via QtExtractCallback (implements BOTH
  // IFolderOperationsExtractCallback — the CopyTo param type — AND
  // IFolderArchiveExtractCallback, which CAgentFolder::CopyTo QIs and dispatches
  // to the engine Extract). Mirrors QtThreadExtracting::ProcessVirt's wiring.
  QtExtractCallback *cbSpec = new QtExtractCallback;
  // Own the ref through the QI-primary base (IFolderArchiveExtractCallback, the
  // Z7_COM_QI_BEGIN2 anchor) so Release()/delete go through the object's primary
  // pointer (no multiple-inheritance offset-delete). Pass cbSpec itself (IS-A
  // IFolderOperationsExtractCallback, the CopyTo param type) to the call.
  CMyComPtr<IFolderArchiveExtractCallback> cbHold = cbSpec;
  cbSpec->Init();
  cbSpec->ProgressDialog = ProgressDialog;
  cbSpec->OverwritePrompt = OverwritePrompt;
  cbSpec->PasswordPrompt = PasswordPrompt;
  cbSpec->OverwriteMode = NExtract::NOverwriteMode::kAsk;
  if (OverwritePrompt)
    OverwritePrompt->SetParentWidget(ProgressDialog);
  if (PasswordPrompt)
    PasswordPrompt->SetParentWidget(ProgressDialog);

  const HRESULT res = FolderOperations->CopyTo(
      BoolToInt(false),   // moveMode : CAgentFolder::CopyTo returns E_NOTIMPL for move
      Indices.ConstData(), Indices.Size(),
      BoolToInt(false),   // includeAltStreams
      BoolToInt(false),   // replaceAltStreamChars
      DestPath,
      cbSpec);            // IS-A IFolderOperationsExtractCallback (CopyTo param type)

  if (res == S_OK && cbSpec->ThereAreMessageErrors)
    return E_FAIL;
  return res;
}
