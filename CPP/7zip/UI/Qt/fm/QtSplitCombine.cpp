// QtSplitCombine.cpp
// ----------------------------------------------------------------------------
// See QtSplitCombine.h. CVolSeqName::GetNextName and GetNumberOfVolumes are
// VERBATIM from PanelSplitFile.cpp / SplitUtils.cpp. The two ProcessVirt() byte
// loops track CThreadSplit::ProcessVirt / CThreadCombine::ProcessVirt 1:1; the
// ONLY adaptations are:
//   * `Sync` -> `Sync()`  (the Qt base exposes the progress state as an accessor),
//   * the Win32 NIO::CInFile::Read / COutFile::Write / SeekToBegin / GetLength
//     used by the original are spelled with the LINUX FileIO.h primitives
//     (CInFile::ReadFull / COutFile::write_full / CFileBase::seekToBegin /
//     GetLength), and the buffer is a CMidBuffer (the Linux equivalent of the
//     original CMyBuffer, which is MidAlloc-backed and Win32-FM-header-tied).
// The semantics (1 MiB chunks, per-volume Create_NEW overwrite guard, preallocate
// -then-truncate, the volSize/volIndex consume-then-repeat-last rule, progress
// every >=4 MiB) are byte-for-byte identical.
// ----------------------------------------------------------------------------

#include "QtSplitCombine.h"

#include "../../../../Common/MyBuffer2.h"     // CMidBuffer (MidAlloc-backed)

#include "../../../../Windows/FileIO.h"
#include "../../../../Windows/FileFind.h"

using namespace NWindows;
using namespace NFile;

static const char * const g_Message_FileWriteError = "File write error";


// === CVolSeqName::GetNextName : VERBATIM from PanelSplitFile.cpp:63-78 =======
UString CVolSeqName::GetNextName()
{
  for (int i = (int)ChangedPart.Len() - 1; i >= 0; i--)
  {
    const wchar_t c = ChangedPart[i];
    if (c != L'9')
    {
      ChangedPart.ReplaceOneCharAtPos((unsigned)i, (wchar_t)(c + 1));
      break;
    }
    ChangedPart.ReplaceOneCharAtPos((unsigned)i, L'0');
    if (i == 0)
      ChangedPart.InsertAtFront(L'1');
  }
  return UnchangedPart + ChangedPart;
}


// === GetNumberOfVolumes : VERBATIM from SplitUtils.cpp:81-96 =================
UInt64 GetNumberOfVolumes(UInt64 size, const CRecordVector<UInt64> &volSizes)
{
  if (size == 0 || volSizes.Size() == 0)
    return 1;
  FOR_VECTOR (i, volSizes)
  {
    UInt64 volSize = volSizes[i];
    if (volSize >= size)
      return i + 1;
    size -= volSize;
  }
  UInt64 volSize = volSizes.Back();
  if (volSize == 0)
    return (UInt64)(Int64)-1;
  return volSizes.Size() + (size - 1) / volSize + 1;
}


// === CPreAllocOutFile : from PanelSplitFile.cpp:91-136 ======================
// (Write/PreAlloc spelled with the Linux COutFile primitives; same behavior.)
class CPreAllocOutFile
{
  UInt64 _preAllocSize;
public:
  NIO::COutFile File;
  UInt64 Written;

  CPreAllocOutFile(): _preAllocSize(0), Written(0) {}

  ~CPreAllocOutFile()
  {
    SetCorrectFileLength();
  }

  void PreAlloc(UInt64 preAllocSize)
  {
    _preAllocSize = 0;
    if (File.SetLength(preAllocSize))
      _preAllocSize = preAllocSize;
    File.seekToBegin();
  }

  bool Write(const void *data, UInt32 size, UInt32 &processedSize) throw()
  {
    size_t processed = 0;
    const ssize_t res = File.write_full(data, (size_t)size, processed);
    processedSize = (UInt32)processed;
    Written += processedSize;
    return res != -1;
  }

  void Close()
  {
    SetCorrectFileLength();
    Written = 0;
    _preAllocSize = 0;
    File.Close();
  }

  void SetCorrectFileLength()
  {
    if (Written < _preAllocSize)
    {
      File.SetLength(Written);
      _preAllocSize = 0;
    }
  }
};


static const UInt32 kBufSize = (1 << 20);


// === QtSplitWorker::ProcessVirt : CThreadSplit::ProcessVirt (PanelSplitFile.cpp:141-232)
HRESULT QtSplitWorker::ProcessVirt()
{
  NIO::CInFile inFile;
  if (!inFile.Open(FilePath.Ptr()))
    return GetLastError_noZero_HRESULT();

  CPreAllocOutFile outFile;

  CMidBuffer buffer;
  buffer.Alloc(kBufSize);
  if (!buffer.IsAllocated())
    return E_OUTOFMEMORY;

  CVolSeqName seqName;
  seqName.SetNumDigits(NumVolumes);

  UInt64 length;
  if (!inFile.GetLength(length))
    return GetLastError_noZero_HRESULT();

  CProgressSync &sync = Sync();
  sync.Set_NumBytesTotal(length);

  UInt64 pos = 0;
  UInt64 prev = 0;
  UInt64 numFiles = 0;
  unsigned volIndex = 0;

  for (;;)
  {
    UInt64 volSize;
    if (volIndex < VolumeSizes.Size())
      volSize = VolumeSizes[volIndex];
    else
      volSize = VolumeSizes.Back();

    UInt32 needSize = kBufSize;
    {
      const UInt64 rem = volSize - outFile.Written;
      if (needSize > rem)
        needSize = (UInt32)rem;
    }
    UInt32 processedSize;
    {
      size_t processed = 0;
      if (!inFile.ReadFull((Byte *)buffer, (size_t)needSize, processed))
        return GetLastError_noZero_HRESULT();
      processedSize = (UInt32)processed;
    }
    if (processedSize == 0)
      return S_OK;
    needSize = processedSize;

    if (outFile.Written == 0)
    {
      FString name = VolBasePath;
      name.Add_Dot();
      name += us2fs(seqName.GetNextName());
      sync.Set_FilePath(fs2us(name));
      if (!outFile.File.Create_NEW(name))
      {
        const HRESULT res = GetLastError_noZero_HRESULT();
        AddErrorPath(name);
        return res;
      }
      UInt64 expectSize = volSize;
      if (pos < length)
      {
        const UInt64 rem = length - pos;
        if (expectSize > rem)
          expectSize = rem;
      }
      outFile.PreAlloc(expectSize);
    }

    if (!outFile.Write((Byte *)buffer, needSize, processedSize))
      return GetLastError_noZero_HRESULT();
    if (needSize != processedSize)
      throw g_Message_FileWriteError;

    pos += processedSize;

    if (outFile.Written == volSize)
    {
      outFile.Close();
      sync.Set_NumFilesCur(++numFiles);
      if (volIndex < VolumeSizes.Size())
        volIndex++;
    }

    if (pos - prev >= ((UInt32)1 << 22) || outFile.Written == 0)
    {
      RINOK(sync.Set_NumBytesCur(pos))
      prev = pos;
    }
  }
}


// === QtCombineWorker::ProcessVirt : CThreadCombine::ProcessVirt (PanelSplitFile.cpp:355-409)
HRESULT QtCombineWorker::ProcessVirt()
{
  NIO::COutFile outFile;
  if (!outFile.Create_NEW(OutputPath))
  {
    const HRESULT res = GetLastError_noZero_HRESULT();
    AddErrorPath(OutputPath);
    return res;
  }

  CProgressSync &sync = Sync();
  sync.Set_NumBytesTotal(TotalSize);

  CMidBuffer bufferObject;
  bufferObject.Alloc(kBufSize);
  if (!bufferObject.IsAllocated())
    return E_OUTOFMEMORY;
  Byte *buffer = (Byte *)bufferObject;
  UInt64 pos = 0;
  FOR_VECTOR (i, Names)
  {
    NIO::CInFile inFile;
    const FString nextName = InputDirPrefix + Names[i];
    if (!inFile.Open(nextName.Ptr()))
    {
      const HRESULT res = GetLastError_noZero_HRESULT();
      AddErrorPath(nextName);
      return res;
    }
    sync.Set_FilePath(fs2us(nextName));
    for (;;)
    {
      UInt32 processedSize;
      {
        size_t processed = 0;
        if (!inFile.ReadFull(buffer, (size_t)kBufSize, processed))
        {
          const HRESULT res = GetLastError_noZero_HRESULT();
          AddErrorPath(nextName);
          return res;
        }
        processedSize = (UInt32)processed;
      }
      if (processedSize == 0)
        break;
      const UInt32 needSize = processedSize;
      {
        size_t written = 0;
        const ssize_t res = outFile.write_full(buffer, (size_t)needSize, written);
        processedSize = (UInt32)written;
        if (res == -1)
        {
          const HRESULT hr = GetLastError_noZero_HRESULT();
          AddErrorPath(OutputPath);
          return hr;
        }
      }
      if (needSize != processedSize)
        throw g_Message_FileWriteError;
      pos += processedSize;
      RINOK(sync.Set_NumBytesCur(pos))
    }
  }
  return S_OK;
}
