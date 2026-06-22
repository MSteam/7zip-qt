// agent_browse_test.cpp
// ----------------------------------------------------------------------------
// Milestone B.0 : headless proof harness for the 7-Zip Agent / IFolderFolder
// archive-browsing seam on Linux.
//
// Given an archive path (argv[1]) and an optional password (argv[2]), it:
//   1. creates a CAgent and opens the archive through an IInStream
//      (CInFileStream), exactly as the FileManager's CArchiveFolderManager
//      does, with a minimal IArchiveOpenCallback that supplies the password
//      (proving the headless open-callback path),
//   2. gets the root IFolderFolder via CAgent::BindToRootFolder,
//   3. enumerates items (GetNumberOfItems + GetProperty kpidName/kpidSize/
//      kpidIsDir) and prints name / size / dir-flag,
//   4. recurses into every subfolder via IFolderFolder::BindToFolder (proving
//      real IFolderFolder navigation, not a flat list), printing a full
//      recursive listing.
//
// This is the SINGLE GUID-emitting TU (MyInitGuid.h), like 7zz's MainAr.cpp /
// 7zqt's main_7zqt.cpp. No Qt. Links sevenzip_agent + sevenzip_engine
// (WHOLE_ARCHIVE so the codec/format registrations survive).
// ----------------------------------------------------------------------------

#include "../../Agent/StdAfx.h"

// Force-included on the command line too, but include explicitly so the TU is
// self-describing.
#include "AgentLinuxCompat.h"

// The one place GUIDs are instantiated for the whole executable.
#include "../../../../Common/MyInitGuid.h"

#include <cstdio>

#include "../../../../Common/MyString.h"
#include "../../../../Common/StringConvert.h"
#include "../../../../Windows/PropVariant.h"
#include "../../../../Windows/PropVariantConv.h"

#include "../../../Common/FileStreams.h"
#include "../../../PropID.h"
#include "../../../IPassword.h"

#include "../../Agent/Agent.h"

using namespace NWindows;

// --- minimal open callback : supplies the password headlessly ---------------
// Mirrors the relevant slice of UI/Common/ArchiveOpenCallback.h's
// COpenCallbackImp: IArchiveOpenCallback (progress, ignored here) +
// ICryptoGetTextPassword (returns the user password for encrypted headers).
class COpenCallbackTest Z7_final:
  public IArchiveOpenCallback,
  public ICryptoGetTextPassword,
  public CMyUnknownImp
{
  Z7_COM_QI_BEGIN2(IArchiveOpenCallback)
    Z7_COM_QI_ENTRY(ICryptoGetTextPassword)
  Z7_COM_QI_END
  Z7_COM_ADDREF_RELEASE

  Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
  Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
public:
  bool PasswordIsDefined;
  UString Password;
  COpenCallbackTest(): PasswordIsDefined(false) {}
};

Z7_COM7F_IMF(COpenCallbackTest::SetTotal(const UInt64 * /* files */, const UInt64 * /* bytes */))
  { return S_OK; }
Z7_COM7F_IMF(COpenCallbackTest::SetCompleted(const UInt64 * /* files */, const UInt64 * /* bytes */))
  { return S_OK; }

Z7_COM7F_IMF(COpenCallbackTest::CryptoGetTextPassword(BSTR *password))
{
  if (!PasswordIsDefined)
  {
    // No password supplied: return an empty string (the engine will then fail
    // to open an encrypted-header archive, which the caller can observe).
    return StringToBstr(UString(), password);
  }
  return StringToBstr(Password, password);
}

// --- property helpers --------------------------------------------------------
static void PrintIndent(unsigned depth)
{
  for (unsigned i = 0; i < depth; i++)
    printf("  ");
}

static UString Prop_GetString(IFolderFolder *folder, UInt32 index, PROPID propID)
{
  NCOM::CPropVariant prop;
  if (folder->GetProperty(index, propID, &prop) != S_OK)
    return UString();
  if (prop.vt == VT_BSTR)
    return UString(prop.bstrVal);
  return UString();
}

static bool Prop_GetBool(IFolderFolder *folder, UInt32 index, PROPID propID)
{
  NCOM::CPropVariant prop;
  if (folder->GetProperty(index, propID, &prop) != S_OK)
    return false;
  return (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE);
}

static bool Prop_GetUInt64(IFolderFolder *folder, UInt32 index, PROPID propID, UInt64 &v)
{
  NCOM::CPropVariant prop;
  if (folder->GetProperty(index, propID, &prop) != S_OK)
    return false;
  return ConvertPropVariantToUInt64(prop, v);
}

// --- recursive IFolderFolder walk -------------------------------------------
static unsigned g_FileCount = 0;
static unsigned g_DirCount = 0;
static UInt64   g_TotalSize = 0;

static HRESULT WalkFolder(IFolderFolder *folder, const UString &prefix, unsigned depth)
{
  UInt32 numItems = 0;
  RINOK(folder->GetNumberOfItems(&numItems))

  for (UInt32 i = 0; i < numItems; i++)
  {
    const UString name = Prop_GetString(folder, i, kpidName);
    const bool isDir = Prop_GetBool(folder, i, kpidIsDir);
    UInt64 size = 0;
    const bool hasSize = Prop_GetUInt64(folder, i, kpidSize, size);

    const UString full = prefix + name;

    PrintIndent(depth);
    if (isDir)
    {
      g_DirCount++;
      printf("[DIR ] %s/\n", (const char *)GetOemString(full));
    }
    else
    {
      g_FileCount++;
      if (hasSize)
        g_TotalSize += size;
      printf("[FILE] %s    size=%llu\n",
          (const char *)GetOemString(full),
          hasSize ? (unsigned long long)size : 0ULL);
    }

    if (isDir)
    {
      // Prove IFolderFolder navigation: bind into the subfolder and recurse.
      CMyComPtr<IFolderFolder> sub;
      const HRESULT res = folder->BindToFolder(i, &sub);
      if (res == S_OK && sub)
        RINOK(WalkFolder(sub, full + L"/", depth + 1))
      else
        printf("    (BindToFolder failed: 0x%08X)\n", (unsigned)res);
    }
  }
  return S_OK;
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: %s <archive> [password]\n", argv[0]);
    return 2;
  }

  const UString arcPath = GetUnicodeString(argv[1]);

  // 1. Open the archive file as an IInStream (CInFileStream), exactly like the
  //    FileManager path that passes a stream into CAgent::Open.
  CInFileStream *fileSpec = new CInFileStream;
  CMyComPtr<IInStream> fileStream = fileSpec;
  if (!fileSpec->Open(us2fs(arcPath)))
  {
    fprintf(stderr, "cannot open file: %s\n", argv[1]);
    return 1;
  }

  COpenCallbackTest *cbSpec = new COpenCallbackTest;
  CMyComPtr<IArchiveOpenCallback> openCallback = cbSpec;
  if (argc >= 3)
  {
    cbSpec->PasswordIsDefined = true;
    cbSpec->Password = GetUnicodeString(argv[2]);
  }

  // 2. Create the CAgent and open. (CAgent::Open mirrors how
  //    CArchiveFolderManager::OpenFolderFile drives it.)
  CAgent *agentSpec = new CAgent;
  CMyComPtr<IInFolderArchive> agent = agentSpec;

  // arcFormat must be a (possibly empty) string, NOT NULL: CAgent::Open feeds
  // it to ParseOpenTypes(const UString&), which would construct UString(NULL)
  // and crash. An empty string means "auto-detect the format" (the default the
  // FileManager / console front ends use).
  CMyComBSTR archiveType;
  HRESULT res = agent->Open(fileStream, arcPath, L"", &archiveType, openCallback);
  if (res != S_OK)
  {
    fprintf(stderr, "Open failed: 0x%08X\n", (unsigned)res);
    if (!agentSpec->_archiveLink.NonOpen_ErrorInfo.IsThereErrorOrWarning())
      return 1;
    fprintf(stderr, "(continuing: NonOpen error info present)\n");
  }

  if (archiveType)
    printf("archive type: %s\n", (const char *)GetOemString(UString(archiveType)));

  // 3. Get the root IFolderFolder.
  CMyComPtr<IFolderFolder> rootFolder;
  res = agent->BindToRootFolder(&rootFolder);
  if (res != S_OK || !rootFolder)
  {
    fprintf(stderr, "BindToRootFolder failed: 0x%08X\n", (unsigned)res);
    return 1;
  }

  // 4. Recursive listing via IFolderFolder + BindToFolder navigation.
  printf("---- agent recursive listing ----\n");
  res = WalkFolder(rootFolder, UString(), 0);
  if (res != S_OK)
  {
    fprintf(stderr, "WalkFolder failed: 0x%08X\n", (unsigned)res);
    return 1;
  }

  printf("---- summary ----\n");
  printf("files=%u dirs=%u total_size=%llu\n",
      g_FileCount, g_DirCount, (unsigned long long)g_TotalSize);

  agent->Close();
  return 0;
}
