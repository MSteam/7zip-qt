// main_progress_demo.cpp
//
// Test harness for milestone C.1a: proves the threaded modal-progress core
// (QtProgressThreadVirt + QtProgressDialog + the verbatim CProgressSync) in
// isolation, driven by a fake worker (no engine).
//
// The worker's ProcessVirt() runs on the worker thread and ONLY touches Sync()
// (the internally-locked CProgressSync), exactly like a real 7-Zip callback
// would. It polls Sync().CheckStop() each step and returns E_ABORT on cancel.

// MyInitGuid.h must be included in exactly ONE TU per executable: with INITGUID
// active, the Z7_DEFINE_GUID macros in the COM interface headers below switch
// from "declare extern" to "define" mode, emitting the IID_* GUID symbols the
// engine archive references (IID_IUnknown, IID_ICompressCoder, IID_IInArchive,
// ...). This is the exact pattern the 7-Zip console (Console/Main.cpp) and the
// Win32 GUI (GUI/GUI.cpp) front-ends use; they pull the interface headers in
// transitively. We include them explicitly here.
#include "../../../Common/MyInitGuid.h"

#include "../../ICoder.h"     // ICompress*/ICrypto* IIDs (+ IStream.h)
#include "../../IPassword.h"  // ICryptoGetTextPassword* IIDs
#include "../../IProgress.h"  // IProgress IID
#include "../../Archive/IArchive.h"      // IInArchive / IOutArchive IIDs
#include "../Common/IFileExtractCallback.h" // IFolder*ExtractCallback / IGetProp IIDs

#include <QtWidgets/QApplication>

#include <cstdio>

#include <QtCore/QThread>

#include "QtProgressThreadVirt.h"

class CDemoThread : public QtProgressThreadVirt
{
  HRESULT ProcessVirt() override;
};

HRESULT CDemoThread::ProcessVirt()
{
  const UInt64 kNumFiles = 20;
  const UInt64 kTotalBytes = 64 * 1024 * 1024; // 64 MiB of "work"
  const UInt64 kStep = kTotalBytes / 200;

  // Mirror a real callback's setup: announce totals up front.
  RINOK(Sync().Set_NumFilesTotal(kNumFiles))
  Sync().Set_NumBytesTotal(kTotalBytes);
  Sync().Set_Status(UString(L"Testing"));

  UInt64 completed = 0;
  UInt64 files = 0;
  bool addedMessage = false;

  for (;;)
  {
    // Poll for cancel/pause exactly like the engine's progress callbacks do.
    RINOK(Sync().CheckStop())

    if (completed >= kTotalBytes)
      break;

    completed += kStep;
    if (completed > kTotalBytes)
      completed = kTotalBytes;

    files = (completed * kNumFiles) / kTotalBytes;

    RINOK(Sync().Set_NumBytesCur(completed))
    Sync().Set_NumFilesCur(files);

    {
      UString path = L"test_folder/file_";
      path.Add_UInt64(files);
      path += L".dat";
      Sync().Set_FilePath(path.Ptr());
    }

    // Prove the message list works: add one error/info message mid-run.
    if (!addedMessage && completed >= kTotalBytes / 2)
    {
      Sync().AddError_Message(L"demo message");
      addedMessage = true;
    }

    QThread::msleep(30);
  }

  return S_OK;
}

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);

  CDemoThread worker;

  // Headless smoke test: when there is no interactive display (offscreen / no
  // DISPLAY), run non-interactively so the dialog auto-closes at the end instead
  // of waiting for a human to click Close. This mirrors the original
  // g_DisableUserQuestions used by 7-Zip's non-interactive code paths.
  {
    const QByteArray qpa = qgetenv("QT_QPA_PLATFORM");
    if (qpa.contains("offscreen") || qpa.contains("minimal") ||
        (qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty()))
      worker.DisableUserQuestions = true;
  }

  const HRESULT res = worker.Create(UString(L"Demo Extracting"));

  std::printf("ProcessVirt returned HRESULT = 0x%08X\n", (unsigned)res);
  std::fflush(stdout);

  return (res == S_OK) ? 0 : 1;
}
