// ArchiveOpenHelper.cpp
// ----------------------------------------------------------------------------
// See ArchiveOpenHelper.h. This is the B.0/B.1 CAgent open sequence (verbatim
// shape from main_archive_browser.cpp), made reusable. It does NOT emit GUIDs
// (the single GUID-emitting TU per executable stays main_*_browser.cpp).
// ----------------------------------------------------------------------------

#include "../../Agent/StdAfx.h"

#include "../agent/AgentLinuxCompat.h"

#include "../../../../Common/MyString.h"
#include "../../../../Common/StringConvert.h"

#include "../../../../Windows/FileFind.h"     // G.3d : CFileInfo (sibling-volume stat)
#include "../../../../Windows/FileName.h"     // G.3d : NName::GetFullPath
#include "../../../../Windows/PropVariant.h"  // G.3d : CPropVariant (GetProperty)
#include "../../../../Windows/TimeUtils.h"    // G.3d : PropVariant_SetFrom_FiTime

#include "../../../Common/FileStreams.h"
#include "../../../IPassword.h"

#include "../../Agent/Agent.h"

#include "../ProgressSync.h"   // P.1 : CProgressSync (Set_Num*/CheckStop)

#include <QtCore/QThread>           // Encrypted-FM : pick the prompt connection by thread
#include <QtCore/QMetaObject>
#include <QtCore/QString>
#include <QtCore/Qt>                // Qt::DirectConnection / BlockingQueuedConnection
#include "../QtExtractPrompts.h"    // Encrypted-FM : QtPasswordPrompt (GUI-thread Ask)

#include "ArchiveOpenHelper.h"

using namespace NWindows;

// G.3d : declared in ArchiveExtractCallback.cpp (external linkage), forward-declared
// here exactly as COpenCallbackImp does — rejects unsafe sibling-volume names (abs
// paths, "..", drive-relative) before we ever touch the filesystem.
bool IsSafePath(const UString &path);

// --- minimal open callback : supplies the password headlessly ----------------
// Same shape as main_archive_browser.cpp's COpenCallbackBrowser (B.1).
//
// G.3d : ALSO implements IArchiveOpenVolumeCallback so the engine can resolve the
// sibling parts of a MULTI-VOLUME archive (e.g. archive.7z.001 + .002 + ...). The
// engine QIs this open callback for IArchiveOpenVolumeCallback (OpenArchive.cpp:2459)
// and, when it detects a volume set, asks GetProperty(kpidName) for the base name
// and GetStream(<volumeName>) for each sibling. We mirror COpenCallbackImp's
// GetProperty/GetStream (ArchiveOpenCallback.cpp:79,284): GetProperty answers from
// the archive file's own CFileInfo (or the sub-archive name in sub-archive mode);
// GetStream resolves <_folderPrefix>/<name>, opens it via CInFileStream, and returns
// S_FALSE when the sibling is absent (so a partial set fails cleanly).
//
// The file-path open variant Init2()'s _folderPrefix to the directory containing the
// archive (so siblings resolve); the stream variant has no on-disk prefix and stays
// in sub-archive mode (no volume resolution), exactly as COpenCallbackImp does when
// SetSubArchiveName is used instead of Init2.
namespace {
class COpenCallbackFm Z7_final:
  public IArchiveOpenCallback,
  public IArchiveOpenVolumeCallback,
  public ICryptoGetTextPassword,
  public CMyUnknownImp
{
  Z7_COM_QI_BEGIN2(IArchiveOpenCallback)
    Z7_COM_QI_ENTRY(IArchiveOpenVolumeCallback)
    Z7_COM_QI_ENTRY(ICryptoGetTextPassword)
  Z7_COM_QI_END
  Z7_COM_ADDREF_RELEASE

  Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
  Z7_IFACE_COM7_IMP(IArchiveOpenVolumeCallback)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
public:
  bool PasswordIsDefined;
  UString Password;
  // P.1 : optional progress/cancel sink. When set (the threaded open path), the
  // engine's open scan ticks Set_Num*Total/Cur and the callback returns the
  // Sync's CheckStop() HRESULT so a user Cancel propagates E_ABORT out of Open.
  CProgressSync *Sync;
  // Encrypted-FM : optional GUI-thread password prompt (CPasswordDialog analogue).
  // When non-null and the archive is header-encrypted, CryptoGetTextPassword asks
  // the user once and caches the answer (PasswordIsDefined/Password), exactly like
  // COpenCallbackImp. Null on the headless / no-prompt path (old behaviour).
  QtPasswordPrompt *Prompt;

  // G.3b : faithful mirror of CFfpOpen::Encrypted (FileFolderPluginOpen.cpp) /
  // COpenResult::Encrypted (PanelItemOpen.cpp:462,484). Set true the first time the
  // engine asks this callback for a password (CryptoGetTextPassword) — i.e. the
  // archive is header-encrypted. The open caller reads it so a FAILED open of a
  // header-encrypted archive can show IDS_CANT_OPEN_ENCRYPTED_ARCHIVE instead of
  // IDS_CANT_OPEN_ARCHIVE, exactly as CPanel::OpenAsArc_Msg branches on opRes.Encrypted.
  bool Encrypted;

  // G.3d : multi-volume wiring, mirroring COpenCallbackImp's members.
  // _folderPrefix is the directory (with trailing separator) that contains the
  // archive file; sibling volumes are resolved relative to it. _fileInfo holds the
  // stat of the OPENED archive file (GetProperty answers from it). _subArchiveMode
  // is set for the stream variant (no on-disk prefix), where GetStream returns
  // S_FALSE and GetProperty answers the virtual name (faithful to SetSubArchiveName).
  bool VolumeModeReady;       // true once Init2 succeeded (file-path open only)
  bool SubArchiveMode;        // true on the stream variant
  FString FolderPrefix;
  UString SubArchiveName;
  NFile::NFind::CFileInfo FileInfo;

  // Faithful mirror of COpenCallbackImp::Init2 (ArchiveOpenCallback.cpp:30): record
  // the folder prefix and stat the archive file itself so GetProperty(kpidName/Size/
  // times) can answer from it. On failure (file vanished) returns an HRESULT and the
  // volume path stays disabled — the engine then just opens the single given stream.
  HRESULT Init2(const FString &folderPrefix, const FString &fileName)
  {
    SubArchiveMode = false;
    FolderPrefix = folderPrefix;
    if (!FileInfo.Find_FollowLink(FolderPrefix + fileName))
      return GetLastError_noZero_HRESULT();
    VolumeModeReady = true;
    return S_OK;
  }

  // Faithful mirror of COpenCallbackImp::SetSubArchiveName: the stream variant has no
  // real on-disk prefix, so GetProperty answers this virtual name and GetStream is a
  // no-op (S_FALSE) — a nested archive is never a multi-volume set on disk.
  void SetSubArchiveNameLocal(const UString &name)
  {
    SubArchiveMode = true;
    SubArchiveName = name;
  }

  COpenCallbackFm():
      PasswordIsDefined(false), Sync(nullptr), Prompt(nullptr),
      Encrypted(false),
      VolumeModeReady(false), SubArchiveMode(false) {}
};

// SetTotal/SetCompleted : faithful mirror of how the FS callback drives Sync
// (QtFsOperations.cpp) — null-check each optional pointer, then surface the stop
// HRESULT. With no Sync (the synchronous fast-path) this stays the old no-op.
Z7_COM7F_IMF(COpenCallbackFm::SetTotal(const UInt64 *files, const UInt64 *bytes))
{
  if (Sync)
  {
    if (files) Sync->Set_NumFilesTotal(*files);
    // G.8c : mirror COpenArchiveCallback::Open_SetTotal (OpenCallback.cpp:30) —
    // switch the progress bar to FILES-count mode whenever the format reports a
    // file count (numFiles != NULL), so the bar advances by file count when a
    // format reports files but no byte total.
    Sync->Set_FilesProgressMode(files != NULL);
    if (bytes) Sync->Set_NumBytesTotal(*bytes);
    return Sync->CheckStop();   // S_OK / E_ABORT (paused-aware), cancel as early as SetTotal
  }
  return S_OK;
}
Z7_COM7F_IMF(COpenCallbackFm::SetCompleted(const UInt64 *files, const UInt64 *bytes))
{
  if (Sync)
  {
    if (files) Sync->Set_NumFilesCur(*files);
    // Set_NumBytesCur(const UInt64*) already CheckStop()s and returns E_ABORT on stop.
    return Sync->Set_NumBytesCur(bytes);
  }
  return S_OK;
}
Z7_COM7F_IMF(COpenCallbackFm::CryptoGetTextPassword(BSTR *password))
{
  // Faithful mirror of COpenCallbackImp::Open_CryptoGetTextPassword
  // (FileManager/OpenCallback.cpp:63): ask once on first need (header-encrypted
  // archive), then cache. The Qt prompt is the QtExtractCallback shape
  // (QtExtractCallback.cpp:548): pick DirectConnection when we're already on the
  // prompt's (GUI) thread — the synchronous fast-path — and BlockingQueued when
  // we're on the open worker thread (threaded path); a Cancel -> E_ABORT so Open
  // fails cleanly and the panel stays on its FS folder.
  //
  // G.3b : the engine only calls CryptoGetTextPassword when the archive needs a
  // password (header-encrypted) — so this call IS the "encrypted" signal, exactly
  // as CFfpOpen sets ffp.Encrypted when its open callback is asked for a password.
  // Record it BEFORE the prompt so a Cancel (E_ABORT) still leaves Encrypted=true,
  // and so a still-failed open after a (wrong) password reports the encrypted case.
  Encrypted = true;
  if (!PasswordIsDefined)
  {
    if (Prompt)
    {
      QString pw;
      bool accepted = false;

      const auto conn = (QThread::currentThread() == Prompt->thread())
          ? Qt::DirectConnection : Qt::BlockingQueuedConnection;
      QMetaObject::invokeMethod(Prompt, "Ask", conn,
          Q_ARG(QString *, &pw),
          Q_ARG(bool *, &accepted));

      if (!accepted)
        return E_ABORT;

      {
        // QString (UTF-16) -> UString. Go through UTF-8 to be wchar_t-width safe
        // (the exact conversion QtExtractCallback::CryptoGetTextPassword uses).
        const QByteArray utf8 = pw.toUtf8();
        Password = GetUnicodeString(AString(utf8.constData()));
      }
      PasswordIsDefined = true;
    }
    else
      return StringToBstr(UString(), password);
  }
  return StringToBstr(Password, password);
}

// G.3d : IArchiveOpenVolumeCallback::GetProperty — faithful mirror of
// COpenCallbackImp::GetProperty (ArchiveOpenCallback.cpp:79). In sub-archive mode
// (the stream variant) only kpidName is answered (the virtual name); otherwise the
// archive file's own CFileInfo supplies kpidName/IsDir/Size/Attrib/times. The
// engine uses these (notably kpidName) to derive the base name of the volume set.
Z7_COM7F_IMF(COpenCallbackFm::GetProperty(PROPID propID, PROPVARIANT *value))
{
  NCOM::CPropVariant prop;
  if (SubArchiveMode)
  {
    switch (propID)
    {
      case kpidName: prop = SubArchiveName; break;
      default: break;
    }
  }
  else if (VolumeModeReady)
  {
    switch (propID)
    {
      case kpidName:  prop = fs2us(FileInfo.Name); break;
      case kpidIsDir:  prop = FileInfo.IsDir(); break;
      case kpidSize:  prop = FileInfo.Size; break;
      case kpidAttrib:  prop = (UInt32)FileInfo.GetWinAttrib(); break;
      case kpidPosixAttrib:  prop = (UInt32)FileInfo.GetPosixAttrib(); break;
      case kpidCTime:  PropVariant_SetFrom_FiTime(prop, FileInfo.CTime); break;
      case kpidATime:  PropVariant_SetFrom_FiTime(prop, FileInfo.ATime); break;
      case kpidMTime:  PropVariant_SetFrom_FiTime(prop, FileInfo.MTime); break;
      default: break;
    }
  }
  prop.Detach(value);
  return S_OK;
}

// G.3d : IArchiveOpenVolumeCallback::GetStream — faithful (simplified) mirror of
// COpenCallbackImp::GetStream (ArchiveOpenCallback.cpp:284). Resolves the requested
// sibling-volume `name` against _folderPrefix, opens it via CInFileStream, and
// returns S_FALSE when it's unsafe/absent/a directory (a missing tail volume then
// fails the open cleanly). We open a plain CInFileStream per sibling rather than the
// engine's LRU-pooled CInFileStreamVol/CMultiStreams — that pool is purely a
// file-handle-budget optimisation for huge volume sets; the IInStream contract the
// engine consumes is identical. In sub-archive mode (stream variant) there is no
// on-disk prefix, so this is a no-op (S_FALSE), exactly as COpenCallbackImp does.
Z7_COM7F_IMF(COpenCallbackFm::GetStream(const wchar_t *name, IInStream **inStream))
{
  *inStream = NULL;

  if (SubArchiveMode || !VolumeModeReady)
    return S_FALSE;

  // Break-check on the same Sync the open scan uses, mirroring COpenCallbackImp's
  // Callback->Open_CheckBreak() so a user Cancel during volume discovery aborts.
  if (Sync)
    RINOK(Sync->CheckStop())

  UString name2 = name;

  // IsSafePath rejects absolute paths / ".." / drive-relative names (the engine
  // does the same WIN32 separator-normalisation and wildcard rejection there; on
  // Linux only IsSafePath applies — no '\\'/'*'/'?' handling, matching the engine's
  // #ifdef _WIN32 guards).
  if (!IsSafePath(name2))
    return S_FALSE;

  FString fullPath;
  if (!NFile::NName::GetFullPath(FolderPrefix, us2fs(name2), fullPath))
    return S_FALSE;

  NFile::NFind::CFileInfo fi;
  if (!fi.Find_FollowLink(fullPath))
    return S_FALSE;
  if (fi.IsDir())
    return S_FALSE;

  CInFileStream *inFile = new CInFileStream;
  CMyComPtr<IInStream> inStreamTemp = inFile;
  if (!inFile->Open(fullPath))
    return GetLastError_noZero_HRESULT();

  *inStream = inStreamTemp.Detach();
  return S_OK;
}
} // namespace

// Shared open body for BOTH the FILE and the STREAM variants. The ONLY difference
// between CAgent::Open from a path and from a sub-stream is which IInStream is
// passed (Agent.cpp:1609: a non-NULL inStream opens from the stream; the path arg
// is then only the format-detection filePath hint). Everything after the open —
// the callback wiring, BindToRootFolder, and the CanUpdate gate — is identical, so
// it lives here once.
// G.3d : `volumeDirPrefix`/`volumeFileName` drive the IArchiveOpenVolumeCallback
// wiring (Init2-style, mirroring FileFolderPluginOpen.cpp:325-330). When BOTH are
// non-null (the FILE open) the callback Init2's _folderPrefix to the archive's
// directory so sibling volumes resolve; when null (the STREAM open) the callback is
// put in sub-archive mode with `filePath` as the virtual name (no on-disk volumes).
static HRESULT OpenStreamAsFolder(IInStream *inStream, const UString &filePath,
    const UString &password, bool passwordDefined,
    CMyComPtr<IFolderFolder> &rootFolder,
    CMyComPtr<IUnknown> &agentHolder,
    bool *isUpdatable,
    CProgressSync *openSync,
    QtPasswordPrompt *passwordPrompt,
    const FString *volumeDirPrefix,
    const FString *volumeFileName,
    bool *outEncrypted)
{
  COpenCallbackFm *cbSpec = new COpenCallbackFm;
  CMyComPtr<IArchiveOpenCallback> openCallback = cbSpec;
  if (passwordDefined)
  {
    cbSpec->PasswordIsDefined = true;
    cbSpec->Password = password;
  }
  cbSpec->Sync = openSync;   // P.1 : null on the synchronous fast-path; set when threaded
  cbSpec->Prompt = passwordPrompt;   // Encrypted-FM : null = headless / no prompt wired

  // G.3d : enable multi-volume resolution for the file-path open, or sub-archive
  // mode (no volumes) for the stream open — faithful to COpenCallbackImp's
  // Init2 / SetSubArchiveName split (FileFolderPluginOpen.cpp:325). If Init2 fails
  // (archive file vanished between open and stat) we DON'T abort the open: leave the
  // callback in single-stream mode and let the engine open the one stream it has.
  if (volumeDirPrefix && volumeFileName)
    cbSpec->Init2(*volumeDirPrefix, *volumeFileName);
  else
    cbSpec->SetSubArchiveNameLocal(filePath);

  // Create the CAgent and open. arcFormat must be L"" (NOT NULL) - B.0 note.
  CAgent *agentSpec = new CAgent;
  CMyComPtr<IInFolderArchive> agent = agentSpec;
  CMyComBSTR archiveType;
  HRESULT res = agent->Open(inStream, filePath, L"", &archiveType, openCallback);
  // G.3b : surface whether the engine asked for a password (header-encrypted),
  // mirroring CPanel::OpenAsArc copying ffp.Encrypted into opRes.Encrypted REGARDLESS
  // of the open result — so the caller can branch CANT_OPEN vs CANT_OPEN_ENCRYPTED.
  if (outEncrypted)
    *outEncrypted = cbSpec->Encrypted;
  if (res != S_OK)
    return res; // not an archive (or open error): caller treats as "not archive"

  // Get the root IFolderFolder.
  CMyComPtr<IFolderFolder> root;
  res = agent->BindToRootFolder(&root);
  if (res != S_OK || !root)
    return (res == S_OK) ? E_FAIL : res;

  // B.5b : capture in-place updatability while we still hold the typed CAgent*.
  // Faithful to the FM gate (CAgent::CanUpdate / IsThere_ReadOnlyArc). A nested
  // stream archive (G.2b) is not writable in place, so this comes back false.
  if (isUpdatable)
    *isUpdatable = agentSpec->CanUpdate() && !agentSpec->IsThere_ReadOnlyArc();

  rootFolder = root;
  agentHolder = agent;       // keep the CAgent alive while the folder is in use
  return S_OK;
}

HRESULT OpenArchiveAsFolder(const UString &arcPath, const UString &password,
    bool passwordDefined,
    CMyComPtr<IFolderFolder> &rootFolder,
    CMyComPtr<IUnknown> &agentHolder,
    bool *isUpdatable,
    CProgressSync *openSync,
    QtPasswordPrompt *passwordPrompt,
    bool *outEncrypted)
{
  rootFolder.Release();
  agentHolder.Release();
  if (isUpdatable)
    *isUpdatable = false;
  if (outEncrypted)
    *outEncrypted = false;

  // Open the file as an IInStream (CInFileStream), B.0 pattern, then share the
  // open body with the stream variant.
  CInFileStream *fileSpec = new CInFileStream;
  CMyComPtr<IInStream> fileStream = fileSpec;
  if (!fileSpec->Open(us2fs(arcPath)))
    return E_FAIL;

  // G.3d : split arcPath into <dirPrefix>/<fileName> for the volume callback, exactly
  // as FileFolderPluginOpen.cpp:253-262 (ReverseFind on the path separator; dirPrefix
  // keeps its trailing separator; no separator => empty prefix, leaf = the whole name).
  // Sibling volumes (archive.7z.002, ...) are then resolved relative to dirPrefix.
  const FString fsPath = us2fs(arcPath);
  const int slashPos = fsPath.ReverseFind_PathSepar();
  FString dirPrefix;
  FString fileName;
  if (slashPos >= 0)
  {
    dirPrefix.SetFrom(fsPath, (unsigned)(slashPos + 1));
    fileName = fsPath.Ptr((unsigned)(slashPos + 1));
  }
  else
    fileName = fsPath;

  return OpenStreamAsFolder(fileStream, arcPath, password, passwordDefined,
      rootFolder, agentHolder, isUpdatable, openSync, passwordPrompt,
      &dirPrefix, &fileName, outEncrypted);
}

// G.2b : open a nested archive directly from its sub-stream (no temp file). The
// caller already GetStream()'d the sub-archive's IInStream out of the outer
// CAgentFolder; we just hand it to the shared open body. No CProgressSync here —
// a nested archive is opened synchronously on the GUI thread (the sub-stream read
// is from the already-resident outer archive, not the slow disk-scan a top-level
// open faces), exactly as CPanel::OpenItemInArchive opens the sub-stream inline.
HRESULT OpenArchiveStreamAsFolder(IInStream *inStream, const UString &virtualName,
    const UString &password, bool passwordDefined,
    CMyComPtr<IFolderFolder> &rootFolder,
    CMyComPtr<IUnknown> &agentHolder,
    bool *isUpdatable,
    QtPasswordPrompt *passwordPrompt,
    bool *outEncrypted)
{
  rootFolder.Release();
  agentHolder.Release();
  if (isUpdatable)
    *isUpdatable = false;
  if (outEncrypted)
    *outEncrypted = false;

  if (!inStream)
    return E_INVALIDARG;

  // Rewind the sub-stream to its start before the format scan — GetStream may hand
  // back a stream positioned anywhere, and CAgent::Open expects to read from 0.
  if (inStream->Seek(0, STREAM_SEEK_SET, NULL) != S_OK)
    return E_FAIL;

  // G.2b : a stream-backed (in-memory/view) archive is NOT writable in place — its
  // CAgent has no real on-disk _archiveFilePath to write back to (Open set it to the
  // virtual entry name). CAgent::CanUpdate() only checks the FORMAT and would still
  // say "true" for e.g. a nested 7z, so we pass nullptr to the shared body (skip its
  // CanUpdate gate) and force isUpdatable=false here. The FM then keeps refusing
  // in-place Delete/Add on a nested archive (G.2b is read/browse/extract only).
  // G.3d : no on-disk directory prefix for a nested sub-stream, so pass null volume
  // params — the callback goes into sub-archive mode (GetStream is a no-op / S_FALSE),
  // faithful to COpenCallbackImp::SetSubArchiveName (a nested archive is never a
  // multi-volume set on disk).
  const HRESULT res = OpenStreamAsFolder(inStream, virtualName, password,
      passwordDefined, rootFolder, agentHolder, /*isUpdatable*/ nullptr,
      /*openSync*/ nullptr, passwordPrompt,
      /*volumeDirPrefix*/ nullptr, /*volumeFileName*/ nullptr, outEncrypted);
  // isUpdatable already initialised to false above; leave it false on success too.
  return res;
}
