// main_7zqt.cpp
//
// Milestone C.5 — the UNIFIED Qt/Linux GUI entry point, mirroring 7zG.exe's
// GUI.cpp (Main2() + the WinMain try/catch wrapper). It replaces the four ad-hoc
// per-flow test mains (main_extract / main_compress / main_hash) with ONE binary
// that uses the REAL 7-Zip command-line parser (CArcCmdLineParser /
// CArcCmdLineOptions) and the REAL command dispatch, then routes each command
// group to the existing Qt flow (QtExtractGUI / QtUpdateGUI / QtHashGUI) instead
// of the Win32 ExtractGUI / UpdateGUI / HashCalcGUI.
//
// This TU is the SINGLE GUID-emitting TU for the 7zqt binary: it includes
// MyInitGuid.h plus the engine interface headers, so the IID_* symbols the engine
// archive references are DEFINED here (exactly the pattern the per-flow mains
// used; consolidated here). No other TU in 7zqt may include MyInitGuid.h.
//
// FIDELITY: the body of Run7zqt() below tracks GUI.cpp Main2() line-for-line; the
// catch ladder in main() tracks GUI.cpp's WinMain catch ladder. The Win32-only
// bits (MessageBoxW "Specify command", OleInitialize, NT_CHECK, comctl32) are
// dropped or replaced with their Linux equivalents (stderr / QApplication); the
// command dispatch and option mapping are preserved exactly.

#include "../../../Common/MyInitGuid.h"

#include "../../ICoder.h"      // ICompress*/ICrypto* IIDs (+ IStream.h)
#include "../../IPassword.h"   // ICryptoGetTextPassword* IIDs
#include "../../IProgress.h"   // IProgress IID
#include "../../Archive/IArchive.h"          // IInArchive / IOutArchive IIDs
#include "../Common/IFileExtractCallback.h"  // IFolder*ExtractCallback / IGetProp IIDs

#include <QtWidgets/QApplication>
#include <QtCore/QCoreApplication>   // P.2 : Lang dir
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>          // P.2 : [Options] Lang
#include <QtCore/QString>

#include "QtLang.h"                  // P.2 : QtLang_LoadFile (translate the dialogs)
#include "../GUI/ExtractRes.h"       // G.1l : IDS_MEM_ERROR / IDS_UNSUPPORTED_ARCHIVE_TYPE / IDS_UPDATE_NOT_SUPPORTED

#include <cstdio>

#include "../../../Common/MyException.h"
#include "../../../Common/NewHandler.h" // CNewException
#include "../../../Common/StringConvert.h"

#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileName.h"

#include "../Common/ArchiveCommandLine.h"
#include "../Common/EnumDirItems.h"
#include "../Common/ExitCode.h"
#include "../Common/LoadCodecs.h"
#include "../Common/OpenArchive.h"   // ParseOpenTypes

#include "QtExtractCallback.h"
#include "QtExtractGUI.h"
#include "QtExtractPrompts.h"
#include "QtMemDialog.h"        // G.8a : decompression memory-limit request prompt
#include "QtUpdateCallback.h"
#include "QtCompressGUI.h"
#include "QtHashGUI.h"
#include "fm/QtBenchmarkDialog.h"   // G.10a : Benchmark dialog + RunBenchmarkConsole + k_NumBenchIterations_Default

using namespace NWindows;
using namespace NFile;
using namespace NDir;


#ifdef Z7_EXTERNAL_CODECS
extern
const CExternalCodecs *g_ExternalCodecs_Ptr;
const CExternalCodecs *g_ExternalCodecs_Ptr;
#endif

// Mirror of GUI.cpp's global g_DisableUserQuestions (set from options.YesToAll).
// In the Qt port the equivalent state is threaded into each flow as the
// `disableUserQuestions` argument; we keep the global too so the semantics read
// like the original and any shared engine path that consults it still works.
bool g_DisableUserQuestions = false;


// Detect a headless / offscreen environment (no usable display). Mirrors the
// per-flow mains' DetectHeadless(): QT_QPA_PLATFORM=offscreen/minimal, or no
// DISPLAY / WAYLAND_DISPLAY. When headless we never block on a dialog.
static bool DetectHeadless()
{
  const QByteArray qpa = qgetenv("QT_QPA_PLATFORM");
  if (qpa.contains("offscreen") || qpa.contains("minimal"))
    return true;
  return qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty();
}


static const char * const kNoFormats = "7-Zip cannot find the code that works with archives.";

// === error reporting (mirror GUI.cpp ErrorMessage / Show*ErrorMessage) =======
// On Linux there is no MessageBoxW; under -y / headless the original suppresses
// the box entirely (if (!g_DisableUserQuestions)). We print to stderr always so
// the test harness sees the message; this is the faithful "no modal box" path.
static void ErrorMessage(const wchar_t *message)
{
  const AString a = UnicodeStringToMultiByte(UString(message), CP_UTF8);
  std::fprintf(stderr, "7-Zip: %s\n", a.Ptr());
}

static void ErrorMessage(const char *s)
{
  std::fprintf(stderr, "7-Zip: %s\n", s);
}

static int ShowMemErrorMessage()
{
  // G.1l : original GUI.cpp:119 ErrorLangMessage(IDS_MEM_ERROR) [=3000, ExtractRes.h].
  // The inline English is the port's existing fallback literal (kept verbatim); the
  // numeric id is what a loaded Lang/*.txt translates.
  ErrorMessage(FmLang(IDS_MEM_ERROR,
      QStringLiteral("ERROR: Can't allocate required memory!")).toUtf8().constData());
  return NExitCode::kMemoryError;
}

static int ShowSysErrorMessage(HRESULT errorCode)
{
  if (errorCode == E_OUTOFMEMORY)
    return ShowMemErrorMessage();
  ErrorMessage(HResultToMessage(errorCode));
  return NExitCode::kFatalError;
}

static void ThrowException_if_Error(HRESULT res)
{
  if (res != S_OK)
    throw CSystemException(res);
}


// === Run7zqt() : faithful port of GUI.cpp Main2() ============================
// `commandStrings` is the switch/argument vector (argv[1..], like Main2() after
// it deletes argv[0]). `headless` drives the dialog-show decision.
static int Run7zqt(const UStringVector &commandStrings, bool headless)
{
  if (commandStrings.Size() == 0)
  {
    // GUI.cpp: MessageBoxW(NULL, L"Specify command", ...); return 0;
    ErrorMessage("Specify command");
    return 0;
  }

  CArcCmdLineOptions options;
  CArcCmdLineParser parser;

  parser.Parse1(commandStrings, options);
  g_DisableUserQuestions = options.YesToAll;
  parser.Parse2(options);

  // Headless / offscreen runs are non-interactive too: there is no display to
  // show a dialog on, so we suppress user questions exactly as -y does. This is
  // the unified-binary analogue of the per-flow mains' headless auto-accept.
  const bool disableUserQuestions = options.YesToAll || headless;

  CREATE_CODECS_OBJECT

  codecs->CaseSensitive_Change = options.CaseSensitive_Change;
  codecs->CaseSensitive = options.CaseSensitive;
  ThrowException_if_Error(codecs->Load());
  Codecs_AddHashArcHandler(codecs);

  #ifdef Z7_EXTERNAL_CODECS
  {
    g_ExternalCodecs_Ptr = &_externalCodecs;
    UString s;
    codecs->GetCodecsErrorMessage(s);
    if (!s.IsEmpty())
      ErrorMessage(s.Ptr());
  }
  #endif

  const bool isExtractGroupCommand = options.Command.IsFromExtractGroup();

  if (codecs->Formats.Size() == 0 &&
        (isExtractGroupCommand
        || options.Command.IsFromUpdateGroup()))
  {
    #ifdef Z7_EXTERNAL_CODECS
    if (!codecs->MainDll_ErrorPath.IsEmpty())
    {
      UString s ("7-Zip cannot load module: ");
      s += fs2us(codecs->MainDll_ErrorPath);
      throw s;
    }
    #endif
    throw kNoFormats;
  }

  CObjectVector<COpenType> formatIndices;
  if (!ParseOpenTypes(*codecs, options.ArcType, formatIndices))
  {
    // G.1l : original GUI.cpp:201/212 ErrorLangMessage(IDS_UNSUPPORTED_ARCHIVE_TYPE)
    // [=3007, ExtractRes.h]. Inline English kept verbatim as the fallback literal.
    ErrorMessage(FmLang(IDS_UNSUPPORTED_ARCHIVE_TYPE,
        QStringLiteral("Unsupported archive type")).toUtf8().constData());
    return NExitCode::kFatalError;
  }

  CIntVector excludedFormats;
  FOR_VECTOR (k, options.ExcludedArcTypes)
  {
    CIntVector tempIndices;
    if (!codecs->FindFormatForArchiveType(options.ExcludedArcTypes[k], tempIndices)
        || tempIndices.Size() != 1)
    {
      // G.1l : original GUI.cpp:212 ErrorLangMessage(IDS_UNSUPPORTED_ARCHIVE_TYPE)
      // [=3007, ExtractRes.h]. Inline English kept verbatim as the fallback literal.
      ErrorMessage(FmLang(IDS_UNSUPPORTED_ARCHIVE_TYPE,
          QStringLiteral("Unsupported archive type")).toUtf8().constData());
      return NExitCode::kFatalError;
    }
    excludedFormats.AddToUniqueSorted(tempIndices[0]);
  }

  #ifdef Z7_EXTERNAL_CODECS
  if (isExtractGroupCommand
      || options.Command.IsFromUpdateGroup()
      || options.Command.CommandType == NCommandType::kHash
      || options.Command.CommandType == NCommandType::kBenchmark)
    ThrowException_if_Error(_externalCodecs.Load());
  #endif

  if (options.Command.CommandType == NCommandType::kBenchmark)
  {
    // GUI.cpp Main2() kBenchmark : Benchmark(EXTERNAL_CODECS_VARS_L
    //   options.Properties, options.NumIterations_Defined ? options.NumIterations
    //   : k_NumBenchIterations_Default); ThrowException_if_Error(res).
    const UInt32 numIterations = options.NumIterations_Defined ?
        options.NumIterations : k_NumBenchIterations_Default;

    if (headless)
    {
      // No display : drive the engine Bench() in TotalMode and print the textual
      // report to stdout — the faithful BenchCon.cpp analogue (`7zz b`).
      const HRESULT res = RunBenchmarkConsole(
          EXTERNAL_CODECS_VARS_L
          options.Properties, numIterations, stdout);
      std::fflush(stdout);
      ThrowException_if_Error(res);
    }
    else
    {
      // Interactive : show the live Benchmark dialog (GUI.cpp opens BenchmarkDialog).
      // Total mode is requested via the "m=*" benchmark property (Benchmark() parses it).
      const HRESULT res = QtBenchmarkDialog::Benchmark(
          options.Properties, numIterations, /*totalMode*/ false, nullptr);
      ThrowException_if_Error(res);
    }
  }
  else if (isExtractGroupCommand)
  {
    // ---- extract / test group : mirror GUI.cpp Main2() extract branch -------
    UStringVector ArchivePathsSorted;
    UStringVector ArchivePathsFullSorted;

    // The Qt extract callback is the QtExtractCallback (== CExtractCallbackImp).
    // It is COM-owned via a CMyComPtr (Release()==delete this).
    QtExtractCallback *ecs = new QtExtractCallback;
    CMyComPtr<IFolderArchiveExtractCallback> extractCallback = ecs;

    #ifndef Z7_NO_CRYPTO
    ecs->PasswordIsDefined = options.PasswordEnabled;
    ecs->Password = options.Password;
    #endif

    ecs->Init();

    CExtractOptions eo;
    (CExtractOptionsBase &)eo = options.ExtractOptions;
    eo.StdInMode = options.StdInMode;
    eo.StdOutMode = options.StdOutMode;
    eo.YesToAll = options.YesToAll;
    ecs->YesToAll = options.YesToAll;
    eo.TestMode = options.Command.IsTestCommand();
    ecs->TestMode = eo.TestMode;

    eo.Properties = options.Properties;

    // NOTE: the original optionally builds a CHashBundle when -scrc... is given
    // (hash-during-extract). The Qt extract worker passes NULL for IHashCalc, so
    // that sub-feature is out of scope here (DEFERRED, like the original's #ifndef
    // Z7_SFX hb block when no hash methods were requested).

    {
      CDirItemsStat st;
      HRESULT hresultMain = EnumerateDirItemsAndSort(
          options.arcCensor,
          NWildcard::k_RelatPath,
          UString(), // addPathPrefix
          ArchivePathsSorted,
          ArchivePathsFullSorted,
          st,
          NULL);
      if (hresultMain != S_OK)
        throw CSystemException(hresultMain);
    }

    ecs->MultiArcMode = (ArchivePathsSorted.Size() > 1);

    // The dialog stage (mirror ExtractGUI.cpp's "if (showDialog) { CExtractDialog
    // ... }") runs when options.ShowDialog (the -ad switch) is set and we are not
    // in a non-interactive (headless / -y) run; otherwise the preset/command-line
    // values are accepted directly. Either way, QtExtractGUI_ShowDialog copies the
    // chosen options into eo and the password into the callback.
    const bool extractShowDialog = options.ShowDialog && !disableUserQuestions;
    {
      const HRESULT dres = QtExtractGUI_ShowDialog(
          ArchivePathsFullSorted, eo, ecs,
          !extractShowDialog,   // disableUserQuestions := don't exec the dialog
          nullptr);
      if (dres == E_ABORT)
        return NExitCode::kUserBreak;
      if (dres != S_OK)
        throw CSystemException(dres);
    }

    // GUI-thread prompt objects (GUI-thread affinity for the blocking queued
    // connection from the worker). Mirror of main_extract.cpp.
    QtPasswordPrompt passwordPrompt;
    QtOverwritePrompt overwritePrompt;
    QtMemDialog memPrompt;   // G.8a : decompression memory-limit request prompt

    CDecompressStat stat;
    HRESULT result = QtExtractGUI(
          codecs,
          formatIndices, excludedFormats,
          ArchivePathsSorted,
          ArchivePathsFullSorted,
          options.Censor.Pairs.Front().Head,
          eo,
          ecs,
          &passwordPrompt,
          &overwritePrompt,
          &memPrompt,
          disableUserQuestions,
          nullptr,
          stat);
    if (result != S_OK)
    {
      if (result == E_ABORT)
        return NExitCode::kUserBreak;
      throw CSystemException(result);
    }
    if (!ecs->IsOK())
      return NExitCode::kFatalError;
  }
  else if (options.Command.IsFromUpdateGroup())
  {
    // ---- update group (a/u/d) : mirror GUI.cpp Main2() update branch --------
    #ifndef Z7_NO_CRYPTO
    bool passwordIsDefined = options.PasswordEnabled && !options.Password.IsEmpty();
    #endif

    QtUpdateCallback callback;

    #ifndef Z7_NO_CRYPTO
    callback.PasswordIsDefined = passwordIsDefined;
    callback.AskPassword = options.PasswordEnabled && options.Password.IsEmpty();
    callback.Password = options.Password;
    #endif

    callback.Init();

    if (!options.UpdateOptions.InitFormatIndex(codecs, formatIndices, options.ArchiveName) ||
        !options.UpdateOptions.SetArcPath(codecs, options.ArchiveName))
    {
      // G.1l : original GUI.cpp:350 ErrorLangMessage(IDS_UPDATE_NOT_SUPPORTED)
      // [=3004, ExtractRes.h]. Inline English kept verbatim as the fallback literal.
      ErrorMessage(FmLang(IDS_UPDATE_NOT_SUPPORTED,
          QStringLiteral("Update operations are not supported for this archive")).toUtf8().constData());
      return NExitCode::kFatalError;
    }

    QtPasswordPrompt passwordPrompt;

    // Enter the dialog stage whenever -ad is requested, EVEN when headless: like the
    // extract branch (which always calls QtExtractGUI_ShowDialog and passes the
    // suppress flag), QtUpdateGUI runs QtCompressGUI_ShowDialog with
    // disableUserQuestions controlling whether the dialog is exec()'d or its presets
    // (and the SEVENZQT_* dialog overrides, incl. SEVENZQT_VOLUME) are accepted
    // directly. Without -ad, the parsed command-line options (real -m/-t/-v switches)
    // reach the engine untouched via the non-dialog path.
    const bool updateShowDialog = options.ShowDialog;

    HRESULT result = QtUpdateGUI(
        codecs, formatIndices,
        options.ArchiveName,
        options.Censor,
        options.UpdateOptions,
        updateShowDialog,
        disableUserQuestions,
        &callback,
        &passwordPrompt,
        nullptr);

    if (result != S_OK)
    {
      if (result == E_ABORT)
        return NExitCode::kUserBreak;
      throw CSystemException(result);
    }
    if (callback.FailedFiles.Size() > 0)
      return NExitCode::kWarning;
  }
  else if (options.Command.CommandType == NCommandType::kHash)
  {
    // ---- hash (h) : mirror GUI.cpp Main2() hash branch ----------------------
    CPropNameValPairs resultPairs;
    HRESULT result = QtHashGUI(
        EXTERNAL_CODECS_VARS_L
        options.Censor, options.HashOptions,
        disableUserQuestions, nullptr, resultPairs);

    if (result != S_OK)
    {
      if (result == E_ABORT)
        return NExitCode::kUserBreak;
      throw CSystemException(result);
    }

    // Print the formatted result pairs so the offscreen harness can diff the
    // digests against 7zz h (the per-flow main_hash did this).
    for (unsigned i = 0; i < resultPairs.Size(); i++)
    {
      const CProperty &p = resultPairs[i];
      const AString name = UnicodeStringToMultiByte(p.Name, CP_UTF8);
      const AString val  = UnicodeStringToMultiByte(p.Value, CP_UTF8);
      std::printf("%s: %s\n", name.Ptr(), val.Ptr());
    }
    std::fflush(stdout);
  }
  else
  {
    throw "Unsupported command";
  }
  return 0;
}


// === main() : faithful port of GUI.cpp WinMain (try/catch ladder) ============
// P.2 : load the active translation for the archiver dialogs. Reads [Options]
// Lang from the SAME QSettings the FM uses ("7-Zip"/"7-Zip" IniFormat/UserScope),
// resolves <exe-dir>/Lang/<name>.txt (or $7ZIP_LANG_DIR), and QtLang_LoadFile's
// it. Empty/"-"/missing/bad => English. Mirrors QtFmSettings::StartupLoadLang
// without dragging the FM-only QtFmSettings TU into this binary.
static void LoadLangFor7zqt()
{
  QSettings st(QSettings::IniFormat, QSettings::UserScope,
      QStringLiteral("7-Zip"), QStringLiteral("7-Zip"));
  st.beginGroup(QStringLiteral("Options"));
  const QString name = st.value(QStringLiteral("Lang")).toString().trimmed();
  st.endGroup();
  if (name.isEmpty() || name == QStringLiteral("-"))
    return;

  QString path;
  if (name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char('/')))
    path = name;
  else
  {
    QString dir;
    if (const char *env = getenv("7ZIP_LANG_DIR"))
      dir = QString::fromUtf8(env);
    else
      dir = QCoreApplication::applicationDirPath() + QStringLiteral("/Lang");
    path = dir + QLatin1Char('/') + name;
  }
  if (QFileInfo(path).suffix().isEmpty())
    path += QStringLiteral(".txt");

  UString p;
  for (const QChar &c : path)
    p += (wchar_t)c.unicode();
  QtLang_LoadFile(us2fs(p));   // false => English (left cleared)
}

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);

  LoadLangFor7zqt();   // P.2 : translate dialogs per [Options] Lang (English default)

  const bool headless = DetectHeadless();

  // Build the command-line vector from argv[1..], mirroring Main2()'s
  // SplitCommandLine(GetCommandLineW(), ...) + Delete(0). QApplication has already
  // consumed any Qt-specific switches (e.g. -platform); argv now holds the 7-Zip
  // command + switches.
  UStringVector commandStrings;
  for (int i = 1; i < argc; i++)
    commandStrings.Add(GetUnicodeString(AString(argv[i])));

  try
  {
    return Run7zqt(commandStrings, headless);
  }
  catch(const CNewException &)
  {
    return ShowMemErrorMessage();
  }
  catch(const CMessagePathException &e)
  {
    ErrorMessage(((const UString &)e).Ptr());
    return NExitCode::kUserError;
  }
  catch(const CSystemException &systemError)
  {
    if (systemError.ErrorCode == E_ABORT)
      return NExitCode::kUserBreak;
    return ShowSysErrorMessage(systemError.ErrorCode);
  }
  catch(const UString &s)
  {
    ErrorMessage(s.Ptr());
    return NExitCode::kFatalError;
  }
  catch(const AString &s)
  {
    ErrorMessage(s.Ptr());
    return NExitCode::kFatalError;
  }
  catch(const wchar_t *s)
  {
    ErrorMessage(s);
    return NExitCode::kFatalError;
  }
  catch(const char *s)
  {
    ErrorMessage(s);
    return NExitCode::kFatalError;
  }
  catch(int v)
  {
    AString e ("Error: ");
    e.Add_UInt32((unsigned)v);
    ErrorMessage(e.Ptr());
    return NExitCode::kFatalError;
  }
  catch(...)
  {
    ErrorMessage("Unknown error");
    return NExitCode::kFatalError;
  }
}
