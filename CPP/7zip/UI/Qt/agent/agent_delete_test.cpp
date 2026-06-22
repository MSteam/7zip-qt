// agent_delete_test.cpp
// ----------------------------------------------------------------------------
// Milestone B.5a : headless proof harness for the 7-Zip Agent / IFolderFolder
// archive-WRITE seam on Linux (in-place archive modification).
//
// Given an archive path (argv[1]), a root-folder item index to delete (argv[2])
// and an optional password (argv[3]), it:
//   1. opens the archive through CAgent exactly like agent_browse_test.cpp
//      (CInFileStream + minimal IArchiveOpenCallback),
//   2. gets the root IFolderFolder via CAgent::BindToRootFolder and QIs it to
//      IFolderOperations (the same QI the FileManager's GetFolderOperations does),
//   3. prints the root listing with indices,
//   4. calls IFolderOperations::Delete({index}, 1, NULL) - this drives
//      CAgentFolder::Delete -> CommonUpdateOperation, which rebuilds the archive
//      in a CWorkDirTempFile and MoveToOriginal()s it over the original file.
//      A NULL IProgress is valid: CommonUpdateOperation guards every progress
//      use with `if (progress)`.
// The caller then runs `7zz l <archive>` to confirm the entry is gone and
// `7zz t <archive>` to confirm the rewritten archive is still valid.
//
// SINGLE GUID-emitting TU (MyInitGuid.h), no Qt. Links sevenzip_agent +
// sevenzip_engine (WHOLE_ARCHIVE). Mirrors agent_browse_test.cpp's scaffolding.
// ----------------------------------------------------------------------------

#include "../../Agent/StdAfx.h"

#include "AgentLinuxCompat.h"

// The one place GUIDs are instantiated for the whole executable.
#include "../../../../Common/MyInitGuid.h"

#include <cstdio>
#include <cstdlib>

#include "../../../../Common/MyString.h"
#include "../../../../Common/StringConvert.h"
#include "../../../../Windows/PropVariant.h"

#include "../../../Common/FileStreams.h"
#include "../../../PropID.h"
#include "../../../IPassword.h"
#include "../../../IProgress.h"

#include "../../FileManager/IFolder.h"   // IFolderOperations / IID_IFolderOperations
#include "../../Agent/IFolderArchive.h"  // IFolderArchiveUpdateCallback

#include "../../Agent/Agent.h"

using namespace NWindows;

// --- minimal open callback : supplies the password headlessly ---------------
// Identical to agent_browse_test.cpp's COpenCallbackTest.
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
    return StringToBstr(UString(), password);
  return StringToBstr(Password, password);
}

// --- minimal archive-update callback ----------------------------------------
// CAgent::DeleteItems calls updateCallback100->DeleteOperation(name)
// UNCONDITIONALLY, where updateCallback100 is the result of
// progress->QueryInterface(IID_IFolderArchiveUpdateCallback). So the IProgress
// passed to IFolderOperations::Delete MUST also implement
// IFolderArchiveUpdateCallback (passing a NULL or IProgress-only object
// NULL-derefs). This mirrors the Win32 FileManager, whose progress callback
// implements IFolderArchiveUpdateCallback. NOTE for B.5b: the Qt FM's
// archive-delete callback must do the same.
class CUpdateCallbackTest Z7_final:
  public IFolderArchiveUpdateCallback,
  public CMyUnknownImp
{
  Z7_COM_QI_BEGIN2(IProgress)
    Z7_COM_QI_ENTRY(IFolderArchiveUpdateCallback)
  Z7_COM_QI_END
  Z7_COM_ADDREF_RELEASE

  Z7_IFACE_COM7_IMP(IProgress)
  Z7_IFACE_COM7_IMP(IFolderArchiveUpdateCallback)
};

Z7_COM7F_IMF(CUpdateCallbackTest::SetTotal(UInt64 /* total */)) { return S_OK; }
Z7_COM7F_IMF(CUpdateCallbackTest::SetCompleted(const UInt64 * /* v */)) { return S_OK; }
Z7_COM7F_IMF(CUpdateCallbackTest::CompressOperation(const wchar_t *name))
  { printf("  compress: %s\n", (const char *)GetOemString(UString(name))); return S_OK; }
Z7_COM7F_IMF(CUpdateCallbackTest::DeleteOperation(const wchar_t *name))
  { printf("  delete-op: %s\n", (const char *)GetOemString(UString(name))); return S_OK; }
Z7_COM7F_IMF(CUpdateCallbackTest::OperationResult(Int32 /* opRes */)) { return S_OK; }
Z7_COM7F_IMF(CUpdateCallbackTest::UpdateErrorMessage(const wchar_t *message))
  { printf("  error: %s\n", (const char *)GetOemString(UString(message))); return S_OK; }
Z7_COM7F_IMF(CUpdateCallbackTest::SetNumFiles(UInt64 /* numFiles */)) { return S_OK; }

static UString Prop_GetString(IFolderFolder *folder, UInt32 index, PROPID propID)
{
  NCOM::CPropVariant prop;
  if (folder->GetProperty(index, propID, &prop) != S_OK)
    return UString();
  if (prop.vt == VT_BSTR)
    return UString(prop.bstrVal);
  return UString();
}

int main(int argc, char **argv)
{
  setbuf(stdout, NULL); // unbuffered: don't lose output if a call crashes

  if (argc < 3)
  {
    fprintf(stderr, "usage: %s <archive> <rootIndexToDelete | +newFolderName> [password]\n", argv[0]);
    return 2;
  }

  const UString arcPath = GetUnicodeString(argv[1]);
  // "+name" -> exercise CAgentFolder::CreateFolder (validates the di.SetAsDir /
  // GetCurUtc_FiTime [B.5 Linux port] guards); a number -> delete that index.
  const bool createMode = (argv[2][0] == '+');
  const UInt32 delIndex = createMode ? 0 : (UInt32)atoi(argv[2]);

  CInFileStream *fileSpec = new CInFileStream;
  CMyComPtr<IInStream> fileStream = fileSpec;
  if (!fileSpec->Open(us2fs(arcPath)))
  {
    fprintf(stderr, "cannot open file: %s\n", argv[1]);
    return 1;
  }

  COpenCallbackTest *cbSpec = new COpenCallbackTest;
  CMyComPtr<IArchiveOpenCallback> openCallback = cbSpec;
  if (argc >= 4)
  {
    cbSpec->PasswordIsDefined = true;
    cbSpec->Password = GetUnicodeString(argv[3]);
  }

  CAgent *agentSpec = new CAgent;
  CMyComPtr<IInFolderArchive> agent = agentSpec;

  CMyComBSTR archiveType;
  HRESULT res = agent->Open(fileStream, arcPath, L"", &archiveType, openCallback);
  if (res != S_OK)
  {
    fprintf(stderr, "Open failed: 0x%08X\n", (unsigned)res);
    return 1;
  }
  if (archiveType)
    printf("archive type: %s\n", (const char *)GetOemString(UString(archiveType)));

  CMyComPtr<IFolderFolder> rootFolder;
  res = agent->BindToRootFolder(&rootFolder);
  if (res != S_OK || !rootFolder)
  {
    fprintf(stderr, "BindToRootFolder failed: 0x%08X\n", (unsigned)res);
    return 1;
  }

  // List the root folder with indices, so the caller can see what index maps to.
  {
    UInt32 numItems = 0;
    rootFolder->GetNumberOfItems(&numItems);
    printf("---- root listing (%u items) ----\n", (unsigned)numItems);
    for (UInt32 i = 0; i < numItems; i++)
      printf("  [%u] %s\n", (unsigned)i,
          (const char *)GetOemString(Prop_GetString(rootFolder, i, kpidName)));
  }

  // QI the root CAgentFolder to IFolderOperations - exactly like the Qt FM's
  // GetFolderOperations(panel->currentFolder(), ...).
  CMyComPtr<IFolderOperations> ops;
  rootFolder->QueryInterface(IID_IFolderOperations, (void **)&ops);
  if (!ops)
  {
    fprintf(stderr, "QI IFolderOperations failed\n");
    return 1;
  }

  CUpdateCallbackTest *upSpec = new CUpdateCallbackTest;
  CMyComPtr<IProgress> progress = upSpec;

  if (createMode)
  {
    const UString newName = GetUnicodeString(argv[2] + 1);
    printf("---- creating folder \"%s\" ----\n", (const char *)GetOemString(newName));
    res = ops->CreateFolder(newName, progress);
    printf("CreateFolder -> 0x%08X\n", (unsigned)res);
  }
  else
  {
    const UString delName = Prop_GetString(rootFolder, delIndex, kpidName);
    printf("---- deleting root index %u (%s) ----\n",
        (unsigned)delIndex, (const char *)GetOemString(delName));
    const UInt32 indices[1] = { delIndex };
    res = ops->Delete(indices, 1, progress);
    printf("Delete -> 0x%08X\n", (unsigned)res);
  }

  agent->Close();
  return (res == S_OK) ? 0 : 1;
}
