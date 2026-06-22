// ProgressSync.h
//
// Verbatim extraction of CProgressSync from
//   CPP/7zip/UI/FileManager/ProgressDialog2.{h,cpp}
// (the HWND-free progress-state object). Logic unchanged.
//
// Only the include paths and the surrounding #include set differ from the
// original (the original ProgressDialog2.h pulls in Win32 Control/* and
// MyWindowsNew.h headers that CProgressSync itself does not use). The class
// declaration below, plus the CProgressMessageBoxPair / CProgressFinalMessage
// structs it depends on, are copied byte-for-byte from ProgressDialog2.h.
//
// This object is the thread-safe state shared between the worker thread and the
// GUI; reusing it unchanged is the whole point of milestone C.1a.

#ifndef ZIP7_INC_QT_PROGRESS_SYNC_H
#define ZIP7_INC_QT_PROGRESS_SYNC_H

#include "../../../Common/MyCom.h"
#include "../../../Common/MyString.h"

#include "../../../Windows/ErrorMsg.h"
#include "../../../Windows/Synchronization.h"

struct CProgressMessageBoxPair
{
  UString Title;
  UString Message;
};

struct CProgressFinalMessage
{
  CProgressMessageBoxPair ErrorMessage;
  CProgressMessageBoxPair OkMessage;

  bool ThereIsMessage() const { return !ErrorMessage.Message.IsEmpty() || !OkMessage.Message.IsEmpty(); }
};

class CProgressSync
{
  bool _stopped;
  bool _paused;
public:
  bool _filesProgressMode;
  bool _isDir;
  UInt64 _totalBytes;
  UInt64 _completedBytes;
  UInt64 _totalFiles;
  UInt64 _curFiles;
  UInt64 _inSize;
  UInt64 _outSize;

  UString _titleFileName;
  UString _status;
  UString _filePath;

  UStringVector Messages;
  CProgressFinalMessage FinalMessage;

  NWindows::NSynchronization::CCriticalSection _cs;

  CProgressSync();

  bool Get_Stopped()
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_cs);
    return _stopped;
  }
  void Set_Stopped(bool val)
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_cs);
    _stopped = val;
  }

  bool Get_Paused();
  void Set_Paused(bool val)
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_cs);
    _paused = val;
  }

  void Set_FilesProgressMode(bool filesProgressMode)
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_cs);
    _filesProgressMode = filesProgressMode;
  }

  HRESULT CheckStop();
  void Clear_Stop_Status();
  HRESULT ScanProgress(UInt64 numFiles, UInt64 totalSize, const FString &fileName, bool isDir = false);

  HRESULT Set_NumFilesTotal(UInt64 val);
  void Set_NumBytesTotal(UInt64 val);
  void Set_NumFilesCur(UInt64 val);
  HRESULT Set_NumBytesCur(const UInt64 *val);
  HRESULT Set_NumBytesCur(UInt64 val);
  void Set_Ratio(const UInt64 *inSize, const UInt64 *outSize);

  void Set_TitleFileName(const UString &fileName);
  void Set_Status(const UString &s);
  HRESULT Set_Status2(const UString &s, const wchar_t *path, bool isDir = false);
  void Set_FilePath(const wchar_t *path, bool isDir = false);

  void AddError_Message(const wchar_t *message);
  void AddError_Message_Name(const wchar_t *message, const wchar_t *name);
  // void AddError_Code_Name(DWORD systemError, const wchar_t *name);
  void AddError_Code_Name(HRESULT systemError, const wchar_t *name);

  bool ThereIsMessage() const { return !Messages.IsEmpty() || FinalMessage.ThereIsMessage(); }
};

#endif
