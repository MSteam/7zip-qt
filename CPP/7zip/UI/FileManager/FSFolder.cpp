// FSFolder.cpp

#include "StdAfx.h"

// [B.2 Linux port] <winternl.h> / <ddk/ntddk.h> are Win32 SDK headers (NTSTATUS,
// IO_STATUS_BLOCK for the NtQueryInformationFile change-time query below). They
// do not exist on Linux; the whole NT-query path is itself _WIN32-guarded later,
// so on Linux this entire block is skipped. Windows branch is verbatim-original.
#ifdef _WIN32

#ifdef __MINGW32_VERSION
// #if !defined(_MSC_VER) && (__GNUC__) && (__GNUC__ < 10)
// for old mingw
#include <ddk/ntddk.h>
#else
#ifndef Z7_OLD_WIN_SDK
  #if !defined(_M_IA64)
    #include <winternl.h>
  #endif
#else
typedef LONG NTSTATUS;
typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;
#endif
#endif

#endif // _WIN32

#include "../../../Common/ComTry.h"
#include "../../../Common/Defs.h"
#include "../../../Common/StringConvert.h"
#include "../../../Common/UTFConvert.h"

#include "../../../Windows/DLL.h"
#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileIO.h"
#include "../../../Windows/FileName.h"
#include "../../../Windows/PropVariant.h"

#include "../../PropID.h"

#include "FSFolder.h"

// [B.2 Linux port] FSDrives.h (CFSDrives), NetFolder.h (CNetFolder ->
// Windows/Net.h) and SysIconUtils.h (-> <CommCtrl.h>, the Win32 common-controls
// GUI header) are Win32-GUI-coupled File-Manager units. FSFolder only references
// them on Windows-specific code paths (drive-root parent folder, shell icon
// index) that are themselves _WIN32-guarded below, so on Linux they are not
// needed and not compiled. Windows branch is verbatim-original.
#ifdef _WIN32
#include "FSDrives.h"
#ifndef UNDER_CE
#include "NetFolder.h"
#endif
#include "SysIconUtils.h"
#endif

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0501
#ifdef _APISETFILE_
// Windows SDK 8.1 defines in fileapi.h the function GetCompressedFileSizeW only if _WIN32_WINNT >= 0x0501
// But real support version for that function is NT 3.1 (probably)
// So we must define GetCompressedFileSizeW
EXTERN_C_BEGIN
WINBASEAPI DWORD WINAPI GetCompressedFileSizeW(LPCWSTR lpFileName, LPDWORD lpFileSizeHigh);
EXTERN_C_END
#endif
#endif

using namespace NWindows;
using namespace NFile;
using namespace NFind;
using namespace NDir;
using namespace NName;

#ifndef USE_UNICODE_FSTRING
int CompareFileNames_ForFolderList(const FChar *s1, const FChar *s2);
int CompareFileNames_ForFolderList(const FChar *s1, const FChar *s2)
{
  return CompareFileNames_ForFolderList(fs2us(s1), fs2us(s2));
}
#endif

namespace NFsFolder {

static const Byte kProps[] =
{
  kpidName,
  kpidSize,
  kpidMTime,
  kpidCTime,
  kpidATime,
 #ifdef FS_SHOW_LINKS_INFO
  kpidChangeTime,
 #endif
  kpidAttrib,
  kpidPackSize,
 #ifdef FS_SHOW_LINKS_INFO
  kpidINode,
  kpidLinks,
 #endif
  kpidComment,
  kpidNumSubDirs,
  kpidNumSubFiles,
  kpidPrefix
};

HRESULT CFSFolder::Init(const FString &path /* , IFolderFolder *parentFolder */)
{
  // _parentFolder = parentFolder;
  _path = path;

  #ifdef _WIN32

  _findChangeNotification.FindFirst(_path, false,
        FILE_NOTIFY_CHANGE_FILE_NAME
      | FILE_NOTIFY_CHANGE_DIR_NAME
      | FILE_NOTIFY_CHANGE_ATTRIBUTES
      | FILE_NOTIFY_CHANGE_SIZE
      | FILE_NOTIFY_CHANGE_LAST_WRITE
      /*
      | FILE_NOTIFY_CHANGE_LAST_ACCESS
      | FILE_NOTIFY_CHANGE_CREATION
      | FILE_NOTIFY_CHANGE_SECURITY
      */
      );

  if (!_findChangeNotification.IsHandleAllocated())
  {
    const HRESULT lastError = GetLastError_noZero_HRESULT();
    CFindFile findFile;
    CFileInfo fi;
    FString path2 = _path;
    path2.Add_Char('*'); // CHAR_ANY_MASK;
    if (!findFile.FindFirst(path2, fi))
      return lastError;
  }

  #endif
  
  return S_OK;
}


HRESULT CFsFolderStat::Enumerate()
{
  if (Progress)
  {
    RINOK(Progress->SetCompleted(NULL))
  }
  Path.Add_PathSepar();
  const unsigned len = Path.Len();
  CEnumerator enumerator;
  enumerator.SetDirPrefix(Path);
#ifdef _WIN32
  CDirEntry fi;
  while (enumerator.Next(fi))
  {
    if (fi.IsDir())
    {
      NumFolders++;
      Path.DeleteFrom(len);
      Path += fi.Name;
      RINOK(Enumerate())
    }
    else
    {
      NumFiles++;
      Size += fi.Size;
    }
  }
#else
  // [B.2 Linux port] The Win32 CEnumerator yields a full CFileInfo (CDirEntry is
  // typedef'd to CFileInfo) directly from Next(). The Linux CEnumerator is a thin
  // readdir() wrapper: Next(CDirEntry&, bool&) yields only {name, d_type}, and a
  // full CFileInfo (size, IsDir) must be resolved per-entry via Fill_FileInfo()
  // (lstat). This is exactly the two-phase pattern the engine's own portable
  // CDirItems::EnumerateOneDir uses (UI/Common/EnumDirItems.cpp). The Windows
  // branch (#if) is verbatim-original.
  for (;;)
  {
    bool found;
    CDirEntry de;
    if (!enumerator.Next(de, found))
      break;
    if (!found)
      break;
    CFileInfo fi;
    if (!enumerator.Fill_FileInfo(de, fi, true /* followLink: count real file sizes */))
      continue;
    if (fi.IsDir())
    {
      NumFolders++;
      Path.DeleteFrom(len);
      Path += de.Name;
      RINOK(Enumerate())
    }
    else
    {
      NumFiles++;
      Size += fi.Size;
    }
  }
#endif
  return S_OK;
}

#if !defined(UNDER_CE) && defined(_WIN32)

// [B.2 Linux port] MyGetCompressedFileSizeW wraps the Win32 GetCompressedFileSizeW
// (NTFS compressed/sparse on-disk size). There is no portable equivalent; on
// Linux the only caller (GetProperty kpidPackSize) is _WIN32-guarded too and
// falls back to fi.Size. Windows branch is verbatim-original.
bool MyGetCompressedFileSizeW(CFSTR path, UInt64 &size);
bool MyGetCompressedFileSizeW(CFSTR path, UInt64 &size)
{
  DWORD highPart;
  DWORD lowPart = INVALID_FILE_SIZE;
  IF_USE_MAIN_PATH
  {
    lowPart = ::GetCompressedFileSizeW(fs2us(path), &highPart);
    if (lowPart != INVALID_FILE_SIZE || ::GetLastError() == NO_ERROR)
    {
      size = ((UInt64)highPart << 32) | lowPart;
      return true;
    }
  }
  #ifdef Z7_LONG_PATH
  if (USE_SUPER_PATH)
  {
    UString superPath;
    if (GetSuperPath(path, superPath, USE_MAIN_PATH))
    {
      lowPart = ::GetCompressedFileSizeW(superPath, &highPart);
      if (lowPart != INVALID_FILE_SIZE || ::GetLastError() == NO_ERROR)
      {
        size = ((UInt64)highPart << 32) | lowPart;
        return true;
      }
    }
  }
  #endif
  return false;
}

#endif

HRESULT CFSFolder::LoadSubItems(int dirItem, const FString &relPrefix)
{
  const unsigned startIndex = Folders.Size();
  {
    CEnumerator enumerator;
    enumerator.SetDirPrefix(_path + relPrefix);
    CDirItem fi;
    fi.FolderStat_Defined = false;
    fi.NumFolders = 0;
    fi.NumFiles = 0;
    fi.Parent = dirItem;

#ifndef _WIN32
    // [B.2 Linux port] The Win32 CEnumerator::Next(fi) fills a full CFileInfo
    // (CDirItem's base) per call. The Linux CEnumerator is a readdir() wrapper
    // that yields a lightweight CDirEntry; the full CFileInfo (size, times,
    // mode/IsDir, inode, nlink) is resolved per-entry via Fill_FileInfo() (lstat).
    // We first collect all CDirEntry's, then in the loop below Fill_FileInfo each
    // into the CDirItem base before running the SHARED per-item body. This is the
    // same two-phase pattern as the engine's CDirItems::EnumerateOneDir
    // (UI/Common/EnumDirItems.cpp). The Win32 loop header (#else) is verbatim.
    CObjectVector<CDirEntry> _entries;
    for (;;)
    {
      bool found;
      CDirEntry de;
      if (!enumerator.Next(de, found) || !found)
        break;
      _entries.Add(de);
    }
    FOR_VECTOR (_ei, _entries)
    {
      const CDirEntry &de = _entries[_ei];
      // Fill the CFileInfo base of fi; followLink=false so a symlink is reported
      // as itself (matches the Windows listing, which does not follow links here).
      if (!enumerator.Fill_FileInfo(de, fi, false))
        continue;
      fi.Name = de.Name;
#else
    while (enumerator.Next(fi))
    {
#endif
      if (fi.IsDir())
      {
        fi.Size = 0;
        if (_flatMode)
          Folders.Add(relPrefix + fi.Name + FCHAR_PATH_SEPARATOR);
      }
      else
      {
        /*
        fi.PackSize_Defined = true;
        if (!MyGetCompressedFileSizeW(_path + relPrefix + fi.Name, fi.PackSize))
          fi.PackSize = fi.Size;
        */
      }
      
     #ifndef UNDER_CE

      fi.Reparse.Free();
      fi.PackSize_Defined = false;
    
     #ifdef FS_SHOW_LINKS_INFO
      fi.FileInfo_Defined = false;
      fi.FileInfo_WasRequested = false;
      fi.FileIndex = 0;
      fi.NumLinks = 0;
      fi.ChangeTime_Defined = false;
      fi.ChangeTime_WasRequested = false;
     #endif
      
      fi.PackSize = fi.Size;
     
     #ifdef FS_SHOW_LINKS_INFO
     #ifdef _WIN32
      // [B.2 Linux port] CFileInfoBase::HasReparsePoint() and the 3-arg
      // NIO::GetReparseData(path, buf, BY_HANDLE_FILE_INFORMATION*) (NTFS reparse
      // points + by-handle file info) are _WIN32-only. On Linux there are no NTFS
      // reparse points and CDirItem::Reparse stays empty (set above by .Free()),
      // so the GetRawProp(kpidNtReparse) path returns nothing - matching how a
      // non-reparse item behaves on Windows. Windows branch is verbatim-original.
      if (fi.HasReparsePoint())
      {
        fi.FileInfo_WasRequested = true;
        BY_HANDLE_FILE_INFORMATION info;
        NIO::GetReparseData(_path + relPrefix + fi.Name, fi.Reparse, &info);
        fi.NumLinks = info.nNumberOfLinks;
        fi.FileIndex = (((UInt64)info.nFileIndexHigh) << 32) + info.nFileIndexLow;
        fi.FileInfo_Defined = true;
      }
     #endif
     #endif

     #endif // UNDER_CE

      /* unsigned fileIndex = */ Files.Add(fi);

      #if defined(_WIN32) && !defined(UNDER_CE)
      /*
      if (_scanAltStreams)
      {
        CStreamEnumerator enumerator(_path + relPrefix + fi.Name);
        CStreamInfo si;
        for (;;)
        {
          bool found;
          if (!enumerator.Next(si, found))
          {
            // if (GetLastError() == ERROR_ACCESS_DENIED)
            //   break;
            // return E_FAIL;
            break;
          }
          if (!found)
            break;
          if (si.IsMainStream())
            continue;
          CAltStream ss;
          ss.Parent = fileIndex;
          ss.Name = si.GetReducedName();
          ss.Size = si.Size;
          ss.PackSize_Defined = false;
          ss.PackSize = si.Size;
          Streams.Add(ss);
        }
      }
      */
      #endif
    }
  }
  if (!_flatMode)
    return S_OK;

  const unsigned endIndex = Folders.Size();
  for (unsigned i = startIndex; i < endIndex; i++)
    LoadSubItems((int)i, Folders[i]);
  return S_OK;
}

Z7_COM7F_IMF(CFSFolder::LoadItems())
{
  Int32 dummy;
  WasChanged(&dummy);
  Clear();
  RINOK(LoadSubItems(-1, FString()))
  _commentsAreLoaded = false;
  return S_OK;
}

static CFSTR const kDescriptionFileName = FTEXT("descript.ion");

bool CFSFolder::LoadComments()
{
  _comments.Clear();
  _commentsAreLoaded = true;
  NIO::CInFile file;
  if (!file.Open(_path + kDescriptionFileName))
    return false;
  UInt64 len;
  if (!file.GetLength(len))
    return false;
  if (len >= (1 << 28))
    return false;
  AString s;
  char *p = s.GetBuf((unsigned)(size_t)len);
  size_t processedSize;
  if (!file.ReadFull(p, (unsigned)(size_t)len, processedSize))
    return false;
  s.ReleaseBuf_CalcLen((unsigned)(size_t)len);
  if (processedSize != len)
    return false;
  file.Close();
  UString unicodeString;
  if (!ConvertUTF8ToUnicode(s, unicodeString))
    return false;
  return _comments.ReadFromString(unicodeString);
}

bool CFSFolder::SaveComments()
{
  AString utf;
  {
    UString unicode;
    _comments.SaveToString(unicode);
    ConvertUnicodeToUTF8(unicode, utf);
  }
  if (!utf.IsAscii())
    utf.Insert(0, "\xEF\xBB\xBF" "\r\n");

  FString path = _path + kDescriptionFileName;
 #ifdef _WIN32
  // We must set same attrib. COutFile::CreateAlways can fail, if file has another attrib.
  DWORD attrib = FILE_ATTRIBUTE_NORMAL;
  {
    CFileInfo fi;
    if (fi.Find(path))
      attrib = fi.Attrib;
  }
  NIO::COutFile file;
  if (!file.Create_ALWAYS_with_Attribs(path, attrib))
    return false;
  UInt32 processed;
  file.Write(utf, utf.Len(), processed);
 #else
  // [B.2 Linux port] CFileInfoBase::Attrib, COutFile::Create_ALWAYS_with_Attribs
  // and COutFile::Write are _WIN32-only. The "preserve the file attribute" step
  // is a Windows-attrib concern with no Linux equivalent; on Linux we just
  // (re)create the descript.ion file and write it with the portable WriteFull.
  // (SaveComments is a write/SetProperty path, not exercised by B.2 browsing.)
  NIO::COutFile file;
  if (!file.Create_ALWAYS(path))
    return false;
  if (!file.WriteFull(utf, utf.Len()))
    return false;
 #endif
  _commentsAreLoaded = false;
  return true;
}

Z7_COM7F_IMF(CFSFolder::GetNumberOfItems(UInt32 *numItems))
{
  *numItems = Files.Size() /* + Streams.Size() */;
  return S_OK;
}

#ifdef USE_UNICODE_FSTRING

Z7_COM7F_IMF(CFSFolder::GetItemPrefix(UInt32 index, const wchar_t **name, unsigned *len))
{
  *name = NULL;
  *len = 0;
  /*
  if (index >= Files.Size())
    index = Streams[index - Files.Size()].Parent;
  */
  CDirItem &fi = Files[index];
  if (fi.Parent >= 0)
  {
    const FString &fo = Folders[fi.Parent];
    USE_UNICODE_FSTRING
    *name = fo;
    *len = fo.Len();
  }
  return S_OK;
}

Z7_COM7F_IMF(CFSFolder::GetItemName(UInt32 index, const wchar_t **name, unsigned *len))
{
  *name = NULL;
  *len = 0;
  if (index < Files.Size())
  {
    CDirItem &fi = Files[index];
    *name = fi.Name;
    *len = fi.Name.Len();
    return S_OK;
  }
  else
  {
    // const CAltStream &ss = Streams[index - Files.Size()];
    // *name = ss.Name;
    // *len = ss.Name.Len();
    //
    // change it;
  }
  return S_OK;
}

Z7_COM7F_IMF2(UInt64, CFSFolder::GetItemSize(UInt32 index))
{
  /*
  if (index >= Files.Size())
    return Streams[index - Files.Size()].Size;
  */
  CDirItem &fi = Files[index];
  return fi.IsDir() ? 0 : fi.Size;
}

#endif


// [B.2 Linux port] The link/inode/change-time helpers below
// (CFSFolder::ReadFileInfo, CFSFolder::ReadChangeTime) and their NT typedef
// block are all _WIN32-only:
//   * ReadFileInfo uses NIO::CFileBase::GetFileInformation +
//     BY_HANDLE_FILE_INFORMATION (the Win32 by-handle file info: hard-link count
//     and 64-bit file index/inode).
//   * ReadChangeTime dynamically binds NtQueryInformationFile from ntdll.dll to
//     read the NTFS "change time" - a Windows-kernel API with no Linux analog.
// On Linux these are not compiled; the kpidLinks / kpidINode / kpidChangeTime
// property reads that would call them are likewise _WIN32-guarded at their call
// sites (GetProperty / CompareItems), so those props simply stay empty/undefined
// - exactly as on Windows when the queries fail. Windows branch is verbatim.
#if defined(FS_SHOW_LINKS_INFO) && defined(_WIN32)

bool CFSFolder::ReadFileInfo(CDirItem &di)
{
  di.FileInfo_WasRequested = true;
  BY_HANDLE_FILE_INFORMATION info;
  memset(&info, 0, sizeof(info)); // for vc6-O2
  if (!NIO::CFileBase::GetFileInformation(_path + GetRelPath(di), &info))
    return false;
  di.NumLinks = info.nNumberOfLinks;
  di.FileIndex = (((UInt64)info.nFileIndexHigh) << 32) + info.nFileIndexLow;
  di.FileInfo_Defined = true;
  return true;
}


EXTERN_C_BEGIN

typedef struct
{
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  ULONG FileAttributes;
  UInt32 Reserved; // it's expected for alignment
}
Z7_WIN_FILE_BASIC_INFORMATION;


typedef enum
{
  Z7_WIN_FileDirectoryInformation = 1,
  Z7_WIN_FileFullDirectoryInformation,
  Z7_WIN_FileBothDirectoryInformation,
  Z7_WIN_FileBasicInformation
}
Z7_WIN_FILE_INFORMATION_CLASS;


#if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0500) && !defined(_M_IA64)
#define Z7_WIN_NTSTATUS  NTSTATUS
#define Z7_WIN_IO_STATUS_BLOCK  IO_STATUS_BLOCK
#else
typedef LONG Z7_WIN_NTSTATUS;
typedef struct
{
  union
  {
    Z7_WIN_NTSTATUS Status;
    PVOID Pointer;
  } DUMMYUNIONNAME;
  ULONG_PTR Information;
} Z7_WIN_IO_STATUS_BLOCK;
#endif


typedef Z7_WIN_NTSTATUS (WINAPI * Func_NtQueryInformationFile)(
    HANDLE handle, Z7_WIN_IO_STATUS_BLOCK *io,
    void *ptr, LONG len, Z7_WIN_FILE_INFORMATION_CLASS cls);

#define MY_STATUS_SUCCESS 0

EXTERN_C_END

static Func_NtQueryInformationFile f_NtQueryInformationFile;
static bool g_NtQueryInformationFile_WasRequested = false;

Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION

void CFSFolder::ReadChangeTime(CDirItem &di)
{
  di.ChangeTime_WasRequested = true;

  if (!g_NtQueryInformationFile_WasRequested)
  {
       g_NtQueryInformationFile_WasRequested = true;
       f_NtQueryInformationFile = Z7_GET_PROC_ADDRESS(
    Func_NtQueryInformationFile, ::GetModuleHandleW(L"ntdll.dll"),
        "NtQueryInformationFile");
  }
  if (!f_NtQueryInformationFile)
    return;

  NIO::CInFile file;
  if (!file.Open_for_ReadAttributes(_path + GetRelPath(di)))
    return;
  Z7_WIN_FILE_BASIC_INFORMATION fbi;
  Z7_WIN_IO_STATUS_BLOCK IoStatusBlock;
  const Z7_WIN_NTSTATUS status = f_NtQueryInformationFile(file.GetHandle(), &IoStatusBlock,
      &fbi, sizeof(fbi), Z7_WIN_FileBasicInformation);
  if (status != MY_STATUS_SUCCESS)
    return;
  if (IoStatusBlock.Information != sizeof(fbi))
    return;
  di.ChangeTime.dwLowDateTime = fbi.ChangeTime.u.LowPart;
  di.ChangeTime.dwHighDateTime = (DWORD)fbi.ChangeTime.u.HighPart;
  di.ChangeTime_Defined = true;
}

#endif // FS_SHOW_LINKS_INFO


Z7_COM7F_IMF(CFSFolder::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value))
{
  NCOM::CPropVariant prop;
  /*
  if (index >= Files.Size())
  {
    CAltStream &ss = Streams[index - Files.Size()];
    CDirItem &fi = Files[ss.Parent];
    switch (propID)
    {
      case kpidIsDir: prop = false; break;
      case kpidIsAltStream: prop = true; break;
      case kpidName: prop = fs2us(fi.Name) + ss.Name; break;
      case kpidSize: prop = ss.Size; break;
      case kpidPackSize:
        #ifdef UNDER_CE
        prop = ss.Size;
        #else
        if (!ss.PackSize_Defined)
        {
          ss.PackSize_Defined = true;
          if (!MyGetCompressedFileSizeW(_path + GetRelPath(fi) + us2fs(ss.Name), ss.PackSize))
            ss.PackSize = ss.Size;
        }
        prop = ss.PackSize;
        #endif
        break;
      case kpidComment: break;
      default: index = ss.Parent;
    }
    if (index >= Files.Size())
    {
      prop.Detach(value);
      return S_OK;
    }
  }
  */
  CDirItem &fi = Files[index];
  switch (propID)
  {
    case kpidIsDir: prop = fi.IsDir(); break;
    case kpidIsAltStream: prop = false; break;
    case kpidName: prop = fs2us(fi.Name); break;
    case kpidSize: if (!fi.IsDir() || fi.FolderStat_Defined) prop = fi.Size; break;
    case kpidPackSize:
      #if defined(UNDER_CE) || !defined(_WIN32)
      // [B.2 Linux port] no GetCompressedFileSizeW (NTFS on-disk compressed size)
      // on Linux; pack size == logical size, the same fallback Windows uses when
      // the query fails. Windows branch (#else) is verbatim-original.
      prop = fi.Size;
      #else
      if (!fi.PackSize_Defined)
      {
        fi.PackSize_Defined = true;
        if (fi.IsDir () || !MyGetCompressedFileSizeW(_path + GetRelPath(fi), fi.PackSize))
          fi.PackSize = fi.Size;
      }
      prop = fi.PackSize;
      #endif
      break;

    #ifdef FS_SHOW_LINKS_INFO

    case kpidLinks:
      #ifdef UNDER_CE
      // prop = fi.NumLinks;
      #elif !defined(_WIN32)
      // [B.2 Linux port] ReadFileInfo (Win32 by-handle file info) is not built;
      // the portable CFileInfo already carries the POSIX hard-link count (nlink)
      // from the lstat/stat-based scan, so report it directly.
      prop = (UInt32)fi.nlink;
      #else
      if (!fi.FileInfo_WasRequested)
        ReadFileInfo(fi);
      if (fi.FileInfo_Defined)
        prop = fi.NumLinks;
      #endif
      break;

    case kpidINode:
      #ifdef UNDER_CE
      // prop = fi.FileIndex;
      #elif !defined(_WIN32)
      // [B.2 Linux port] same as kpidLinks: use the POSIX inode number (ino)
      // already present on CFileInfo instead of the Win32 64-bit file index.
      prop = (UInt64)fi.ino;
      #else
      if (!fi.FileInfo_WasRequested)
        ReadFileInfo(fi);
      if (fi.FileInfo_Defined)
        prop = fi.FileIndex;
      #endif
      break;

    case kpidChangeTime:
      #ifdef _WIN32
      if (!fi.ChangeTime_WasRequested)
        ReadChangeTime(fi);
      if (fi.ChangeTime_Defined)
        prop = fi.ChangeTime;
      #else
      // [B.2 Linux port] the NTFS "change time" (ReadChangeTime via ntdll
      // NtQueryInformationFile) has no portable equivalent; leave undefined, as
      // Windows does when the NT query fails. Windows branch is verbatim.
      #endif
      break;

    #endif

    // [B.2 Linux port] CFileInfoBase::Attrib is _WIN32-only; GetWinAttrib() maps
    // the POSIX mode to the same Windows attribute bitmask kpidAttrib expects
    // (identical value on Windows). Same accessor the Agent.cpp B.0 port uses.
    #ifdef _WIN32
    case kpidAttrib: prop = (UInt32)fi.Attrib; break;
    #else
    case kpidAttrib: prop = (UInt32)fi.GetWinAttrib(); break;
    #endif
    // [B.2 Linux port] On Windows CFiTime IS a FILETIME, so the verbatim
    // `prop = fi.CTime` uses CPropVariant's FILETIME assignment. On Linux CFiTime
    // is a `timespec`; the engine's portable helper PropVariant_SetFrom_FiTime
    // converts it to the same NTFS-time PROPVARIANT the view formats (so the
    // displayed time matches a Windows listing). Windows branch is verbatim.
    #ifdef _WIN32
    case kpidCTime: prop = fi.CTime; break;
    case kpidATime: prop = fi.ATime; break;
    case kpidMTime: prop = fi.MTime; break;
    #else
    case kpidCTime: PropVariant_SetFrom_FiTime(prop, fi.CTime); break;
    case kpidATime: PropVariant_SetFrom_FiTime(prop, fi.ATime); break;
    case kpidMTime: PropVariant_SetFrom_FiTime(prop, fi.MTime); break;
    #endif
    case kpidComment:
    {
      if (!_commentsAreLoaded)
        LoadComments();
      UString comment;
      if (_comments.GetValue(fs2us(GetRelPath(fi)), comment))
      {
        int pos = comment.Find((wchar_t)4);
        if (pos >= 0)
          comment.DeleteFrom((unsigned)pos);
        prop = comment;
      }
      break;
    }
    case kpidPrefix:
      if (fi.Parent >= 0)
        prop = fs2us(Folders[fi.Parent]);
      break;
    case kpidNumSubDirs: if (fi.IsDir() && fi.FolderStat_Defined) prop = fi.NumFolders; break;
    case kpidNumSubFiles: if (fi.IsDir() && fi.FolderStat_Defined) prop = fi.NumFiles; break;
  }
  prop.Detach(value);
  return S_OK;
}


// ---------- IArchiveGetRawProps ----------


Z7_COM7F_IMF(CFSFolder::GetNumRawProps(UInt32 *numProps))
{
  *numProps = 1;
  return S_OK;
}

Z7_COM7F_IMF(CFSFolder::GetRawPropInfo(UInt32 /* index */, BSTR *name, PROPID *propID))
{
  *name = NULL;
  *propID = kpidNtReparse;
  return S_OK;
}

Z7_COM7F_IMF(CFSFolder::GetParent(UInt32 /* index */, UInt32 * /* parent */, UInt32 * /* parentType */))
{
  return E_FAIL;
}

Z7_COM7F_IMF(CFSFolder::GetRawProp(UInt32 index, PROPID propID,
    const void **data, UInt32 *dataSize, UInt32 *propType))
{
  #ifdef UNDER_CE
  UNUSED(index)
  UNUSED(propID)
  #endif

  *data = NULL;
  *dataSize = 0;
  *propType = 0;
  
  #ifndef UNDER_CE
  if (propID == kpidNtReparse)
  {
    const CDirItem &fi = Files[index];
    const CByteBuffer &buf = fi.Reparse;
    if (buf.Size() == 0)
      return S_OK;
    *data = buf;
    *dataSize = (UInt32)buf.Size();
    *propType = NPropDataType::kRaw;
    return S_OK;
  }
  #endif
  
  return S_OK;
}


// returns Position of extension including '.'

static inline CFSTR GetExtensionPtr(const FString &name)
{
  const int dotPos = name.ReverseFind_Dot();
  return name.Ptr((dotPos < 0) ? name.Len() : (unsigned)dotPos);
}

Z7_COM7F_IMF2(Int32, CFSFolder::CompareItems(UInt32 index1, UInt32 index2, PROPID propID, Int32 /* propIsRaw */))
{
  /*
  const CAltStream *ss1 = NULL;
  const CAltStream *ss2 = NULL;
  if (index1 >= Files.Size()) { ss1 = &Streams[index1 - Files.Size()]; index1 = ss1->Parent; }
  if (index2 >= Files.Size()) { ss2 = &Streams[index2 - Files.Size()]; index2 = ss2->Parent; }
  */
  CDirItem &fi1 = Files[index1];
  CDirItem &fi2 = Files[index2];

  switch (propID)
  {
    case kpidName:
    {
      const int comp = CompareFileNames_ForFolderList(fi1.Name, fi2.Name);
      /*
      if (comp != 0)
        return comp;
      if (!ss1)
        return ss2 ? -1 : 0;
      if (!ss2)
        return 1;
      return MyStringCompareNoCase(ss1->Name, ss2->Name);
      */
      return comp;
    }
    case kpidSize:
      return MyCompare(
          /* ss1 ? ss1->Size : */ fi1.Size,
          /* ss2 ? ss2->Size : */ fi2.Size);
    // [B.2 Linux port] CFileInfoBase::Attrib is _WIN32-only; compare the mapped
    // GetWinAttrib() value (identical to Attrib on Windows). Same idiom as above.
    #ifdef _WIN32
    case kpidAttrib: return MyCompare(fi1.Attrib, fi2.Attrib);
    #else
    case kpidAttrib: return MyCompare(fi1.GetWinAttrib(), fi2.GetWinAttrib());
    #endif
    // [B.2 Linux port] On Windows CFiTime is FILETIME, so the verbatim
    // ::CompareFileTime applies. On Linux CFiTime is a timespec; the engine's
    // portable Compare_FiTime (a macro for ::CompareFileTime on Windows, a real
    // function on Linux - Windows/TimeUtils.h) compares them. Same ordering.
    case kpidCTime: return Compare_FiTime(&fi1.CTime, &fi2.CTime);
    case kpidATime: return Compare_FiTime(&fi1.ATime, &fi2.ATime);
    case kpidMTime: return Compare_FiTime(&fi1.MTime, &fi2.MTime);
    case kpidIsDir:
    {
      const bool isDir1 = /* ss1 ? false : */ fi1.IsDir();
      const bool isDir2 = /* ss2 ? false : */ fi2.IsDir();
      if (isDir1 == isDir2)
        return 0;
      return isDir1 ? -1 : 1;
    }
    case kpidPackSize:
    {
      #ifdef UNDER_CE
      return MyCompare(fi1.Size, fi2.Size);
      #else
      // PackSize can be undefined here
      return MyCompare(
          /* ss1 ? ss1->PackSize : */ fi1.PackSize,
          /* ss2 ? ss2->PackSize : */ fi2.PackSize);
      #endif
    }
    
    #ifdef FS_SHOW_LINKS_INFO
    case kpidINode:
    {
      #if defined(UNDER_CE)
      #elif !defined(_WIN32)
      // [B.2 Linux port] compare the POSIX inode numbers directly (ReadFileInfo
      // is Win32-only); see the GetProperty kpidINode note.
      return MyCompare(fi1.ino, fi2.ino);
      #else
      if (!fi1.FileInfo_WasRequested) ReadFileInfo(fi1);
      if (!fi2.FileInfo_WasRequested) ReadFileInfo(fi2);
      return MyCompare(
          fi1.FileIndex,
          fi2.FileIndex);
      #endif
    }
    case kpidLinks:
    {
      #if defined(UNDER_CE)
      #elif !defined(_WIN32)
      // [B.2 Linux port] compare the POSIX hard-link counts directly.
      return MyCompare(fi1.nlink, fi2.nlink);
      #else
      if (!fi1.FileInfo_WasRequested) ReadFileInfo(fi1);
      if (!fi2.FileInfo_WasRequested) ReadFileInfo(fi2);
      return MyCompare(
          fi1.NumLinks,
          fi2.NumLinks);
      #endif
    }
    #endif

    case kpidComment:
    {
      // change it !
      UString comment1, comment2;
      _comments.GetValue(fs2us(GetRelPath(fi1)), comment1);
      _comments.GetValue(fs2us(GetRelPath(fi2)), comment2);
      return MyStringCompareNoCase(comment1, comment2);
    }
    case kpidPrefix:
      if (fi1.Parent == fi2.Parent)
        return 0;
      if (fi1.Parent < 0) return -1;
      if (fi2.Parent < 0) return 1;
      return CompareFileNames_ForFolderList(
          Folders[fi1.Parent],
          Folders[fi2.Parent]);
    case kpidExtension:
      return CompareFileNames_ForFolderList(
          GetExtensionPtr(fi1.Name),
          GetExtensionPtr(fi2.Name));
  }
  
  return 0;
}

HRESULT CFSFolder::BindToFolderSpec(CFSTR name, IFolderFolder **resultFolder)
{
  *resultFolder = NULL;
  CFSFolder *folderSpec = new CFSFolder;
  CMyComPtr<IFolderFolder> subFolder = folderSpec;
  RINOK(folderSpec->Init(_path + name + FCHAR_PATH_SEPARATOR))
  *resultFolder = subFolder.Detach();
  return S_OK;
}

/*
void CFSFolder::GetPrefix(const CDirItem &item, FString &prefix) const
{
  if (item.Parent >= 0)
    prefix = Folders[item.Parent];
  else
    prefix.Empty();
}
*/

/*
void CFSFolder::GetPrefix(const CDirItem &item, FString &prefix) const
{
  int parent = item.Parent;

  unsigned len = 0;

  while (parent >= 0)
  {
    const CDirItem &cur = Files[parent];
    len += cur.Name.Len() + 1;
    parent = cur.Parent;
  }

  wchar_t *p = prefix.GetBuf_SetEnd(len) + len;
  parent = item.Parent;

  while (parent >= 0)
  {
    const CDirItem &cur = Files[parent];
    *(--p) = FCHAR_PATH_SEPARATOR;
    p -= cur.Name.Len();
    wmemcpy(p, cur.Name, cur.Name.Len());
    parent = cur.Parent;
  }
}
*/

FString CFSFolder::GetRelPath(const CDirItem &item) const
{
  if (item.Parent < 0)
    return item.Name;
  return Folders[item.Parent] + item.Name;
}

Z7_COM7F_IMF(CFSFolder::BindToFolder(UInt32 index, IFolderFolder **resultFolder))
{
  *resultFolder = NULL;
  const CDirItem &fi = Files[index];
  if (!fi.IsDir())
    return E_INVALIDARG;
  return BindToFolderSpec(GetRelPath(fi), resultFolder);
}

Z7_COM7F_IMF(CFSFolder::BindToFolder(const wchar_t *name, IFolderFolder **resultFolder))
{
  return BindToFolderSpec(us2fs(name), resultFolder);
}

Z7_COM7F_IMF(CFSFolder::BindToParentFolder(IFolderFolder **resultFolder))
{
  *resultFolder = NULL;
  /*
  if (_parentFolder)
  {
    CMyComPtr<IFolderFolder> parentFolder = _parentFolder;
    *resultFolder = parentFolder.Detach();
    return S_OK;
  }
  */
  if (_path.IsEmpty())
    return E_INVALIDARG;

  #if defined(UNDER_CE)

  #elif !defined(_WIN32)

  // [B.2 Linux port] The Windows branch (#else) handles drive letters: a drive
  // ROOT ("C:\") binds up to the CFSDrives "Computer" folder, and a drive PATH
  // binds up to its parent directory. Linux has a single unified tree rooted at
  // "/", with no drive-letter concept, so CFSDrives / IsDriveRootPath_SuperAllowed
  // / IsDrivePath_SuperAllowed / IsSuperPath (all _WIN32-only) do not apply. We
  // implement the faithful POSIX equivalent of "go up one directory":
  //   _path always ends in a separator (BindToFolderSpec / Init append one).
  //   * at the filesystem root "/" there is no parent -> return NULL folder
  //     (QtFolderModel::goParent treats a NULL parent as "already at top"),
  //   * otherwise strip the trailing separator and everything after the previous
  //     separator, then Init a new CFSFolder rooted at that parent prefix.
  {
    // strip the trailing separator
    int pos = _path.ReverseFind_PathSepar();
    if (pos != (int)_path.Len() - 1)
      return E_FAIL;
    if (pos <= 0)
      return S_OK; // _path == "/" : the root has no parent
    FString parentPath = _path.Left((unsigned)pos);
    pos = parentPath.ReverseFind_PathSepar();
    // keep the separator so the parent prefix also ends in one (root stays "/")
    parentPath.DeleteFrom((unsigned)(pos + 1));

    CFSFolder *parentFolderSpec = new CFSFolder;
    CMyComPtr<IFolderFolder> parentFolder = parentFolderSpec;
    if (parentFolderSpec->Init(parentPath) == S_OK)
    {
      *resultFolder = parentFolder.Detach();
      return S_OK;
    }
  }

  #else

  if (IsDriveRootPath_SuperAllowed(_path))
  {
    CFSDrives *drivesFolderSpec = new CFSDrives;
    CMyComPtr<IFolderFolder> drivesFolder = drivesFolderSpec;
    drivesFolderSpec->Init(false, IsSuperPath(_path));
    *resultFolder = drivesFolder.Detach();
    return S_OK;
  }

  int pos = _path.ReverseFind_PathSepar();
  if (pos < 0 || pos != (int)_path.Len() - 1)
    return E_FAIL;
  FString parentPath = _path.Left((unsigned)pos);
  pos = parentPath.ReverseFind_PathSepar();
  parentPath.DeleteFrom((unsigned)(pos + 1));

  if (NName::IsDrivePath_SuperAllowed(parentPath))
  {
    CFSFolder *parentFolderSpec = new CFSFolder;
    CMyComPtr<IFolderFolder> parentFolder = parentFolderSpec;
    if (parentFolderSpec->Init(parentPath) == S_OK)
    {
      *resultFolder = parentFolder.Detach();
      return S_OK;
    }
  }

  /*
  FString parentPathReduced = parentPath.Left(pos);

  pos = parentPathReduced.ReverseFind_PathSepar();
  if (pos == 1)
  {
    if (!IS_PATH_SEPAR_CHAR(parentPath[0]))
      return E_FAIL;
    CNetFolder *netFolderSpec = new CNetFolder;
    CMyComPtr<IFolderFolder> netFolder = netFolderSpec;
    netFolderSpec->Init(fs2us(parentPath));
    *resultFolder = netFolder.Detach();
    return S_OK;
  }
  */

  #endif

  return S_OK;
}

Z7_COM7F_IMF(CFSFolder::GetNumberOfProperties(UInt32 *numProperties))
{
  *numProperties = Z7_ARRAY_SIZE(kProps);
  if (!_flatMode)
    (*numProperties)--;
  return S_OK;
}

IMP_IFolderFolder_GetProp(CFSFolder::GetPropertyInfo, kProps)

Z7_COM7F_IMF(CFSFolder::GetFolderProperty(PROPID propID, PROPVARIANT *value))
{
  COM_TRY_BEGIN
  NWindows::NCOM::CPropVariant prop;
  switch (propID)
  {
    case kpidType: prop = "FSFolder"; break;
    case kpidPath: prop = fs2us(_path); break;
  }
  prop.Detach(value);
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CFSFolder::WasChanged(Int32 *wasChanged))
{
  bool wasChangedMain = false;

  #ifdef _WIN32

  for (;;)
  {
    if (!_findChangeNotification.IsHandleAllocated())
      break;
    DWORD waitResult = ::WaitForSingleObject(_findChangeNotification, 0);
    if (waitResult != WAIT_OBJECT_0)
      break;
    _findChangeNotification.FindNext();
    wasChangedMain = true;
  }

  #endif

  *wasChanged = BoolToInt(wasChangedMain);
  return S_OK;
}
 
Z7_COM7F_IMF(CFSFolder::Clone(IFolderFolder **resultFolder))
{
  CFSFolder *fsFolderSpec = new CFSFolder;
  CMyComPtr<IFolderFolder> folderNew = fsFolderSpec;
  fsFolderSpec->Init(_path);
  *resultFolder = folderNew.Detach();
  return S_OK;
}


/*
HRESULT CFSFolder::GetItemFullSize(unsigned index, UInt64 &size, IProgress *progress)
{
  if (index >= Files.Size())
  {
    size = Streams[index - Files.Size()].Size;
    return S_OK;
  }
  const CDirItem &fi = Files[index];
  if (fi.IsDir())
  {
    UInt64 numFolders = 0, numFiles = 0;
    size = 0;
    return GetFolderSize(_path + GetRelPath(fi), numFolders, numFiles, size, progress);
  }
  size = fi.Size;
  return S_OK;
}

Z7_COM7F_IMF(CFSFolder::GetItemFullSize(UInt32 index, PROPVARIANT *value, IProgress *progress)
{
  NCOM::CPropVariant prop;
  UInt64 size = 0;
  HRESULT result = GetItemFullSize(index, size, progress);
  prop = size;
  prop.Detach(value);
  return result;
}
*/

Z7_COM7F_IMF(CFSFolder::CalcItemFullSize(UInt32 index, IProgress *progress))
{
  if (index >= Files.Size())
    return S_OK;
  CDirItem &fi = Files[index];
  if (!fi.IsDir())
    return S_OK;
  CFsFolderStat stat(_path + GetRelPath(fi), progress);
  RINOK(stat.Enumerate())
  fi.Size = stat.Size;
  fi.NumFolders = stat.NumFolders;
  fi.NumFiles = stat.NumFiles;
  fi.FolderStat_Defined = true;
  return S_OK;
}

void CFSFolder::GetAbsPath(const wchar_t *name, FString &absPath)
{
  absPath.Empty();
  if (!IsAbsolutePath(name))
    absPath += _path;
  absPath += us2fs(name);
}

Z7_COM7F_IMF(CFSFolder::CreateFolder(const wchar_t *name, IProgress * /* progress */))
{
  FString absPath;
  GetAbsPath(name, absPath);
  if (CreateDir(absPath))
    return S_OK;
  if (::GetLastError() != ERROR_ALREADY_EXISTS)
    if (CreateComplexDir(absPath))
      return S_OK;
  return GetLastError_noZero_HRESULT();
}

Z7_COM7F_IMF(CFSFolder::CreateFile(const wchar_t *name, IProgress * /* progress */))
{
  FString absPath;
  GetAbsPath(name, absPath);
  NIO::COutFile outFile;
  if (!outFile.Create_NEW(absPath))
    return GetLastError_noZero_HRESULT();
  return S_OK;
}

Z7_COM7F_IMF(CFSFolder::Rename(UInt32 index, const wchar_t *newName, IProgress * /* progress */))
{
  if (index >= Files.Size())
    return E_NOTIMPL;
  const CDirItem &fi = Files[index];
  // FString prefix;
  // GetPrefix(fi, prefix);
  FString fullPrefix = _path;
  if (fi.Parent >= 0)
    fullPrefix += Folders[fi.Parent];
  if (!MyMoveFile(fullPrefix + fi.Name, fullPrefix + us2fs(newName)))
    return GetLastError_noZero_HRESULT();
  return S_OK;
}

Z7_COM7F_IMF(CFSFolder::Delete(const UInt32 *indices, UInt32 numItems,IProgress *progress))
{
  RINOK(progress->SetTotal(numItems))
  // int prevDeletedFileIndex = -1;
  for (UInt32 i = 0; i < numItems; i++)
  {
    // Sleep(200);
    UInt32 index = indices[i];
    bool result = true;
    /*
    if (index >= Files.Size())
    {
      const CAltStream &ss = Streams[index - Files.Size()];
      if (prevDeletedFileIndex != ss.Parent)
      {
        const CDirItem &fi = Files[ss.Parent];
        result = DeleteFileAlways(_path + GetRelPath(fi) + us2fs(ss.Name));
      }
    }
    else
    */
    {
      const CDirItem &fi = Files[index];
      const FString fullPath = _path + GetRelPath(fi);
      // prevDeletedFileIndex = index;
      if (fi.IsDir())
       #ifdef _WIN32
        result = RemoveDirWithSubItems(fullPath);
       #else
        // [B.4 Linux port] NDir::RemoveDirWithSubItems (recursive directory
        // removal) is _WIN32-only in the engine (Windows/FileDir.cpp builds it
        // inside #ifdef _WIN32; it uses the Win32 CEnumerator::Next(fi) +
        // HasReparsePoint). B.4a supplies the faithful POSIX equivalent
        // RemoveDirWithSubItems_Fs (NFsFolder, defined in FSFolderCopy.cpp): same
        // logic (enumerate + recurse + unlink + rmdir) using the two-phase Linux
        // CEnumerator. The plain-file branch below stays portable (DeleteFileAlways).
        result = NFsFolder::RemoveDirWithSubItems_Fs(fullPath);
       #endif
      else
        result = DeleteFileAlways(fullPath);
    }
    if (!result)
      return GetLastError_noZero_HRESULT();
    const UInt64 completed = i;
    RINOK(progress->SetCompleted(&completed))
  }
  return S_OK;
}

Z7_COM7F_IMF(CFSFolder::SetProperty(UInt32 index, PROPID propID,
    const PROPVARIANT *value, IProgress * /* progress */))
{
  if (index >= Files.Size())
    return E_INVALIDARG;
  CDirItem &fi = Files[index];
  if (fi.Parent >= 0)
    return E_NOTIMPL;
  switch (propID)
  {
    case kpidComment:
    {
      UString filename = fs2us(fi.Name);
      filename.Trim();
      if (value->vt == VT_EMPTY)
        _comments.DeletePair(filename);
      else if (value->vt == VT_BSTR)
      {
        CTextPair pair;
        pair.ID = filename;
        pair.ID.Trim();
        pair.Value.SetFromBstr(value->bstrVal);
        pair.Value.Trim();
        if (pair.Value.IsEmpty())
          _comments.DeletePair(filename);
        else
          _comments.AddPair(pair);
      }
      else
        return E_INVALIDARG;
      SaveComments();
      break;
    }
    default:
      return E_NOTIMPL;
  }
  return S_OK;
}

Z7_COM7F_IMF(CFSFolder::GetSystemIconIndex(UInt32 index, Int32 *iconIndex))
{
  *iconIndex = -1;
  if (index >= Files.Size())
    return E_INVALIDARG;
  #ifdef _WIN32
  const CDirItem &fi = Files[index];
  return Shell_GetFileInfo_SysIconIndex_for_Path_return_HRESULT(
      _path + GetRelPath(fi), fi.Attrib, iconIndex);
  #else
  // [B.2 Linux port] Shell_GetFileInfo_SysIconIndex_* lives in SysIconUtils.cpp,
  // which is a Win32 Shell/common-controls (HIMAGELIST / SHGetFileInfo) unit not
  // built on Linux. The Qt model provides its own theme icons (QIcon::fromTheme),
  // so this engine-side system-image-list index is unused; report "no index"
  // (-1), which is exactly what the Windows path yields when the shell lookup
  // fails. Windows branch (#if) is verbatim-original.
  return S_OK;
  #endif
}

Z7_COM7F_IMF(CFSFolder::SetFlatMode(Int32 flatMode))
{
  _flatMode = IntToBool(flatMode);
  return S_OK;
}

/*
Z7_COM7F_IMF(CFSFolder::SetShowNtfsStreamsMode(Int32 showStreamsMode)
{
  _scanAltStreams = IntToBool(showStreamsMode);
  return S_OK;
}
*/

}
