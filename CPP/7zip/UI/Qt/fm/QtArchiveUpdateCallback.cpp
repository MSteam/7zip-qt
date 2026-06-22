// QtArchiveUpdateCallback.cpp
// ----------------------------------------------------------------------------
// See QtArchiveUpdateCallback.h. Each method is the faithful Qt mirror of the
// corresponding CUpdateCallback100Imp body (FileManager/UpdateCallback100.cpp),
// with ProgressDialog->Sync.* unchanged and the two CUpdateCallbackGUI2 helpers
// folded inline:
//   * SetOperation_Base(notifyOp, name, isDir) -> Sync.Set_Status2(status, name)
//     (the original looks the op-name up in a lang table; headless we use a
//     short English status string — the routing to Set_Status2 is identical).
//   * ShowAskPasswordDialog() -> QtPasswordPrompt marshalled to the GUI thread
//     via Qt::BlockingQueuedConnection (the QtExtractCallback pattern).
// ----------------------------------------------------------------------------

#include "QtArchiveUpdateCallback.h"

#include <QtCore/QMetaObject>
#include <QtCore/QString>
#include <QtCore/Qt>

#include "../../../../Common/StringConvert.h"

#include "../QtProgressDialog.h"
#include "../QtExtractPrompts.h"
#include "../QtLang.h"                       // G.1i : FmLang(IDS_EXTRACT_MSG_*)
#include "../../GUI/ExtractRes.h"           // G.1i : IDS_EXTRACT_MSG_* langIDs


// Short status strings for the per-item operation (mirror the lang-table entries
// CUpdateCallbackGUI2 loads for NUpdateNotifyOp::{kAdd,kDelete,...}; routing is
// identical — Sync.Set_Status2(status, name, isDir)).
static UString OpStatusString(UInt32 notifyOp)
{
  // NUpdateNotifyOp ids (Common/Update.h): 0 kAdd, 1 kUpdate, 2 kAnalyze,
  // 3 kReplicate, 4 kRepack, 5 kSkipFiltered, 6 kDelete, 7 kHeader.
  switch (notifyOp)
  {
    case 0: return UString(L"Adding");
    case 1: return UString(L"Updating");
    case 2: return UString(L"Analyzing");
    case 3: return UString(L"Replicating");
    case 4: return UString(L"Repacking");
    case 6: return UString(L"Removing");
    case 7: return UString(L"Header");
    default: return UString();
  }
}

// SetOperation_Base inline (UpdateCallbackGUI2.cpp:40).
static HRESULT SetOperation(QtProgressDialog *pd, UInt32 notifyOp,
    const wchar_t *name, bool isDir)
{
  return pd->Sync.Set_Status2(OpStatusString(notifyOp), name, isDir);
}


// === IProgress (UpdateCallback100.cpp:28,34) ================================
Z7_COM7F_IMF(QtArchiveUpdateCallback::SetTotal(UInt64 size))
{
  ProgressDialog->Sync.Set_NumBytesTotal(size);
  return S_OK;
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::SetCompleted(const UInt64 *completed))
{
  return ProgressDialog->Sync.Set_NumBytesCur(completed);
}


// === IFolderArchiveUpdateCallback (UpdateCallback100.cpp:23,45,50,55,79) =====
Z7_COM7F_IMF(QtArchiveUpdateCallback::SetNumFiles(UInt64 numFiles))
{
  return ProgressDialog->Sync.Set_NumFilesTotal(numFiles);
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::CompressOperation(const wchar_t *name))
{
  // NUpdateNotifyOp::kAdd (0)
  return SetOperation(ProgressDialog, 0, name, false);
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::DeleteOperation(const wchar_t *name))
{
  // NUpdateNotifyOp::kDelete (6). Called UNCONDITIONALLY by CAgent::DeleteItems
  // (AgentOut.cpp) — must be a safe, non-crashing body.
  return SetOperation(ProgressDialog, 6, name, false);
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::OperationResult(Int32 /* operationResult */))
{
  ProgressDialog->Sync.Set_NumFilesCur(++NumFiles);
  return S_OK;
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::UpdateErrorMessage(const wchar_t *message))
{
  ThereAreMessageErrors = true;
  ProgressDialog->Sync.AddError_Message(message);
  return S_OK;
}


// === IFolderArchiveUpdateCallback2 (UpdateCallback100.cpp:85,91,63,74) =======
Z7_COM7F_IMF(QtArchiveUpdateCallback::OpenFileError(const wchar_t *path, HRESULT errorCode))
{
  ThereAreMessageErrors = true;
  ProgressDialog->Sync.AddError_Code_Name(errorCode, path);
  return S_OK;
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::ReadingFileError(const wchar_t *path, HRESULT errorCode))
{
  ThereAreMessageErrors = true;
  ProgressDialog->Sync.AddError_Code_Name(errorCode, path);
  return S_OK;
}

// (G.1i) Faithful mirror of FileManager/ExtractCallback.cpp's SetExtractErrorMessage
// (UpdateCallback100.cpp uses the shared FileManager symbol; the Qt port keeps a
// per-TU copy, as QtUpdateCallback.cpp / QtExtractCallback.cpp do): base message via
// FmLang(IDS_EXTRACT_MSG_*, <inline English == .rc text>); for an encrypted-but-not-
// WrongPassword result append " : " + IDS_EXTRACT_MSG_WRONG_PSW_GUESS; then
// " : " + fileName.
static void Qt_SetExtractErrorMessage(Int32 opRes, Int32 encrypted, const wchar_t *fileName, UString &dest)
{
  dest.Empty();
  if (opRes == NArchive::NExtract::NOperationResult::kOK)
    return;
  unsigned id = 0;
  QString english;
  switch (opRes)
  {
    case NArchive::NExtract::NOperationResult::kUnsupportedMethod:
      id = IDS_EXTRACT_MSG_UNSUPPORTED_METHOD; english = QStringLiteral("Unsupported Method"); break;
    case NArchive::NExtract::NOperationResult::kCRCError:
      id = IDS_EXTRACT_MSG_CRC_ERROR; english = QStringLiteral("CRC Failed"); break;
    case NArchive::NExtract::NOperationResult::kDataError:
      id = IDS_EXTRACT_MSG_DATA_ERROR; english = QStringLiteral("Data Error"); break;
    case NArchive::NExtract::NOperationResult::kUnavailable:
      id = IDS_EXTRACT_MSG_UNAVAILABLE_DATA; english = QStringLiteral("Unavailable data"); break;
    case NArchive::NExtract::NOperationResult::kUnexpectedEnd:
      id = IDS_EXTRACT_MSG_UEXPECTED_END; english = QStringLiteral("Unexpected end of data"); break;
    case NArchive::NExtract::NOperationResult::kDataAfterEnd:
      id = IDS_EXTRACT_MSG_DATA_AFTER_END; english = QStringLiteral("There are some data after the end of the payload data"); break;
    case NArchive::NExtract::NOperationResult::kIsNotArc:
      id = IDS_EXTRACT_MSG_IS_NOT_ARC; english = QStringLiteral("Is not archive"); break;
    case NArchive::NExtract::NOperationResult::kHeadersError:
      id = IDS_EXTRACT_MSG_HEADERS_ERROR; english = QStringLiteral("Headers Error"); break;
    case NArchive::NExtract::NOperationResult::kWrongPassword:
      id = IDS_EXTRACT_MSG_WRONG_PSW_CLAIM; english = QStringLiteral("Wrong password"); break;
    default: break;
  }
  if (id != 0)
  {
    const QByteArray u8 = FmLang(id, english).toUtf8();
    dest += GetUnicodeString(AString(u8.constData()));
  }
  else
  {
    dest += "Error #";
    dest.Add_UInt32((UInt32)opRes);
  }

  if (encrypted && opRes != NArchive::NExtract::NOperationResult::kWrongPassword)
  {
    dest += " : ";
    const QByteArray u8 = FmLang(IDS_EXTRACT_MSG_WRONG_PSW_GUESS,
        QStringLiteral("Wrong password?")).toUtf8();
    dest += GetUnicodeString(AString(u8.constData()));
  }

  dest += " : ";
  dest += fileName;
}

// Faithful to UpdateCallback100.cpp:63-72.
Z7_COM7F_IMF(QtArchiveUpdateCallback::ReportExtractResult(Int32 opRes, Int32 isEncrypted, const wchar_t *name))
{
  if (opRes != NArchive::NExtract::NOperationResult::kOK)
  {
    UString s;
    Qt_SetExtractErrorMessage(opRes, isEncrypted, name, s);
    ProgressDialog->Sync.AddError_Message(s);
  }
  return S_OK;
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::ReportUpdateOperation(UInt32 notifyOp, const wchar_t *name, Int32 isDir))
{
  return SetOperation(ProgressDialog, notifyOp, name, IntToBool(isDir));
}


// === IFolderScanProgress (UpdateCallback100.cpp:12,17) ======================
Z7_COM7F_IMF(QtArchiveUpdateCallback::ScanProgress(UInt64 /* numFolders */, UInt64 numFiles, UInt64 totalSize, const wchar_t *path, Int32 /* isDir */))
{
  return ProgressDialog->Sync.ScanProgress(numFiles, totalSize, us2fs(path));
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::ScanError(const wchar_t *path, HRESULT errorCode))
{
  ThereAreMessageErrors = true;
  ProgressDialog->Sync.AddError_Code_Name(errorCode, path);
  return S_OK;
}


// === ICompressProgressInfo (UpdateCallback100.cpp:39) =======================
Z7_COM7F_IMF(QtArchiveUpdateCallback::SetRatioInfo(const UInt64 *inSize, const UInt64 *outSize))
{
  ProgressDialog->Sync.Set_Ratio(inSize, outSize);
  return S_OK;
}


// === IArchiveOpenCallback (UpdateCallback100.cpp:106,111) ===================
// These fire during the post-update ReOpen() of the rebuilt archive.
Z7_COM7F_IMF(QtArchiveUpdateCallback::SetTotal(const UInt64 * /* files */, const UInt64 * /* bytes */))
{
  return S_OK;
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::SetCompleted(const UInt64 * /* files */, const UInt64 * /* bytes */))
{
  return ProgressDialog->Sync.CheckStop();
}


// === IFolderArchiveUpdateCallback_MoveArc (UpdateCallback100.cpp:117-136) ====
// The temp->original move inside CommonUpdateOperation. Non-crashing bodies; the
// contract is only that Before_ArcReopen() clears the stop status so the engine
// can ReOpen() the rebuilt archive (IFolderArchive.h:112).
Z7_COM7F_IMF(QtArchiveUpdateCallback::MoveArc_Start(const wchar_t * /* srcTempPath */, const wchar_t *destFinalPath, UInt64 /* size */, Int32 /* updateMode */))
{
  return ProgressDialog->Sync.Set_Status2(UString(L"Moving"), destFinalPath, false);
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::MoveArc_Progress(UInt64 /* totalSize */, UInt64 /* currentSize */))
{
  return ProgressDialog->Sync.CheckStop();
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::MoveArc_Finish())
{
  return ProgressDialog->Sync.Set_Status2(UString(), L"", false);
}

Z7_COM7F_IMF(QtArchiveUpdateCallback::Before_ArcReopen())
{
  ProgressDialog->Sync.Clear_Stop_Status();
  return S_OK;
}


// === ICryptoGetTextPassword2 (UpdateCallback100.cpp:97) =====================
// Re-supplies the already-defined password when re-packing an encrypted archive;
// no prompt here (cpp:97).
Z7_COM7F_IMF(QtArchiveUpdateCallback::CryptoGetTextPassword2(Int32 *passwordIsDefined, BSTR *password))
{
  *password = NULL;
  *passwordIsDefined = BoolToInt(PasswordIsDefined);
  if (!PasswordIsDefined)
    return S_OK;
  return StringToBstr(Password, password);
}


// === ICryptoGetTextPassword (UpdateCallback100.cpp:139) =====================
// Invoked only for encrypted targets. Mirror: if no password is defined, prompt
// (here via QtPasswordPrompt on the GUI thread), else return the cached one. If
// there is no prompt (headless / not wired), a missing password is E_ABORT
// (== the original's IDCANCEL path), matching CUpdateCallbackGUI2::ShowAskPassword
// Dialog returning E_ABORT.
Z7_COM7F_IMF(QtArchiveUpdateCallback::CryptoGetTextPassword(BSTR *password))
{
  *password = NULL;
  if (!PasswordIsDefined)
  {
    if (!PasswordPrompt)
      return E_ABORT;
    QString pw;
    bool accepted = false;
    QMetaObject::invokeMethod(PasswordPrompt, "Ask", Qt::BlockingQueuedConnection,
        Q_ARG(QString *, &pw),
        Q_ARG(bool *, &accepted));
    if (!accepted)
      return E_ABORT;
    const std::wstring w = pw.toStdWString();
    Password = UString(w.c_str());
    PasswordIsDefined = true;
  }
  return StringToBstr(Password, password);
}
