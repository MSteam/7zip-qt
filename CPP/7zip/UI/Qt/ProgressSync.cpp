// ProgressSync.cpp
//
// Verbatim extraction of CProgressSync's method implementations from
//   CPP/7zip/UI/FileManager/ProgressDialog2.cpp
// (the HWND-free progress-state object). Logic unchanged.
//
// The ONLY changes versus the original are:
//   * the #include set / paths (this TU does not include the Win32 GUI headers),
//   * a small ::Sleep() compatibility shim for non-Windows builds, because the
//     original CheckStop() calls the Win32 ::Sleep API that the portable engine
//     does not expose on Linux. The CheckStop() body itself is byte-for-byte
//     identical to the original; only the symbol it resolves to is provided here.
// No field names, method signatures, or arithmetic were altered.

#include "ProgressSync.h"

#include "../../../Common/StringConvert.h"
#include "../../../Windows/ErrorMsg.h"

#ifndef _WIN32
// The original ::Sleep(milliseconds) is a Win32 API. On Linux the portable
// 7-Zip engine has no equivalent in its public headers, so provide a local,
// behaviour-equivalent shim. This mirrors what the engine's own C layer does
// internally (nanosleep). CheckStop()'s logic below is unchanged.
#include <time.h>
static inline void Sleep(unsigned milliseconds)
{
  struct timespec ts;
  ts.tv_sec = (time_t)(milliseconds / 1000);
  ts.tv_nsec = (long)(milliseconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
}
#endif

using namespace NWindows;

#define UNDEFINED_VAL         ((UInt64)(Int64)-1)

static const DWORD kPauseSleepTime = 100;

CProgressSync::CProgressSync():
    _stopped(false),
    _paused(false),
    _filesProgressMode(false),
    _isDir(false),
    _totalBytes(UNDEFINED_VAL), _completedBytes(0),
    _totalFiles(UNDEFINED_VAL), _curFiles(0),
    _inSize(UNDEFINED_VAL),
    _outSize(UNDEFINED_VAL)
    {}

#define CHECK_STOP  if (_stopped) return E_ABORT; if (!_paused) return S_OK;
#define CRITICAL_LOCK NSynchronization::CCriticalSectionLock lock(_cs);

bool CProgressSync::Get_Paused()
{
  CRITICAL_LOCK
  return _paused;
}

HRESULT CProgressSync::CheckStop()
{
  for (;;)
  {
    {
      CRITICAL_LOCK
      CHECK_STOP
    }
    ::Sleep(kPauseSleepTime);
  }
}

void CProgressSync::Clear_Stop_Status()
{
  CRITICAL_LOCK
  if (_stopped)
    _stopped = false;
}

HRESULT CProgressSync::ScanProgress(UInt64 numFiles, UInt64 totalSize, const FString &fileName, bool isDir)
{
  {
    CRITICAL_LOCK
    _totalFiles = numFiles;
    _totalBytes = totalSize;
    _filePath = fs2us(fileName);
    _isDir = isDir;
    // _completedBytes = 0;
    CHECK_STOP
  }
  return CheckStop();
}

HRESULT CProgressSync::Set_NumFilesTotal(UInt64 val)
{
  {
    CRITICAL_LOCK
    _totalFiles = val;
    CHECK_STOP
  }
  return CheckStop();
}

void CProgressSync::Set_NumBytesTotal(UInt64 val)
{
  CRITICAL_LOCK
  _totalBytes = val;
}

void CProgressSync::Set_NumFilesCur(UInt64 val)
{
  CRITICAL_LOCK
  _curFiles = val;
}

HRESULT CProgressSync::Set_NumBytesCur(const UInt64 *val)
{
  {
    CRITICAL_LOCK
    if (val)
      _completedBytes = *val;
    CHECK_STOP
  }
  return CheckStop();
}

HRESULT CProgressSync::Set_NumBytesCur(UInt64 val)
{
  {
    CRITICAL_LOCK
    _completedBytes = val;
    CHECK_STOP
  }
  return CheckStop();
}

void CProgressSync::Set_Ratio(const UInt64 *inSize, const UInt64 *outSize)
{
  CRITICAL_LOCK
  if (inSize)
    _inSize = *inSize;
  if (outSize)
    _outSize = *outSize;
}

void CProgressSync::Set_TitleFileName(const UString &fileName)
{
  CRITICAL_LOCK
  _titleFileName = fileName;
}

void CProgressSync::Set_Status(const UString &s)
{
  CRITICAL_LOCK
  _status = s;
}

HRESULT CProgressSync::Set_Status2(const UString &s, const wchar_t *path, bool isDir)
{
  {
    CRITICAL_LOCK
    _status = s;
    if (path)
      _filePath = path;
    else
      _filePath.Empty();
    _isDir = isDir;
  }
  return CheckStop();
}

void CProgressSync::Set_FilePath(const wchar_t *path, bool isDir)
{
  CRITICAL_LOCK
  if (path)
    _filePath = path;
  else
    _filePath.Empty();
  _isDir = isDir;
}


void CProgressSync::AddError_Message(const wchar_t *message)
{
  CRITICAL_LOCK
  Messages.Add(message);
}

void CProgressSync::AddError_Message_Name(const wchar_t *message, const wchar_t *name)
{
  UString s;
  if (name && *name != 0)
    s += name;
  if (message && *message != 0)
  {
    if (!s.IsEmpty())
      s.Add_LF();
    s += message;
    if (!s.IsEmpty() && s.Back() == L'\n')
      s.DeleteBack();
  }
  AddError_Message(s);
}

void CProgressSync::AddError_Code_Name(HRESULT systemError, const wchar_t *name)
{
  UString s = NError::MyFormatMessage(systemError);
  if (systemError == 0)
    s = "Error";
  AddError_Message_Name(s, name);
}
