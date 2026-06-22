// QtExtractCallback.h
//
// Qt/Linux extraction callback (milestone C.1b). This is the Qt-backed sibling of
//   FileManager/ExtractCallback.h : CExtractCallbackImp   (the GUI template)
//   Console/ExtractCallbackConsole.h : CExtractCallbackConsole (the HWND-free template)
// trimmed to exactly what Common/Extract.h : Extract() needs to drive a single
// "extract everything to a directory" operation.
//
// Extract() casts ONE object to its three callback parameters
//   (openCallback, extractCallback, faeCallback)
// so this class implements all three roles, exactly like CExtractCallbackImp:
//   * IFolderArchiveExtractCallback (COM, includes IProgress) + ...2 + ICryptoGetTextPassword
//   * IExtractCallbackUI (non-COM)
//   * IOpenCallbackUI    (non-COM)
//
// Routing (mirrors CExtractCallbackImp, which routes everything to
// ProgressDialog->Sync):
//   SetTotal/SetCompleted          -> Sync.Set_NumBytes{Total,Cur}
//   PrepareOperation (status/path) -> Sync.Set_Status2
//   SetCurrentFilePath2            -> Sync.Set_FilePath
//   SetOperationResult/MessageError-> Sync.AddError_Message*
//   the stop check                 -> Sync.CheckStop()  (returns E_ABORT on cancel)
//
// The two USER QUESTIONS that the console template answers via stdin and the GUI
// template answers via Win32 dialogs are answered here by marshalling to the GUI
// thread (QtPasswordPrompt / QtOverwritePrompt, BlockingQueuedConnection).
//
// COM lifetime: CMyUnknownImp -> Release()==delete this. The object is owned by a
// CMyComPtr; it is NEVER put into a Qt parent-ownership tree (IUnknown has no
// virtual dtor). Z7_COM_USE_ATOMIC (defined globally) makes the refcount safe to
// touch from both threads.

#ifndef ZIP7_INC_QT_EXTRACT_CALLBACK_H
#define ZIP7_INC_QT_EXTRACT_CALLBACK_H

#include "../../../Common/MyCom.h"            // CMyComPtr / CMyComPtr2 (the _hashStream holder)
#include "../../../Common/MyString.h"

#include "../../IPassword.h"

#include "../Common/ArchiveExtractCallback.h" // IFolderArchiveExtractCallback*, ExtractMode, COutStreamWithHash
#include "../Common/ArchiveOpenCallback.h"    // IOpenCallbackUI
#include "../Common/HashCalc.h"               // G.2d : IHashCalc (the CHashBundle face)
#include "../Common/IFileExtractCallback.h"   // IExtractCallbackUI, NOverwriteAnswer, IFolderExtractToStreamCallback
#include "../FileManager/IFolder.h"           // B.5c : IFolderOperationsExtractCallback + IID

class QtProgressDialog;
class QtPasswordPrompt;
class QtOverwritePrompt;
class QtMemDialog;       // G.8a : memory-limit request prompt

class QtExtractCallback Z7_final:
  public IFolderArchiveExtractCallback,
  public IExtractCallbackUI, // NON-COM interface since 23.00
  public IOpenCallbackUI,    // NON-COM interface
  public IFolderArchiveExtractCallback2,
  // B.5c : CAgentFolder::CopyTo (ArchiveFolder.cpp:34) takes the callback typed
  // as IFolderOperationsExtractCallback* and QIs it to IFolderArchiveExtract-
  // Callback. So an archive extract-to-temp (drag-OUT) callback must implement
  // BOTH, exactly like the original CExtractCallbackImp (ExtractCallback.h:182,
  // :191). The archive Extract path drives only the IFolderArchiveExtract-
  // Callback methods; the 4 IFolderOperationsExtractCallback methods below
  // mirror CExtractCallbackImp for the FS-source path (not hit on archive
  // extract, but faithful).
  public IFolderOperationsExtractCallback,
  // G.2d : in-archive CRC/checksum. CAgentFolder::CopyTo runs the engine Extract,
  // whose CArchiveExtractCallback QIs the callback to IFolderExtractToStreamCallback
  // and (when UseExtractToStream() returns true) drives GetStream7 in stream mode —
  // exactly the mechanism CExtractCallbackImp uses for "calc CRC of archived
  // streams without writing to disk" (PanelCrc.cpp's !Is_IO_FS_Folder branch ->
  // CPanel::CopyTo with streamMode+hashMethods). These 4 methods + the _hashStream
  // mirror CExtractCallbackImp (ExtractCallback.cpp UseExtractToStream/GetStream7/
  // PrepareOperation7/SetOperationResult8). Additive: the existing drag-OUT extract
  // leaves StreamMode=false, so GetStream7 wraps the real out-stream as before.
  public IFolderExtractToStreamCallback,
  // G.8a : the decompression memory-limit gate. The engine's CArchiveExtract-
  // Callback QIs the callback to IArchiveRequestMemoryUseCallback and calls
  // RequestMemoryUse before unpacking an archive whose required memory exceeds the
  // allowed limit — exactly the mechanism CExtractCallbackImp implements
  // (ExtractCallback.h:194/:207/:224, ExtractCallback.cpp:1012). Our RequestMemory-
  // Use mirrors that body: read the saved GB limit, compare, and (if exceeded and
  // interactive) show a Qt MemDialog; headless => default Allow.
  public IArchiveRequestMemoryUseCallback,
 #ifndef Z7_NO_CRYPTO
  public ICryptoGetTextPassword,
 #endif
  public CMyUnknownImp
{
  // QI / AddRef / Release : identical pattern to CExtractCallbackImp, trimmed to
  // the interfaces extraction-to-dir needs. (The non-COM IExtractCallbackUI /
  // IOpenCallbackUI are reached by static_cast in ExtractGUI, not by QI.)
  Z7_COM_QI_BEGIN2(IFolderArchiveExtractCallback)
  Z7_COM_QI_ENTRY(IFolderArchiveExtractCallback2)
  Z7_COM_QI_ENTRY(IFolderOperationsExtractCallback) // B.5c
  Z7_COM_QI_ENTRY(IFolderExtractToStreamCallback)   // G.2d : in-archive hash
  Z7_COM_QI_ENTRY(IArchiveRequestMemoryUseCallback) // G.8a : memory-limit gate
 #ifndef Z7_NO_CRYPTO
  Z7_COM_QI_ENTRY(ICryptoGetTextPassword)
 #endif
  Z7_COM_QI_END
  Z7_COM_ADDREF_RELEASE

  Z7_IFACE_IMP(IExtractCallbackUI)
  Z7_IFACE_IMP(IOpenCallbackUI)
  Z7_IFACE_COM7_IMP(IProgress)
  Z7_IFACE_COM7_IMP(IFolderArchiveExtractCallback)
  Z7_IFACE_COM7_IMP(IFolderArchiveExtractCallback2)
  Z7_IFACE_COM7_IMP(IFolderOperationsExtractCallback) // B.5c
  Z7_IFACE_COM7_IMP(IFolderExtractToStreamCallback)   // G.2d : in-archive hash
  Z7_IFACE_COM7_IMP(IArchiveRequestMemoryUseCallback) // G.8a : memory-limit gate
 #ifndef Z7_NO_CRYPTO
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
 #endif

  bool _needWriteArchivePath;
  bool _isFolder;
  bool _totalFiles_Defined;
  bool _totalBytes_Defined;

  // G.2d : per-item stream-mode hash state (mirror CExtractCallbackImp's private
  // _isAltStream / _hashStream_WasUsed / _needUpdateStat / _curSize / _filePath).
  bool _isAltStream;
  bool _hashStream_WasUsed;
  bool _needUpdateStat;
  UInt64 _curSize;
  UString _filePath; // virtual path sent via IFolderExtractToStreamCallback::GetStream7

  // G.8a : memory-limit "remember the answer for the rest of this run" state —
  // mirror of CExtractCallbackImp::_remember / _skipArc (ExtractCallback.h private
  // members, set in RequestMemoryUse when the user ticks "Repeat selected action").
  bool _remember;
  bool _skipArc;

public:
  // G.2d : set to true to make UseExtractToStream() return true, so the engine
  // drives GetStream7 (stream-mode, no on-disk file). Left false for ordinary
  // extract/drag-OUT. Mirror of CExtractCallbackImp::StreamMode.
  bool StreamMode;
  bool ProcessAltStreams; // mirror CExtractCallbackImp (skip alt streams when false)

  bool MultiArcMode;
  bool ThereAreMessageErrors;
  bool Src_Is_IO_FS_Folder;

#ifndef Z7_NO_CRYPTO
  bool PasswordIsDefined;
  bool PasswordWasAsked;
#endif

  // mirror CExtractCallbackImp's overwrite-policy state
  bool YesToAll;
  bool TestMode;

  UInt32 NumArchiveErrors;
  NExtract::NOverwriteMode::EEnum OverwriteMode;

private:
  UString _currentArchivePath;
  UString _currentFilePath;

  UInt64 NumFolders;
  UInt64 NumFiles;

public:
  // Like CExtractCallbackImp::ProgressDialog : the source of the shared Sync and
  // the modal parent for the prompt dialogs. Set by the ExtractGUI wiring.
  QtProgressDialog *ProgressDialog;

  // GUI-thread prompt objects (owned/affined to the GUI thread). Set by wiring.
  QtPasswordPrompt  *PasswordPrompt;
  QtOverwritePrompt *OverwritePrompt;
  // G.8a : the memory-limit request prompt (CMemDialog analogue). May be null in
  // paths that never hit a big-dictionary archive (e.g. CRC stream-mode); the
  // RequestMemoryUse default-Allow path tolerates a null prompt.
  QtMemDialog       *MemPrompt;

#ifndef Z7_NO_CRYPTO
  UString Password;
#endif

  // G.2d : the hash-collection stream (mirror CExtractCallbackImp::_hashStream).
  // COutStreamWithHash::_hash points at the caller's CHashBundle (IHashCalc). When
  // set (via SetHashMethods), GetStream7 wraps the per-item output in it and
  // SetOperationResult8 calls _hash->Final() per file. Empty => no hashing.
  CMyComPtr2<ISequentialOutStream, COutStreamWithHash> _hashStream;

  // Mirror of CExtractCallbackImp::SetHashMethods : attach the CHashBundle the
  // engine will feed. (No-op on null, like the original.)
  void SetHashMethods(IHashCalc *hash)
  {
    if (!hash)
      return;
    _hashStream.Create_if_Empty();
    _hashStream->_hash = hash;
  }

  // status strings (the GUI template loads these from the lang table; we use the
  // English defaults, mirroring CExtractCallbackImp::Init's _lang_* assignments).
  UString _lang_Extracting;
  UString _lang_Testing;
  UString _lang_Skipping;
  UString _lang_Reading;
  UString _lang_Empty;

  QtExtractCallback():
      _needWriteArchivePath(true)
    , _isFolder(false)
    , _totalFiles_Defined(false)
    , _totalBytes_Defined(false)
    , _isAltStream(false)
    , _hashStream_WasUsed(false)
    , _needUpdateStat(false)
    , _curSize(0)
    , _remember(false)     // G.8a
    , _skipArc(false)      // G.8a
    , StreamMode(false)
    , ProcessAltStreams(true)
    , MultiArcMode(false)
    , ThereAreMessageErrors(false)
    , Src_Is_IO_FS_Folder(false)
#ifndef Z7_NO_CRYPTO
    , PasswordIsDefined(false)
    , PasswordWasAsked(false)
#endif
    , YesToAll(false)
    , TestMode(false)
    , NumArchiveErrors(0)
    , OverwriteMode(NExtract::NOverwriteMode::kAsk)
    , NumFolders(0)
    , NumFiles(0)
    , ProgressDialog(nullptr)
    , PasswordPrompt(nullptr)
    , OverwritePrompt(nullptr)
    , MemPrompt(nullptr)   // G.8a
    {}

  ~QtExtractCallback() {}

  void Init();

  HRESULT SetCurrentFilePath2(const wchar_t *filePath);
  void AddError_Message(LPCWSTR message);
  void AddError_Message_ShowArcPath(LPCWSTR message);
  HRESULT MessageError(const wchar_t *message, const FString &path);
  void Add_ArchiveName_Error();

  bool IsOK() const { return NumArchiveErrors == 0 && !ThereAreMessageErrors; }
};

#endif
