// QtExtractCallback.cpp
//
// Implementation of the Qt extraction callback. Method-by-method, this mirrors
// the bodies of Console/ExtractCallbackConsole.cpp (the HWND-free primary
// template) and FileManager/ExtractCallback.cpp (the GUI sibling), substituting:
//   console g_StdOut / percent printer  -> ProgressDialog->Sync (progress)
//   console error stream                -> Sync.AddError_Message* (errors)
//   stdin GetPassword                   -> QtPasswordPrompt (GUI thread)
//   Win32 COverwriteDialog              -> QtOverwritePrompt (GUI thread)
// The control flow / answer mapping / error-accumulation are kept faithful.

#include "QtExtractCallback.h"

#include <QtCore/QMetaObject>
#include <QtCore/QString>
#include <QtCore/Qt>

#include "../../../Common/ComTry.h"
#include "../../../Common/IntToString.h"
#include "../../../Common/StringConvert.h"

#include "../../PropID.h"                  // G.2d : kpidSize / kpidIsAltStream
#include "../../IDecl.h"

#include "../../../Windows/ErrorMsg.h"
#include "../../../Windows/PropVariantConv.h"
#include "../../../Windows/FileDir.h"     // B.5c : DeleteFileAlways (AskWrite)
#include "../../../Windows/FileFind.h"    // B.5c : CFileInfo (AskWrite)
#include "../../../Windows/TimeUtils.h"   // B.5c : FiTime_To_FILETIME (CFiTime -> FILETIME)

#include "../../../7zip/Common/FilePathAutoRename.h" // B.5c : AutoRenamePath

#include "QtProgressDialog.h"
#include "QtExtractPrompts.h"
#include "QtMemDialog.h"                  // G.8a : memory-limit request prompt
#include "QtExtractSettings.h"            // G.8a/G.9a : NExtractQt::Read_LimitGB (the GB-limit store)
#include "QtLang.h"                        // G.1i : FmLang(IDS_EXTRACT_MSG_*)
#include "../GUI/ExtractRes.h"            // G.1i : IDS_EXTRACT_MSG_* langIDs
#include "../FileManager/resourceGui.h"  // G.8a : IDS_MEM_* / IDS_MSG_ARC_UNPACKING_WAS_SKIPPED
#include "QtOpenArcError.h"               // G.3c : Qt_GetOpenArcErrorMessage (flag decode)

#include <QtCore/QMetaObject>             // G.8a : marshal the MemDialog to the GUI thread
#include <QtCore/QString>

using namespace NWindows;


// === helpers for marshalling FILETIME/size to the prompt strings ============
static QString SizeToQString(const UInt64 *size)
{
  if (!size || *size == (UInt64)(Int64)-1)
    return QString();
  char temp[32];
  ConvertUInt64ToString(*size, temp);
  QString s = QString::fromLatin1(temp);
  s += QStringLiteral(" bytes");
  return s;
}

static QString TimeToQString(const FILETIME *ft)
{
  if (!ft || (ft->dwHighDateTime == 0 && ft->dwLowDateTime == 0))
    return QString();
  char temp[64];
  // mirrors PrintFileInfo's ConvertUtcFileTimeToString in the console template
  if (ConvertUtcFileTimeToString(*ft, temp, kTimestampPrintLevel_SEC))
    return QString::fromLatin1(temp);
  return QString();
}

// wchar_t* -> QString. On Linux wchar_t is 32-bit (UTF-32); QString::fromWCharArray
// handles that width correctly. Used to marshal engine UString paths to the GUI.
static QString WcsToQString(const wchar_t *s)
{
  if (!s)
    return QString();
  return QString::fromWCharArray(s);
}


// === CExtractCallbackImp::Init mirror =======================================
void QtExtractCallback::Init()
{
  // The GUI template loads these from the lang table; use English defaults.
  _lang_Extracting = "Extracting";
  _lang_Testing = "Testing";
  _lang_Skipping = "Skipping";
  _lang_Reading = "Reading";

  NumArchiveErrors = 0;
  ThereAreMessageErrors = false;
  NumFolders = NumFiles = 0;
}


// === error accumulation : mirror CExtractCallbackImp::AddError_Message* ======
void QtExtractCallback::AddError_Message(LPCWSTR s)
{
  ThereAreMessageErrors = true;
  ProgressDialog->Sync.AddError_Message(s);
}

void QtExtractCallback::AddError_Message_ShowArcPath(LPCWSTR s)
{
  Add_ArchiveName_Error();
  AddError_Message(s);
}

void QtExtractCallback::Add_ArchiveName_Error()
{
  if (_needWriteArchivePath)
  {
    if (!_currentArchivePath.IsEmpty())
      AddError_Message(_currentArchivePath);
    _needWriteArchivePath = false;
  }
}

HRESULT QtExtractCallback::MessageError(const wchar_t *message, const FString &path)
{
  ThereAreMessageErrors = true;
  ProgressDialog->Sync.AddError_Message_Name(message, fs2us(path));
  return S_OK;
}


// === IProgress : mirror CExtractCallbackImp::SetTotal/SetCompleted ==========
Z7_COM7F_IMF(QtExtractCallback::SetTotal(UInt64 total))
{
  // G.8c : the EXTRACT phase reports a byte total — leave files-progress-mode
  // behind. In the original FM these are two separate dialogs (the open dialog's
  // _filesProgressMode is independent of the extract dialog, which stays false);
  // here one Sync spans both phases, so a prior open-phase Set_FilesProgressMode(true)
  // must be cleared once byte-based extract progress begins, matching the
  // original's byte-based extract bar.
  ProgressDialog->Sync.Set_FilesProgressMode(false);
  ProgressDialog->Sync.Set_NumBytesTotal(total);
  return S_OK;
}

Z7_COM7F_IMF(QtExtractCallback::SetCompleted(const UInt64 *value))
{
  return ProgressDialog->Sync.Set_NumBytesCur(value);
}


// === IOpenCallbackUI : mirror CExtractCallbackImp::Open_* ====================
HRESULT QtExtractCallback::Open_CheckBreak()
{
  return ProgressDialog->Sync.CheckStop();
}

HRESULT QtExtractCallback::Open_SetTotal(const UInt64 *files, const UInt64 *bytes)
{
  HRESULT res = S_OK;
  if (!MultiArcMode)
  {
    if (files)
      _totalFiles_Defined = true;
    else
      _totalFiles_Defined = false;

    // G.8c : mirror COpenArchiveCallback::Open_SetTotal (OpenCallback.cpp:30) —
    // put the progress bar into FILES-count mode whenever the format reports a
    // file count (files != NULL), so during the OPEN phase the bar advances by
    // file count when the format gives files but no byte total.
    ProgressDialog->Sync.Set_FilesProgressMode(files != NULL);

    if (bytes)
    {
      _totalBytes_Defined = true;
      ProgressDialog->Sync.Set_NumBytesTotal(*bytes);
    }
    else
      _totalBytes_Defined = false;
  }
  return res;
}

HRESULT QtExtractCallback::Open_SetCompleted(const UInt64 *files, const UInt64 * /* bytes */)
{
  if (!MultiArcMode)
  {
    if (files)
      ProgressDialog->Sync.Set_NumFilesCur(*files);
  }
  return ProgressDialog->Sync.CheckStop();
}

HRESULT QtExtractCallback::Open_Finished()
{
  return ProgressDialog->Sync.CheckStop();
}

#ifndef Z7_NO_CRYPTO
HRESULT QtExtractCallback::Open_CryptoGetTextPassword(BSTR *password)
{
  return CryptoGetTextPassword(password);
}
#endif


// === IFolderArchiveExtractCallback::AskOverwrite ============================
// Mirrors CExtractCallbackConsole::AskOverwrite (primary template): show the two
// files, ask, map the answer to NOverwriteAnswer, return it. The engine
// (ArchiveExtractCallback.cpp) owns the OverwriteMode state machine and the
// "...to all" effect; this method only returns the user's answer (exactly like
// the console template). The question is marshalled to the GUI thread.
Z7_COM7F_IMF(QtExtractCallback::AskOverwrite(
    const wchar_t *existName, const FILETIME *existTime, const UInt64 *existSize,
    const wchar_t *newName, const FILETIME *newTime, const UInt64 *newSize,
    Int32 *answer))
{
  RINOK(ProgressDialog->Sync.CheckStop())

  int qtAnswer = NOverwriteAnswer::kCancel;

  // BlockingQueuedConnection: the worker BLOCKS here until the GUI thread shows
  // the modal dialog and returns the answer. (See QtExtractPrompts.h.)
  QMetaObject::invokeMethod(OverwritePrompt, "Ask", Qt::BlockingQueuedConnection,
      Q_ARG(QString, WcsToQString(existName)),
      Q_ARG(QString, SizeToQString(existSize)),
      Q_ARG(QString, TimeToQString(existTime)),
      Q_ARG(QString, WcsToQString(newName)),
      Q_ARG(QString, SizeToQString(newSize)),
      Q_ARG(QString, TimeToQString(newTime)),
      Q_ARG(int *, &qtAnswer));

  switch (qtAnswer)
  {
    case NOverwriteAnswer::kCancel:    *answer = NOverwriteAnswer::kCancel; return E_ABORT;
    case NOverwriteAnswer::kYes:       *answer = NOverwriteAnswer::kYes; break;
    case NOverwriteAnswer::kYesToAll:  *answer = NOverwriteAnswer::kYesToAll; break;
    case NOverwriteAnswer::kNo:        *answer = NOverwriteAnswer::kNo; break;
    case NOverwriteAnswer::kNoToAll:   *answer = NOverwriteAnswer::kNoToAll; break;
    case NOverwriteAnswer::kAutoRename:*answer = NOverwriteAnswer::kAutoRename; break;
    default: return E_FAIL;
  }
  return ProgressDialog->Sync.CheckStop();
}


// === IFolderArchiveExtractCallback::PrepareOperation ========================
// Mirrors CExtractCallbackImp::PrepareOperation : pick the status string and push
// status+path to Sync via Set_Status2.
Z7_COM7F_IMF(QtExtractCallback::PrepareOperation(const wchar_t *name, Int32 isFolder, Int32 askExtractMode, const UInt64 * /* position */))
{
  _isFolder = IntToBool(isFolder);
  _currentFilePath = name;

  const UString *msg = &_lang_Empty;
  switch (askExtractMode)
  {
    case NArchive::NExtract::NAskMode::kExtract: msg = &_lang_Extracting; break;
    case NArchive::NExtract::NAskMode::kTest:    msg = &_lang_Testing; break;
    case NArchive::NExtract::NAskMode::kSkip:    msg = &_lang_Skipping; break;
    case NArchive::NExtract::NAskMode::kReadExternal: msg = &_lang_Reading; break;
  }

  return ProgressDialog->Sync.Set_Status2(*msg, name, IntToBool(isFolder));
}


// === IFolderArchiveExtractCallback::MessageError ============================
Z7_COM7F_IMF(QtExtractCallback::MessageError(const wchar_t *s))
{
  AddError_Message(s);
  return S_OK;
}


// === IFolderOperationsExtractCallback (B.5c) ================================
// CAgentFolder::CopyTo (ArchiveFolder.cpp:34) requires the callback to be typed
// as IFolderOperationsExtractCallback*; the archive Extract path it dispatches
// drives the IFolderArchiveExtractCallback methods above, so these 4 are not hit
// on archive extract-to-temp. They mirror CExtractCallbackImp (ExtractCallback.
// cpp) for completeness / the FS-source path.
Z7_COM7F_IMF(QtExtractCallback::ShowMessage(const wchar_t *message))
{
  AddError_Message(message);
  return S_OK;
}

Z7_COM7F_IMF(QtExtractCallback::SetCurrentFilePath(const wchar_t *filePath))
{
  return SetCurrentFilePath2(filePath);
}

Z7_COM7F_IMF(QtExtractCallback::SetNumFiles(UInt64 /* numFiles */))
{
  return S_OK;
}

// Mirror of CExtractCallbackImp::AskWrite (ExtractCallback.cpp:710): resolve the
// destination, applying the OverwriteMode state machine (kSkip/kAsk/kOverwrite/
// kRename) and routing the ask to the GUI-thread prompt via AskOverwrite.
Z7_COM7F_IMF(QtExtractCallback::AskWrite(
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
  NWindows::NFile::NFind::CFileInfo destFileInfo;

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
        Int32 overwriteResult;
        UString destPathSpec = destPath;
        const int slashPos = destPathSpec.ReverseFind_PathSepar();
        destPathSpec.DeleteFrom((unsigned)(slashPos + 1));
        destPathSpec += fs2us(destFileInfo.Name);

        // CFileInfo::MTime is a CFiTime (timespec) on Linux; convert to FILETIME
        // for AskOverwrite (same as QtFsOperationCallback::AskWrite).
        FILETIME existMTime_ft;
        FiTime_To_FILETIME(destFileInfo.MTime, existMTime_ft);

        RINOK(AskOverwrite(
            destPathSpec, &existMTime_ft, &destFileInfo.Size,
            srcPath, srcTime, srcSize, &overwriteResult))

        switch (overwriteResult)
        {
          case NOverwriteAnswer::kCancel:   return E_ABORT;
          case NOverwriteAnswer::kNo:       return S_OK;
          case NOverwriteAnswer::kNoToAll:  OverwriteMode = NExtract::NOverwriteMode::kSkip; return S_OK;
          case NOverwriteAnswer::kYes:      break;
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
      if (NWindows::NFile::NFind::DoesFileExist_Raw(destPathSys))
      if (!NWindows::NFile::NDir::DeleteFileAlways(destPathSys))
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


// === IFolderExtractToStreamCallback (G.2d : in-archive CRC/checksum) =========
// Faithful, trimmed mirror of CExtractCallbackImp's stream-mode hash methods
// (ExtractCallback.cpp UseExtractToStream/GetStream7/PrepareOperation7/
// SetOperationResult8). The VirtFileSystem branch and the _hashCalc (test-stat)
// branch are dropped — the FM CRC path only ever sets _hashStream (StreamMode +
// SetHashMethods), never a VirtFileSystem or a bare _hashCalc.
//
// Mechanism: the engine's CArchiveExtractCallback QIs this object to
// IFolderExtractToStreamCallback; if UseExtractToStream() returns true it calls
// GetStream7 per item, we hand back the _hashStream (whose ISequentialOutStream
// writes feed the CHashBundle's running digest), and on SetOperationResult8 we
// Final() that item into the bundle. No file is written to disk.

// mirror ExtractCallback.cpp's file-scoped GetItemBoolProp helper.
static HRESULT Qt_GetItemBoolProp(IGetProp *getProp, PROPID propID, bool &result)
{
  NCOM::CPropVariant prop;
  result = false;
  RINOK(getProp->GetProp(propID, &prop))
  if (prop.vt == VT_BOOL)
    result = VARIANT_BOOLToBool(prop.boolVal);
  else if (prop.vt != VT_EMPTY)
    return E_FAIL;
  return S_OK;
}

Z7_COM7F_IMF(QtExtractCallback::UseExtractToStream(Int32 *res))
{
  *res = BoolToInt(StreamMode);
  return S_OK;
}

Z7_COM7F_IMF(QtExtractCallback::GetStream7(const wchar_t *name,
    Int32 isDir,
    ISequentialOutStream **outStream, Int32 askExtractMode,
    IGetProp *getProp))
{
  COM_TRY_BEGIN
  *outStream = NULL;
  _hashStream_WasUsed = false;
  _needUpdateStat = false;
  _isFolder = IntToBool(isDir);
  _curSize = 0;

  if (_hashStream)
    _hashStream->ReleaseStream();

  _filePath = name;

  {
    NCOM::CPropVariant prop;
    RINOK(getProp->GetProp(kpidSize, &prop))
    UInt64 size = 0;
    if (ConvertPropVariantToUInt64(prop, size))
      _curSize = size;
  }

  Qt_GetItemBoolProp(getProp, kpidIsAltStream, _isAltStream);
  if (!ProcessAltStreams && _isAltStream)
    return S_OK;

  if (isDir) // dir items are not extracted in this code
    return S_OK;

  if (askExtractMode != NArchive::NExtract::NAskMode::kExtract &&
      askExtractMode != NArchive::NExtract::NAskMode::kTest)
    return S_OK;

  _needUpdateStat = true;

  // CRC path: no VirtFileSystem, so the underlying stream is NULL — the hash
  // stream simply discards the written bytes after digesting them.
  CMyComPtr<ISequentialOutStream> outStreamLoc;

  if (_hashStream)
  {
    _hashStream->SetStream(outStreamLoc);
    outStreamLoc = _hashStream;
    _hashStream->Init(true);
    _hashStream_WasUsed = true;
  }

  if (outStreamLoc)
    *outStream = outStreamLoc.Detach();
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(QtExtractCallback::PrepareOperation7(Int32 askExtractMode))
{
  COM_TRY_BEGIN
  _needUpdateStat = (
         askExtractMode == NArchive::NExtract::NAskMode::kExtract
      || askExtractMode == NArchive::NExtract::NAskMode::kTest
      || askExtractMode == NArchive::NExtract::NAskMode::kReadExternal
      );
  return SetCurrentFilePath2(_filePath);
  COM_TRY_END
}

Z7_COM7F_IMF(QtExtractCallback::SetOperationResult8(Int32 opRes, Int32 encrypted, UInt64 /* size */))
{
  COM_TRY_BEGIN
  if (_hashStream && _hashStream_WasUsed)
  {
    _hashStream->_hash->Final(_isFolder, _isAltStream, _filePath);
    _curSize = _hashStream->GetSize();
    _hashStream->ReleaseStream();
    _hashStream_WasUsed = false;
  }
  return SetOperationResult(opRes, encrypted);
  COM_TRY_END
}


// === SetExtractErrorMessage : ported from ExtractCallback.cpp =================
// (G.1i) Faithful mirror of FileManager/ExtractCallback.cpp's SetExtractErrorMessage:
// pick the base message langID, build via FmLang(id, <inline English == .rc text>),
// and for an encrypted-but-not-WrongPassword result append " : " + the
// IDS_EXTRACT_MSG_WRONG_PSW_GUESS suffix, then " : " + fileName. Each branch now
// routes through FmLang so a loaded Lang/*.txt translates it; the inline literal is
// the established port-English fallback.
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


// === IFolderArchiveExtractCallback::SetOperationResult ======================
// Mirrors CExtractCallbackImp::SetOperationResult : on error, build a message and
// accumulate; always advance the file counter into Sync.
Z7_COM7F_IMF(QtExtractCallback::SetOperationResult(Int32 opRes, Int32 encrypted))
{
  switch (opRes)
  {
    case NArchive::NExtract::NOperationResult::kOK:
      break;
    default:
    {
      UString s;
      Qt_SetExtractErrorMessage(opRes, encrypted, _currentFilePath, s);
      AddError_Message_ShowArcPath(s);
    }
  }

  _currentFilePath.Empty();
  if (_isFolder)
    NumFolders++;
  else
    NumFiles++;
  ProgressDialog->Sync.Set_NumFilesCur(NumFiles);

  return S_OK;
}


// === IFolderArchiveExtractCallback2::ReportExtractResult ====================
Z7_COM7F_IMF(QtExtractCallback::ReportExtractResult(Int32 opRes, Int32 encrypted, const wchar_t *name))
{
  if (opRes != NArchive::NExtract::NOperationResult::kOK)
  {
    UString s;
    Qt_SetExtractErrorMessage(opRes, encrypted, name, s);
    AddError_Message_ShowArcPath(s);
  }
  return S_OK;
}


// === IExtractCallbackUI ====================================================
HRESULT QtExtractCallback::BeforeOpen(const wchar_t *name, bool /* testMode */)
{
  _currentArchivePath = name;
  _needWriteArchivePath = true;
  RINOK(ProgressDialog->Sync.CheckStop())
  ProgressDialog->Sync.Set_TitleFileName(name);
  return S_OK;
}

HRESULT QtExtractCallback::SetCurrentFilePath2(const wchar_t *path)
{
  _currentFilePath = path;
  ProgressDialog->Sync.Set_FilePath(path);
  return S_OK;
}

HRESULT QtExtractCallback::OpenResult(
    const CCodecs * /* codecs */, const CArchiveLink &arcLink,
    const wchar_t *name, HRESULT result)
{
  _currentArchivePath = name;
  _needWriteArchivePath = true;

  // Trimmed mirror of OpenResult_GUI -> ErrorInfo_Print (ExtractCallback.cpp:512).
  // G.3c : surface the DECODED error flags, the ErrorMessage, the DECODED warning
  // flags AND the WarningMessage — not just ErrorMessage. The previous code gated
  // the whole level on !er.ErrorMessage.IsEmpty(), so a level with warning FLAGS
  // (e.g. a "There are some data after the end..." tail) or a WarningMessage but
  // no ErrorMessage string surfaced NOTHING; ErrorInfo_Print emits each part
  // independently, which this now mirrors.
  FOR_VECTOR (level, arcLink.Arcs)
  {
    const CArc &arc = arcLink.Arcs[level];
    const CArcErrorInfo &er = arc.ErrorInfo;
    if (!er.IsThereErrorOrWarning())
      continue;

    const UInt32 errorFlags = er.GetErrorFlags();
    if (errorFlags != 0)
    {
      NumArchiveErrors++;
      AddError_Message(Qt_GetOpenArcErrorMessage(errorFlags));
      _needWriteArchivePath = false;
    }
    if (!er.ErrorMessage.IsEmpty())
    {
      NumArchiveErrors++;
      AddError_Message(er.ErrorMessage);
      _needWriteArchivePath = false;
    }

    const UInt32 warningFlags = er.GetWarningFlags();
    if (warningFlags != 0)
    {
      // ErrorInfo_Print prefixes the decoded warning flags with the "Warnings"
      // property name + colon (ExtractCallback.cpp:524-528).
      UString w("Warnings:");
      w.Add_LF();
      w += Qt_GetOpenArcErrorMessage(warningFlags);
      AddError_Message(w);
      _needWriteArchivePath = false;
    }
    if (!er.WarningMessage.IsEmpty())
    {
      // ErrorInfo_Print prefixes the message with "Warning: " (ExtractCallback.cpp:531-536).
      UString w("Warning: ");
      w += er.WarningMessage;
      AddError_Message(w);
      _needWriteArchivePath = false;
    }
  }

  if (result != S_OK)
  {
    NumArchiveErrors++;
    UString s = name;
    s.Add_LF();
    if (result == S_FALSE)
      s += "Cannot open the file as archive";
    else
      s += NError::MyFormatMessage(result);
    AddError_Message(s);
    _needWriteArchivePath = false;
  }

  return S_OK;
}

HRESULT QtExtractCallback::ThereAreNoFiles()
{
  return S_OK;
}

HRESULT QtExtractCallback::ExtractResult(HRESULT result)
{
  ProgressDialog->Sync.Set_FilePath(L"");

  if (result == S_OK)
    return result;
  NumArchiveErrors++;
  if (result == E_ABORT
      || result == HRESULT_FROM_WIN32(ERROR_DISK_FULL))
    return result;

  Add_ArchiveName_Error();
  if (!_currentFilePath.IsEmpty())
    MessageError(NError::MyFormatMessage(result), us2fs(_currentFilePath));
  else
    AddError_Message(NError::MyFormatMessage(result));
  return S_OK;
}


// === IArchiveRequestMemoryUseCallback (G.8a) =================================
// Faithful mirror of CExtractCallbackImp::RequestMemoryUse (ExtractCallback.cpp:
// 1012-1117). The decompression-memory gate: when an archive needs more unpacking
// memory than the allowed limit, the engine calls this before extracting. We read
// the saved GB limit (NExtractQt::Read_LimitGB, the Qt analogue of NExtract::
// Read_LimitGB), compare required vs allowed, and — when exceeded and interactive
// — show a Qt MemDialog (marshalled to the GUI thread). Headless / no-prompt =>
// keep the engine default Allow, exactly like the original's g_DisableUserQuestions
// branch (which never shows CMemDialog and leaves the default answer).

// Mirror of CMemDialog::AddInfoMessage_To_String (MemDialog.cpp:58-71), used to
// build the "ERROR: ..." accumulated message (RequestMemoryUse's k_NoErrorMessage
// branch). ramSize_GB pointer null => RAM line omitted (the is_Allowed case).
static void Qt_Mem_AddInfoMessage(UString &s, UInt32 required_GB, UInt32 limit_GB,
    const UInt32 *ramSize_GB, const UString &filePath)
{
  // IDS_MEM_REQUIRES_BIG_MEM + "\n    <N> GB : <label>" lines (AddSize_GB).
  {
    const QByteArray u8 = FmLang(IDS_MEM_REQUIRES_BIG_MEM,
        QStringLiteral("The operation requires big amount of memory (RAM).")).toUtf8();
    s += GetUnicodeString(AString(u8.constData()));
  }
  struct { UInt32 gb; unsigned id; const char *eng; } lines[] = {
    { required_GB, IDS_MEM_REQUIRED_MEM_SIZE, "required memory usage size" },
    { limit_GB,    IDS_MEM_CURRENT_MEM_LIMIT, "allowed memory usage limit" },
  };
  for (unsigned i = 0; i < 2; i++)
  {
    s.Add_LF();
    s += "    ";
    s.Add_UInt32(lines[i].gb);
    s += " GB : ";
    const QByteArray u8 =
        FmLang(lines[i].id, QString::fromUtf8(lines[i].eng)).toUtf8();
    s += GetUnicodeString(AString(u8.constData()));
  }
  if (ramSize_GB)
  {
    s.Add_LF();
    s += "    ";
    s.Add_UInt32(*ramSize_GB);
    s += " GB : ";
    const QByteArray u8 = FmLang(IDS_MEM_RAM_SIZE, QStringLiteral("RAM size")).toUtf8();
    s += GetUnicodeString(AString(u8.constData()));
  }
  if (!filePath.IsEmpty())
  {
    s.Add_LF();
    s += "File: ";
    s += filePath;
  }
}

Z7_COM7F_IMF(QtExtractCallback::RequestMemoryUse(
    UInt32 flags, UInt32 indexType, UInt32 /* index */, const wchar_t *path,
    UInt64 requiredSize, UInt64 *allowedSize, UInt32 *answerFlags))
{
  UInt32 limit_GB = (UInt32)((*allowedSize + ((1u << 30) - 1)) >> 30);

  if ((flags & NRequestMemoryUseFlags::k_IsReport) == 0)
  {
    UInt64 limit_bytes = *allowedSize;
    // G.9a : the configured GB limit (NExtractQt::Read_LimitGB; (UInt32)-1 == none).
    const UInt32 limit_GB_Registry = NExtractQt::Read_LimitGB();
    if (limit_GB_Registry != 0 && limit_GB_Registry != (UInt32)(Int32)-1)
    {
      const UInt64 limit_bytes_Registry = (UInt64)limit_GB_Registry << 30;
      if ((flags & NRequestMemoryUseFlags::k_AllowedSize_WasForced) == 0
          || limit_bytes < limit_bytes_Registry)
      {
        limit_bytes = limit_bytes_Registry;
        limit_GB = limit_GB_Registry;
      }
    }
    *allowedSize = limit_bytes;
    if (requiredSize <= limit_bytes)
    {
      *answerFlags = NRequestMemoryAnswerFlags::k_Allow;
      return S_OK;
    }
    // default answer can be k_Allow if the limit was not forced; change it to
    // non-allowed here because the user can still raise the limit in the GUI.
    *answerFlags = NRequestMemoryAnswerFlags::k_Limit_Exceeded;
    if (flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected)
      *answerFlags |= NRequestMemoryAnswerFlags::k_SkipArc;
  }

  const UInt32 required_GB = (UInt32)((requiredSize + ((1u << 30) - 1)) >> 30);

  // Mirror CMemDialog field setup (ExtractCallback.cpp:1049-1056).
  UString arcPath;
  if (MultiArcMode)
    arcPath = _currentArchivePath;
  UString filePath;
  if (path)
    filePath = path;

  // The interactive branch: g_DisableUserQuestions => ProgressDialog->Disable-
  // UserQuestions here (the Qt mirror, set by the worker thread before the run).
  // We additionally require a usable MemPrompt: the original always has a stack
  // CMemDialog to Create(), so the interactive block presupposes a prompt. When
  // there is none (the CRC stream-mode path leaves MemPrompt null), we behave like
  // g_DisableUserQuestions — skip the block, keep the default answer, emit the
  // report — rather than silently forcing Allow.
  const bool disableUserQuestions =
      (ProgressDialog && ProgressDialog->DisableUserQuestions);

  if (!disableUserQuestions && MemPrompt
      && (flags & NRequestMemoryUseFlags::k_IsReport) == 0)
  {
    if (_remember)
    {
      // Already answered once this run with "remember" — reuse that skip state
      // (ExtractCallback.cpp:1061-1062). No new dialog.
    }
    else
    {
      const bool showRemember =
          (MultiArcMode
            || indexType != NArchive::NEventIndexType::kNoIndex
            || path != NULL);

      // Marshal the dialog to the GUI thread (BlockingQueuedConnection): the worker
      // blocks here exactly like dialog.Create(*ProgressDialog) (ExtractCallback.cpp
      // :1069-1070). The result fields come back in the out-parameters.
      int outLimitGB = (int)limit_GB;
      bool outNeedSave = false, outRemember = false, outSkipArc = false, outAccepted = false;
      QMetaObject::invokeMethod(MemPrompt, "Ask", Qt::BlockingQueuedConnection,
          Q_ARG(int, (int)required_GB),
          Q_ARG(int, (int)limit_GB),
          Q_ARG(bool, TestMode),
          Q_ARG(QString, WcsToQString(arcPath.Ptr())),
          Q_ARG(QString, WcsToQString(filePath.Ptr())),
          Q_ARG(bool, showRemember),
          Q_ARG(int *, &outLimitGB),
          Q_ARG(bool *, &outNeedSave),
          Q_ARG(bool *, &outRemember),
          Q_ARG(bool *, &outSkipArc),
          Q_ARG(bool *, &outAccepted));

      if (!outAccepted)
      {
        // IDCANCEL: k_Stop + E_ABORT (ExtractCallback.cpp:1071-1074).
        *answerFlags = NRequestMemoryAnswerFlags::k_Stop;
        return E_ABORT;
      }
      limit_GB = (UInt32)outLimitGB;
      if (outNeedSave)
        NExtractQt::Save_LimitGB(limit_GB);
      if (outRemember)
      {
        _remember = true;
        _skipArc = outSkipArc;
      }
      else
        _skipArc = outSkipArc;
    }

    *allowedSize = (UInt64)limit_GB << 30;
    if (!_skipArc)
    {
      *answerFlags = NRequestMemoryAnswerFlags::k_Allow;
      return S_OK;
    }
    *answerFlags =
        NRequestMemoryAnswerFlags::k_SkipArc
      | NRequestMemoryAnswerFlags::k_Limit_Exceeded;
    flags |= NRequestMemoryUseFlags::k_Report_SkipArc;
  }

  if ((flags & NRequestMemoryUseFlags::k_NoErrorMessage) == 0)
  {
    UString s ("ERROR: ");
    Qt_Mem_AddInfoMessage(s, required_GB, limit_GB, NULL, filePath);
    s.Add_LF();
    if ((flags & NRequestMemoryUseFlags::k_SkipArc_IsExpected) ||
        (flags & NRequestMemoryUseFlags::k_Report_SkipArc))
    {
      const QByteArray u8 = FmLang(IDS_MSG_ARC_UNPACKING_WAS_SKIPPED,
          QStringLiteral("Archive extraction was skipped.")).toUtf8();
      s += GetUnicodeString(AString(u8.constData()));
    }
    AddError_Message_ShowArcPath(s);
  }

  return S_OK;
}


// === ICryptoGetTextPassword : mirror CExtractCallbackImp::CryptoGetTextPassword
// Cache PasswordIsDefined/Password like the originals; ask via the GUI thread on
// first need.
#ifndef Z7_NO_CRYPTO

HRESULT QtExtractCallback::SetPassword(const UString &password)
{
  PasswordIsDefined = true;
  Password = password;
  return S_OK;
}

Z7_COM7F_IMF(QtExtractCallback::CryptoGetTextPassword(BSTR *password))
{
  COM_TRY_BEGIN
  PasswordWasAsked = true;
  if (!PasswordIsDefined)
  {
    QString pw;
    bool accepted = false;

    // BlockingQueuedConnection: worker blocks until the GUI thread returns the
    // password (mirrors dialog.Create(*ProgressDialog) != IDOK -> E_ABORT).
    QMetaObject::invokeMethod(PasswordPrompt, "Ask", Qt::BlockingQueuedConnection,
        Q_ARG(QString *, &pw),
        Q_ARG(bool *, &accepted));

    if (!accepted)
      return E_ABORT;

    {
      // QString (UTF-16) -> UString. Go through UTF-8 to be wchar_t-width safe.
      const QByteArray utf8 = pw.toUtf8();
      Password = GetUnicodeString(AString(utf8.constData()));
    }
    PasswordIsDefined = true;
  }
  return StringToBstr(Password, password);
  COM_TRY_END
}

#endif
