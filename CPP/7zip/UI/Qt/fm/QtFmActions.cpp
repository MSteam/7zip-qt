// QtFmActions.cpp
// ----------------------------------------------------------------------------
// See QtFmActions.h. RunFmCommand() is a faithful lift of main_7zqt.cpp's
// Run7zqt() (the GUI.cpp Main2() port) made callable from the FM shell with a
// command vector and a dialog parent. It does NOT emit GUIDs (main_fm.cpp is the
// single GUID-emitting TU for 7zqt_fm).
// ----------------------------------------------------------------------------

#include "QtFmActions.h"

#include <QtWidgets/QMessageBox>
#include <QtWidgets/QWidget>

#include <cstdio>

#include "../../../../Common/MyException.h"
#include "../../../../Common/NewHandler.h"
#include "../../../../Common/StringConvert.h"

#include "../../../../Windows/FileDir.h"

#include "../../Common/ArchiveCommandLine.h"
#include "../../Common/EnumDirItems.h"
#include "../../Common/ExitCode.h"
#include "../../Common/LoadCodecs.h"
#include "../../Common/OpenArchive.h"   // ParseOpenTypes

#include "../QtExtractCallback.h"
#include "../QtExtractGUI.h"
#include "../QtExtractPrompts.h"
#include "../QtMemDialog.h"        // G.8a : decompression memory-limit request prompt
#include "../QtUpdateCallback.h"
#include "../QtCompressGUI.h"
#include "../QtHashGUI.h"

#include "../QtLang.h"                          // G.1f : FmLang(IDS_*, ...)
#include "../../GUI/ExtractRes.h"               // G.1f : IDS_MEM_ERROR/IDS_UPDATE_NOT_SUPPORTED/IDS_UNSUPPORTED_ARCHIVE_TYPE
#include "QtBenchmarkDialog.h"                  // G.10a : Benchmark dialog + RunBenchmarkConsole + k_NumBenchIterations_Default

using namespace NWindows;
using namespace NFile;
using namespace NDir;

#ifdef Z7_EXTERNAL_CODECS
extern const CExternalCodecs *g_ExternalCodecs_Ptr;
#endif

// main_7zqt.cpp owns the real definition; we only consult it here (set by Parse1).
extern bool g_DisableUserQuestions;

static const char * const kNoFormats =
    "7-Zip cannot find the code that works with archives.";

// --- error reporting : GUI message box (FM is always interactive) ------------
static void FmError(QWidget *parent, const UString &message)
{
  const AString a = UnicodeStringToMultiByte(message, CP_UTF8);
  std::fprintf(stderr, "7-Zip: %s\n", a.Ptr());
  if (parent)
    QMessageBox::warning(parent, QStringLiteral("7-Zip"),
        QString::fromUtf8(a.Ptr()));
}

static void FmError(QWidget *parent, const char *s)
{
  FmError(parent, UString(GetUnicodeString(s)));
}

// G.1f : report an error using the loaded translation for the ORIGINAL numeric
// 7-Zip langID 'id' (FmLang), falling back to the inline English literal. Routes
// the resulting QString back through FmError via UTF-8 (same round-trip as the
// const char* overload), so a loaded Lang/*.txt translates these GUI.cpp messages.
static void FmLangError(QWidget *parent, unsigned id, const char *english)
{
  const QString s = FmLang(id, QString::fromUtf8(english));
  const QByteArray u = s.toUtf8();
  FmError(parent, UString(GetUnicodeString(u.constData())));
}

static void ThrowException_if_Error(HRESULT res)
{
  if (res != S_OK)
    throw CSystemException(res);
}

// === RunFmCommandImpl : the Run7zqt() body, returning a code =================
static int RunFmCommandImpl(const UStringVector &commandStrings,
    bool headless, QWidget *parent)
{
  if (commandStrings.Size() == 0)
    return NExitCode::kFatalError;

  CArcCmdLineOptions options;
  CArcCmdLineParser parser;

  parser.Parse1(commandStrings, options);
  g_DisableUserQuestions = options.YesToAll;
  parser.Parse2(options);

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
      FmError(parent, s);
  }
  #endif

  const bool isExtractGroupCommand = options.Command.IsFromExtractGroup();

  if (codecs->Formats.Size() == 0 &&
        (isExtractGroupCommand || options.Command.IsFromUpdateGroup()))
    throw kNoFormats;

  CObjectVector<COpenType> formatIndices;
  if (!ParseOpenTypes(*codecs, options.ArcType, formatIndices))
  {
    // GUI.cpp:201 ErrorLangMessage(IDS_UNSUPPORTED_ARCHIVE_TYPE)
    FmLangError(parent, IDS_UNSUPPORTED_ARCHIVE_TYPE, "Unsupported archive type");
    return NExitCode::kFatalError;
  }

  CIntVector excludedFormats;
  FOR_VECTOR (k, options.ExcludedArcTypes)
  {
    CIntVector tempIndices;
    if (!codecs->FindFormatForArchiveType(options.ExcludedArcTypes[k], tempIndices)
        || tempIndices.Size() != 1)
    {
      // GUI.cpp:212 ErrorLangMessage(IDS_UNSUPPORTED_ARCHIVE_TYPE)
      FmLangError(parent, IDS_UNSUPPORTED_ARCHIVE_TYPE, "Unsupported archive type");
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
    // GUI.cpp Main2() kBenchmark : Benchmark(options.Properties, numIterations).
    // The FM Tools->Benchmark normally opens the dialog directly (onBenchmark);
    // this command-path branch exists for completeness (a `b` command vector).
    const UInt32 numIterations = options.NumIterations_Defined ?
        options.NumIterations : k_NumBenchIterations_Default;
    if (headless)
    {
      const HRESULT res = RunBenchmarkConsole(
          EXTERNAL_CODECS_VARS_L
          options.Properties, numIterations, stdout);
      std::fflush(stdout);
      if (res != S_OK)
        throw CSystemException(res);
    }
    else
    {
      const HRESULT res = QtBenchmarkDialog::Benchmark(
          options.Properties, numIterations, /*totalMode*/ false, parent);
      if (res != S_OK)
        throw CSystemException(res);
    }
  }
  else if (isExtractGroupCommand)
  {
    // ---- extract / test group (mirror GUI.cpp Main2() extract branch) ------
    UStringVector ArchivePathsSorted;
    UStringVector ArchivePathsFullSorted;

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

    {
      CDirItemsStat st;
      HRESULT hresultMain = EnumerateDirItemsAndSort(
          options.arcCensor, NWildcard::k_RelatPath, UString(),
          ArchivePathsSorted, ArchivePathsFullSorted, st, NULL);
      if (hresultMain != S_OK)
        throw CSystemException(hresultMain);
    }

    ecs->MultiArcMode = (ArchivePathsSorted.Size() > 1);

    const bool extractShowDialog = options.ShowDialog && !disableUserQuestions;
    {
      const HRESULT dres = QtExtractGUI_ShowDialog(
          ArchivePathsFullSorted, eo, ecs, !extractShowDialog, parent);
      if (dres == E_ABORT)
        return NExitCode::kUserBreak;
      if (dres != S_OK)
        throw CSystemException(dres);
    }

    QtPasswordPrompt passwordPrompt;
    QtOverwritePrompt overwritePrompt;
    QtMemDialog memPrompt;   // G.8a : decompression memory-limit request prompt

    CDecompressStat stat;
    HRESULT result = QtExtractGUI(
          codecs, formatIndices, excludedFormats,
          ArchivePathsSorted, ArchivePathsFullSorted,
          options.Censor.Pairs.Front().Head,
          eo, ecs, &passwordPrompt, &overwritePrompt, &memPrompt,
          disableUserQuestions, parent, stat);
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
    // ---- update group (a/u/d) (mirror GUI.cpp Main2() update branch) -------
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
      // GUI.cpp:350 ErrorLangMessage(IDS_UPDATE_NOT_SUPPORTED); Extract.rc:9 English fallback
      FmLangError(parent, IDS_UPDATE_NOT_SUPPORTED, "Update operations are not supported for this archive.");
      return NExitCode::kFatalError;
    }

    QtPasswordPrompt passwordPrompt;
    const bool updateShowDialog = options.ShowDialog;

    HRESULT result = QtUpdateGUI(
        codecs, formatIndices, options.ArchiveName,
        options.Censor, options.UpdateOptions,
        updateShowDialog, disableUserQuestions,
        &callback, &passwordPrompt, parent);

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
    // ---- hash (h) (mirror GUI.cpp Main2() hash branch) ---------------------
    CPropNameValPairs resultPairs;
    HRESULT result = QtHashGUI(
        EXTERNAL_CODECS_VARS_L
        options.Censor, options.HashOptions,
        disableUserQuestions, parent, resultPairs);

    if (result != S_OK)
    {
      if (result == E_ABORT)
        return NExitCode::kUserBreak;
      throw CSystemException(result);
    }

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
    throw "Unsupported command";

  return 0;
}

// === RunFmCommand : the WinMain-style catch ladder around the body ===========
int RunFmCommand(const UStringVector &commandStrings, bool headless, QWidget *parent)
{
  try
  {
    return RunFmCommandImpl(commandStrings, headless, parent);
  }
  catch(const CNewException &)
  {
    // GUI.cpp:119 ErrorLangMessage(IDS_MEM_ERROR); Extract.rc:7 English fallback
    FmLangError(parent, IDS_MEM_ERROR, "The system cannot allocate the required amount of memory");
    return NExitCode::kMemoryError;
  }
  catch(const CMessagePathException &e)
  {
    FmError(parent, ((const UString &)e));
    return NExitCode::kUserError;
  }
  catch(const CSystemException &systemError)
  {
    if (systemError.ErrorCode == E_ABORT)
      return NExitCode::kUserBreak;
    FmError(parent, HResultToMessage(systemError.ErrorCode));
    return NExitCode::kFatalError;
  }
  catch(const UString &s) { FmError(parent, s); return NExitCode::kFatalError; }
  catch(const AString &s) { FmError(parent, UString(GetUnicodeString(s))); return NExitCode::kFatalError; }
  catch(const wchar_t *s) { FmError(parent, UString(s)); return NExitCode::kFatalError; }
  catch(const char *s) { FmError(parent, s); return NExitCode::kFatalError; }
  catch(...) { FmError(parent, "Unknown error"); return NExitCode::kFatalError; }
}
