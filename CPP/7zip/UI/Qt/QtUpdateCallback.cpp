// QtUpdateCallback.cpp
//
// Implementation of the Qt update (compression) callback. Method-by-method this
// mirrors UpdateCallbackGUI.cpp / UpdateCallbackGUI2.cpp (the GUI template) and
// UpdateCallbackConsole.cpp (the HWND-free reference), substituting:
//   ProgressDialog->Sync          (kept — same shared CProgressSync object)
//   LangString(IDS_*)             -> English literals (no lang table on Linux)
//   CPasswordDialog (Win32)       -> QtPasswordPrompt (GUI thread, BlockingQueued)
//   OpenResult_GUI prose          -> a trimmed accumulated error message
//
// The control flow / Sync routing / password caching are kept faithful.

#include "QtUpdateCallback.h"

#include <QtCore/QMetaObject>
#include <QtCore/QString>
#include <QtCore/Qt>

#include "../../../Common/IntToString.h"
#include "../../../Common/StringConvert.h"

#include "../../../Windows/PropVariant.h"

#include "../../Archive/IArchive.h" // NArchive::NExtract::NOperationResult

#include "QtProgressDialog.h"
#include "QtExtractPrompts.h" // REUSE the shared GUI-thread QtPasswordPrompt
#include "QtLang.h"                        // G.1i : FmLang(IDS_EXTRACT_MSG_*)
#include "../GUI/ExtractRes.h"            // G.1i : IDS_EXTRACT_MSG_* langIDs
#include "QtOpenArcError.h"               // G.3c : Qt_GetOpenArcErrorMessage (flag decode)

using namespace NWindows;


// === CUpdateCallbackGUI::Init + CUpdateCallbackGUI2::Init mirror =============
// The original loads the per-op status strings from the lang table:
//   k_UpdNotifyLangs[] = { ADD, UPDATE, ANALYZE, REPLICATE, REPACK, SKIPPING,
//                          DELETE, HEADER }
// (UpdateCallbackGUI2.cpp). We use English literals in the SAME order, because
// SetOperation_Base indexes this vector by the engine's NUpdateNotifyOp code.
void QtUpdateCallback::Init()
{
  NumFiles = 0;
  FailedFiles.Clear();

  _lang_Removing = "Removing";
  _lang_Moving = "Moving";
  _lang_Ops.Clear();
  _lang_Ops.Add(UString("Adding"));      // kAdd
  _lang_Ops.Add(UString("Updating"));    // kUpdate
  _lang_Ops.Add(UString("Analyzing"));   // kAnalyze
  _lang_Ops.Add(UString("Replicating")); // kReplicate
  _lang_Ops.Add(UString("Repacking"));   // kRepack
  _lang_Ops.Add(UString("Skipping"));    // kSkip
  _lang_Ops.Add(UString("Deleting"));    // kDelete
  _lang_Ops.Add(UString("Header"));      // kHeader
}


// === CUpdateCallbackGUI2::SetOperation_Base ================================
HRESULT QtUpdateCallback::SetOperation_Base(UInt32 notifyOp, const wchar_t *name, bool isDir)
{
  const UString *s = NULL;
  if (notifyOp < _lang_Ops.Size())
    s = &(_lang_Ops[(unsigned)notifyOp]);
  else
    s = &_emptyString;
  return ProgressDialog->Sync.Set_Status2(*s, name, isDir);
}


// === CUpdateCallbackGUI2::ShowAskPasswordDialog -> QtPasswordPrompt ==========
// Mirrors:
//   CPasswordDialog dialog;
//   if (dialog.Create(*ProgressDialog) != IDOK) return E_ABORT;
//   Password = dialog.Password; PasswordIsDefined = true;
HRESULT QtUpdateCallback::ShowAskPasswordDialog()
{
  QString pw;
  bool accepted = false;
  // BlockingQueuedConnection: the worker BLOCKS until the GUI thread returns the
  // password (exactly the re-entrancy the Win32 dialog.Create(*ProgressDialog)
  // provides). See QtExtractPrompts.h for the threading contract.
  QMetaObject::invokeMethod(PasswordPrompt, "Ask", Qt::BlockingQueuedConnection,
      Q_ARG(QString *, &pw),
      Q_ARG(bool *, &accepted));
  if (!accepted)
    return E_ABORT;
  {
    const QByteArray utf8 = pw.toUtf8();
    Password = GetUnicodeString(AString(utf8.constData()));
  }
  PasswordIsDefined = true;
  return S_OK;
}


// === IUpdateCallbackUI2 : OpenResult ========================================
// Trimmed mirror of CUpdateCallbackGUI::OpenResult (which delegates to
// OpenResult_GUI). We surface any open error as a single accumulated message.
HRESULT QtUpdateCallback::OpenResult(
    const CCodecs * /* codecs */, const CArchiveLink &arcLink,
    const wchar_t *name, HRESULT result)
{
  // G.3c : mirror ErrorInfo_Print (ExtractCallback.cpp:512) — surface the DECODED
  // error flags, the ErrorMessage, the DECODED warning flags AND the
  // WarningMessage independently. The previous code gated the whole level on
  // !er.ErrorMessage.IsEmpty(), so a warning-only level (warning FLAGS or a
  // WarningMessage but no ErrorMessage string) surfaced NOTHING.
  FOR_VECTOR (level, arcLink.Arcs)
  {
    const CArc &arc = arcLink.Arcs[level];
    const CArcErrorInfo &er = arc.ErrorInfo;
    if (!er.IsThereErrorOrWarning())
      continue;

    const UInt32 errorFlags = er.GetErrorFlags();
    if (errorFlags != 0)
      ProgressDialog->Sync.AddError_Message(Qt_GetOpenArcErrorMessage(errorFlags));
    if (!er.ErrorMessage.IsEmpty())
      ProgressDialog->Sync.AddError_Message(er.ErrorMessage);

    const UInt32 warningFlags = er.GetWarningFlags();
    if (warningFlags != 0)
    {
      UString w("Warnings:");
      w.Add_LF();
      w += Qt_GetOpenArcErrorMessage(warningFlags);
      ProgressDialog->Sync.AddError_Message(w);
    }
    if (!er.WarningMessage.IsEmpty())
    {
      UString w("Warning: ");
      w += er.WarningMessage;
      ProgressDialog->Sync.AddError_Message(w);
    }
  }
  if (result != S_OK)
  {
    UString s = name;
    s.Add_LF();
    if (result == S_FALSE)
      s += "Cannot open the file as archive";
    else
      s += NError::MyFormatMessage(result);
    ProgressDialog->Sync.AddError_Message(s);
  }
  return S_OK;
}


// === IUpdateCallbackUI2 : scanning ==========================================
HRESULT QtUpdateCallback::StartScanning()
{
  ProgressDialog->Sync.Set_Status(UString("Scanning"));
  return S_OK;
}

HRESULT QtUpdateCallback::ScanProgress(const CDirItemsStat &st, const FString &path, bool isDir)
{
  return ProgressDialog->Sync.ScanProgress(st.NumFiles + st.NumAltStreams,
      st.GetTotalBytes(), path, isDir);
}

HRESULT QtUpdateCallback::ScanError(const FString &path, DWORD systemError)
{
  FailedFiles.Add(path);
  ProgressDialog->Sync.AddError_Code_Name(HRESULT_FROM_WIN32(systemError), fs2us(path));
  return S_OK;
}

HRESULT QtUpdateCallback::FinishScanning(const CDirItemsStat &st)
{
  RINOK(ProgressDialog->Sync.ScanProgress(st.NumFiles + st.NumAltStreams,
      st.GetTotalBytes(), FString(), true))
  ProgressDialog->Sync.Set_Status(L"");
  return S_OK;
}


// === IUpdateCallbackUI2 : archive open/start/finish =========================
HRESULT QtUpdateCallback::StartOpenArchive(const wchar_t * /* name */)
{
  return S_OK;
}

HRESULT QtUpdateCallback::StartArchive(const wchar_t *name, bool /* updating */)
{
  ProgressDialog->Sync.Set_Status(UString("Compressing"));
  ProgressDialog->Sync.Set_TitleFileName(name);
  return S_OK;
}

HRESULT QtUpdateCallback::FinishArchive(const CFinishArchiveStat & /* st */)
{
  ProgressDialog->Sync.Set_Status(L"");
  return S_OK;
}


// === IUpdateCallbackUI : progress ===========================================
HRESULT QtUpdateCallback::CheckBreak()
{
  return ProgressDialog->Sync.CheckStop();
}

HRESULT QtUpdateCallback::SetNumItems(const CArcToDoStat &stat)
{
  ProgressDialog->Sync.Set_NumFilesTotal(stat.Get_NumDataItems_Total());
  return S_OK;
}

HRESULT QtUpdateCallback::SetTotal(UInt64 total)
{
  ProgressDialog->Sync.Set_NumBytesTotal(total);
  return S_OK;
}

HRESULT QtUpdateCallback::SetCompleted(const UInt64 *completed)
{
  return ProgressDialog->Sync.Set_NumBytesCur(completed);
}

HRESULT QtUpdateCallback::SetRatioInfo(const UInt64 *inSize, const UInt64 *outSize)
{
  ProgressDialog->Sync.Set_Ratio(inSize, outSize);
  return CheckBreak();
}

HRESULT QtUpdateCallback::GetStream(const wchar_t *name, bool isDir, bool /* isAnti */, UInt32 mode)
{
  return SetOperation_Base(mode, name, isDir);
}

HRESULT QtUpdateCallback::OpenFileError(const FString &path, DWORD systemError)
{
  FailedFiles.Add(path);
  ProgressDialog->Sync.AddError_Code_Name(HRESULT_FROM_WIN32(systemError), fs2us(path));
  return S_FALSE;
}

HRESULT QtUpdateCallback::ReadingFileError(const FString &path, DWORD systemError)
{
  FailedFiles.Add(path);
  ProgressDialog->Sync.AddError_Code_Name(HRESULT_FROM_WIN32(systemError), fs2us(path));
  return S_OK;
}

HRESULT QtUpdateCallback::SetOperationResult(Int32 /* operationResult */)
{
  NumFiles++;
  ProgressDialog->Sync.Set_NumFilesCur(NumFiles);
  return S_OK;
}

HRESULT QtUpdateCallback::ReportUpdateOperation(UInt32 op, const wchar_t *name, bool isDir)
{
  return SetOperation_Base(op, name, isDir);
}

HRESULT QtUpdateCallback::WriteSfx(const wchar_t * /* name */, UInt64 /* size */)
{
  ProgressDialog->Sync.Set_Status(L"WriteSfx");
  return S_OK;
}


// === per-file extract-result reporting (used during update repack) ==========
// (G.1i) Faithful mirror of FileManager/ExtractCallback.cpp's SetExtractErrorMessage
// (same logic as QtExtractCallback.cpp): base message via FmLang(IDS_EXTRACT_MSG_*,
// <inline English == .rc text>); for an encrypted-but-not-WrongPassword result append
// " : " + IDS_EXTRACT_MSG_WRONG_PSW_GUESS; then " : " + fileName.
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

HRESULT QtUpdateCallback::ReportExtractResult(Int32 opRes, Int32 isEncrypted, const wchar_t *name)
{
  if (opRes != NArchive::NExtract::NOperationResult::kOK)
  {
    UString s;
    Qt_SetExtractErrorMessage(opRes, isEncrypted, name, s);
    ProgressDialog->Sync.AddError_Message(s);
  }
  return S_OK;
}


// === password (ICryptoGetTextPassword[2]) : mirror CUpdateCallbackGUI ========
HRESULT QtUpdateCallback::CryptoGetTextPassword2(Int32 *passwordIsDefined, BSTR *password)
{
  *password = NULL;
  if (passwordIsDefined)
    *passwordIsDefined = BoolToInt(PasswordIsDefined);
  if (!PasswordIsDefined)
  {
    if (AskPassword)
    {
      RINOK(ShowAskPasswordDialog())
    }
  }
  if (passwordIsDefined)
    *passwordIsDefined = BoolToInt(PasswordIsDefined);
  return StringToBstr(Password, password);
}

HRESULT QtUpdateCallback::CryptoGetTextPassword(BSTR *password)
{
  return CryptoGetTextPassword2(NULL, password);
}


// === IUpdateCallbackUI2 : delete-after-archiving ============================
HRESULT QtUpdateCallback::ShowDeleteFile(const wchar_t *name, bool isDir)
{
  // NUpdateNotifyOp::kDelete == index 6 in _lang_Ops.
  return SetOperation_Base(6, name, isDir);
}

HRESULT QtUpdateCallback::FinishDeletingAfterArchiving()
{
  return S_OK;
}

HRESULT QtUpdateCallback::DeletingAfterArchiving(const FString &path, bool isDir)
{
  return ProgressDialog->Sync.Set_Status2(_lang_Removing, fs2us(path), isDir);
}


// === IUpdateCallbackUI2 : MoveArc (mirror CUpdateCallbackGUI2) ===============
HRESULT QtUpdateCallback::MoveArc_UpdateStatus()
{
  UString s;
  s.Add_UInt64(_arcMoving_percents);
  s.Add_Char('%');
  const bool totalDefined = (_arcMoving_total != 0 && _arcMoving_total != (UInt64)(Int64)-1);
  if (totalDefined || _arcMoving_current != 0)
  {
    s += " : ";
    s.Add_UInt64(_arcMoving_current >> 20);
    s += " MiB";
  }
  if (totalDefined)
  {
    s += " / ";
    s.Add_UInt64((_arcMoving_total + ((1 << 20) - 1)) >> 20);
    s += " MiB";
  }
  s += " : ";
  s += _lang_Moving;
  s += " : ";
  s += _arcMoving_name1;
  return ProgressDialog->Sync.Set_Status2(s, _arcMoving_name2, false);
}

HRESULT QtUpdateCallback::MoveArc_Start_Base(const wchar_t *srcTempPath, const wchar_t *destFinalPath, UInt64 totalSize, Int32 updateMode)
{
  _arcMoving_percents = 0;
  _arcMoving_total = totalSize;
  _arcMoving_current = 0;
  _arcMoving_updateMode = updateMode;
  _arcMoving_name1 = srcTempPath;
  _arcMoving_name2 = destFinalPath;
  return MoveArc_UpdateStatus();
}

HRESULT QtUpdateCallback::MoveArc_Progress_Base(UInt64 totalSize, UInt64 currentSize)
{
  _arcMoving_total = totalSize;
  _arcMoving_current = currentSize;
  UInt64 percents = 0;
  if (totalSize != 0)
  {
    if (totalSize < ((UInt64)1 << 57))
      percents = currentSize * 100 / totalSize;
    else
      percents = currentSize / (totalSize / 100);
  }
  if (percents == _arcMoving_percents)
    return ProgressDialog->Sync.CheckStop();
  _arcMoving_percents = percents;
  return MoveArc_UpdateStatus();
}

HRESULT QtUpdateCallback::MoveArc_Finish_Base()
{
  return ProgressDialog->Sync.Set_Status2(L"", L"", false);
}

HRESULT QtUpdateCallback::MoveArc_Start(const wchar_t *srcTempPath, const wchar_t *destFinalPath, UInt64 totalSize, Int32 updateMode)
{
  return MoveArc_Start_Base(srcTempPath, destFinalPath, totalSize, updateMode);
}
HRESULT QtUpdateCallback::MoveArc_Progress(UInt64 totalSize, UInt64 currentSize)
{
  return MoveArc_Progress_Base(totalSize, currentSize);
}
HRESULT QtUpdateCallback::MoveArc_Finish()
{
  return MoveArc_Finish_Base();
}


// === IOpenCallbackUI (mirror CUpdateCallbackGUI Open_*) =====================
HRESULT QtUpdateCallback::Open_CheckBreak()
{
  return ProgressDialog->Sync.CheckStop();
}

HRESULT QtUpdateCallback::Open_SetTotal(const UInt64 * /* numFiles */, const UInt64 * /* numBytes */)
{
  return S_OK;
}

HRESULT QtUpdateCallback::Open_SetCompleted(const UInt64 * /* numFiles */, const UInt64 * /* numBytes */)
{
  return ProgressDialog->Sync.CheckStop();
}

HRESULT QtUpdateCallback::Open_Finished()
{
  return S_OK;
}

#ifndef Z7_NO_CRYPTO
HRESULT QtUpdateCallback::Open_CryptoGetTextPassword(BSTR *password)
{
  PasswordWasAsked = true;
  return CryptoGetTextPassword2(NULL, password);
}
#endif
