// QtExtractGUI.cpp
//
// Mirror of GUI/ExtractGUI.cpp's CThreadExtracting::ProcessVirt() and the
// ExtractGUI() free function, ported to the Qt threaded-progress core.

#include "QtExtractGUI.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include "QtExtractCallback.h"
#include "QtExtractDialog.h"
#include "QtExtractPrompts.h"
#include "QtMemDialog.h"        // G.8a : memory-limit request prompt
#include "QtExtractSettings.h"
#include "QtProgressDialog.h"

#include "../Common/ArchiveExtractCallback.h"

#include "../../../Common/StringConvert.h"
#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileName.h"


// === CThreadExtracting::ProcessVirt() mirror ================================
// Runs on the worker thread. Just calls Extract(), passing the same callback
// object cast to the three callback parameters, exactly like the original:
//   Extract(..., ExtractCallbackSpec, ExtractCallbackSpec, FolderArchiveExtractCallback, ...)
HRESULT QtThreadExtracting::ProcessVirt()
{
  // Wire the callback (and the GUI-thread prompt parents) to the progress dialog
  // that Create() has just constructed. ProgressDialog is valid here because
  // QtProgressThreadVirt::Create() creates it before starting this worker thread.
  // Storing these pointers does NOT touch any widget (safe from the worker).
  ExtractCallbackSpec->ProgressDialog = ProgressDialog;
  if (ExtractCallbackSpec->PasswordPrompt)
    ExtractCallbackSpec->PasswordPrompt->SetParentWidget(ProgressDialog);
  if (ExtractCallbackSpec->OverwritePrompt)
    ExtractCallbackSpec->OverwritePrompt->SetParentWidget(ProgressDialog);
  // G.8a : same parenting for the memory-limit request prompt (mirrors the original
  // CMemDialog being created with *ProgressDialog as its parent).
  if (ExtractCallbackSpec->MemPrompt)
    ExtractCallbackSpec->MemPrompt->SetParentWidget(ProgressDialog);

  // --- cancel-wiring proof (test-only) --------------------------------------
  // When SEVENZQT_TEST_CANCEL is set, request stop the same way the Cancel button
  // does (Sync.Set_Stopped(true)). The very next Sync.CheckStop() reached by the
  // engine through this callback (e.g. in SetCompleted / Open_CheckBreak) returns
  // E_ABORT, so Extract() aborts. This proves the cancel path end-to-end without
  // a human clicking Cancel.
  if (!qgetenv("SEVENZQT_TEST_CANCEL").isEmpty())
    ProgressDialog->Sync.Set_Stopped(true);

  HRESULT res = Extract(
      codecs,
      *FormatIndices, *ExcludedFormatIndices,
      *ArchivePaths, *ArchivePathsFull,
      *WildcardCensor, *Options,
      ExtractCallbackSpec,                 // IOpenCallbackUI
      ExtractCallbackSpec,                 // IExtractCallbackUI
      FolderArchiveExtractCallback,        // IFolderArchiveExtractCallback
      NULL,                                // IHashCalc* : no hashing here
      FinalMessage.ErrorMessage.Message, Stat);

  return res;
}


// === ExtractGUI() mirror ====================================================
HRESULT QtExtractGUI(
    CCodecs *codecs,
    const CObjectVector<COpenType> &formatIndices,
    const CIntVector &excludedFormatIndices,
    UStringVector &archivePaths,
    UStringVector &archivePathsFull,
    const NWildcard::CCensorNode &wildcardCensor,
    CExtractOptions &options,
    QtExtractCallback *extractCallback,
    QtPasswordPrompt *passwordPrompt,
    QtOverwritePrompt *overwritePrompt,
    QtMemDialog *memPrompt,
    bool disableUserQuestions,
    QWidget *parent,
    CDecompressStat &statOut)
{
  QtThreadExtracting extracter;
  extracter.codecs = codecs;
  extracter.FormatIndices = &formatIndices;
  extracter.ExcludedFormatIndices = &excludedFormatIndices;

  const UString title = options.TestMode ? UString("Testing") : UString("Extracting");

  extracter.Title = title;
  extracter.ExtractCallbackSpec = extractCallback;
  extracter.ExtractCallbackSpec->PasswordPrompt = passwordPrompt;
  extracter.ExtractCallbackSpec->OverwritePrompt = overwritePrompt;
  extracter.ExtractCallbackSpec->MemPrompt = memPrompt; // G.8a
  // FolderArchiveExtractCallback holds the COM reference (mirrors the original's
  // extracter.FolderArchiveExtractCallback = extractCallback;).
  extracter.FolderArchiveExtractCallback = extractCallback;
  extracter.ExtractCallbackSpec->Init();

  extracter.ArchivePaths = &archivePaths;
  extracter.ArchivePathsFull = &archivePathsFull;
  extracter.WildcardCensor = &wildcardCensor;
  extracter.Options = &options;

  // Mirrors the original g_DisableUserQuestions propagation: when headless, the
  // dialog auto-closes and the prompts fall back (see QtExtractPrompts.cpp).
  extracter.DisableUserQuestions = disableUserQuestions;

  const HRESULT res = extracter.Create(title, parent);
  statOut = extracter.Stat;
  return res;
}


// === QtExtractGUI_ShowDialog : mirror of ExtractGUI()'s dialog stage =========
// Tracks GUI/ExtractGUI.cpp lines ~204-247 (the showDialog block).

static UString QStrToU(const QString &s)
{
  UString u;
  const int n = s.size();
  for (int i = 0; i < n; i++)
    u += (wchar_t)s.at(i).unicode();
  return u;
}

HRESULT QtExtractGUI_ShowDialog(
    UStringVector &archivePathsFull,
    CExtractOptions &options,
    QtExtractCallback *extractCallback,
    bool disableUserQuestions,
    QWidget *parent)
{
  using namespace NWindows;
  using namespace NFile;
  using namespace NFile::NDir;

  // outputDir defaults to current dir when empty (ExtractGUI.cpp ~199-201).
  FString outputDir = options.OutputDir;
  if (outputDir.IsEmpty())
    NDir::GetCurrentDir(outputDir);

  FString outputDirFull;
  if (!MyGetFullPathName(outputDir, outputDirFull))
    return E_FAIL;
  NName::NormalizeDirPathPrefix(outputDirFull);

  QtExtractDialog dialog(parent);

  // options -> dialog (ExtractGUI.cpp 215-231).
  dialog.DirPath = fs2us(outputDirFull);
  dialog.OverwriteMode = options.OverwriteMode;
  dialog.OverwriteMode_Force = options.OverwriteMode_Force;
  dialog.PathMode = options.PathMode;
  dialog.PathMode_Force = options.PathMode_Force;
  dialog.ElimDup = options.ElimDup;
  if (archivePathsFull.Size() == 1)
    dialog.ArcPath = archivePathsFull[0];
  dialog.NtSecurity = options.NtOptions.NtSecurity;
#ifndef Z7_NO_CRYPTO
  if (extractCallback->PasswordIsDefined)
    dialog.Password = extractCallback->Password;
#endif

  // OnInit-equivalent: load persisted settings + populate widgets.
  dialog.fillFromState();

  if (disableUserQuestions)
  {
    // Headless / offscreen test path: the real dialog widgets exist (and are
    // fully wired), but we do not block on user input. Optional env overrides let
    // the harness exercise specific PathMode / OverwriteMode / Password values
    // through the SAME field-copy + persistence path the OK button uses.
    {
      const QByteArray pm = qgetenv("SEVENZQT_PATHMODE");
      if (!pm.isEmpty())
      {
        bool ok = false;
        const int v = QString::fromLatin1(pm).toInt(&ok);
        if (ok)
          dialog.PathMode = (NExtract::NPathMode::EEnum)v;
      }
    }
    {
      const QByteArray ow = qgetenv("SEVENZQT_OVERWRITE");
      if (!ow.isEmpty())
      {
        bool ok = false;
        const int v = QString::fromLatin1(ow).toInt(&ok);
        if (ok)
          dialog.OverwriteMode = (NExtract::NOverwriteMode::EEnum)v;
      }
    }
    {
      const QByteArray pw = qgetenv("SEVENZQT_PASSWORD");
      if (!pw.isEmpty())
        dialog.Password = QStrToU(QString::fromUtf8(pw));
    }
    {
      const QByteArray od = qgetenv("SEVENZQT_OUTDIR");
      if (!od.isEmpty())
        dialog.DirPath = QStrToU(QString::fromUtf8(od));
    }

    // G.7e : SEVENZQT_EXTRACT_NAME drives the split-destination sub-folder name
    // field. Setting it enables SplitDest and appends the given name to the
    // directory prefix, mirroring the dialog's OnOK SplitDest concat
    // (QtExtractDialog::onAccept / ExtractDialog.cpp 373-388). The effect is the
    // same as a user ticking "Sub-folder name:" and typing the name: files land
    // in <prefix>/<name>/.
    const QByteArray splitName = qgetenv("SEVENZQT_EXTRACT_NAME");
    const bool splitDest = !splitName.isEmpty();

    // Persist the (possibly overridden) choices exactly like onAccept(): force
    // the modes so a non-default value is written to QtExtractSettings and read
    // back as the default on the next run. This drives the persistence test.
    NExtractQt::CInfo info;
    info.Load();
    info.PathMode_Force = true;
    info.PathMode = dialog.PathMode;
    info.OverwriteMode_Force = true;
    info.OverwriteMode = dialog.OverwriteMode;
    info.ElimDup.SetVal_as_Defined(dialog.ElimDup.Val);
    {
      UString s = dialog.DirPath;
      s.Trim();
      NName::NormalizeDirPathPrefix(s);
      if (splitDest)
      {
        UString pathName = QStrToU(QString::fromUtf8(splitName));
        pathName.Trim();
        s += pathName;
        NName::NormalizeDirPathPrefix(s);
      }
      dialog.DirPath = s;
      info.Paths.Clear();
      info.Paths.Add(s);
    }
    if (splitDest != info.SplitDest.Val)
    {
      info.SplitDest.Def = true;
      info.SplitDest.Val = splitDest;
    }
    info.Save();
  }
  else
  {
    if (dialog.exec() != QDialog::Accepted)
      return E_ABORT; // mirror ExtractGUI.cpp "return E_ABORT"
  }

  // dialog -> options (ExtractGUI.cpp 236-247).
  FString chosen = us2fs(dialog.DirPath);
  options.OverwriteMode = dialog.OverwriteMode;
  options.OverwriteMode_Force = true;
  options.PathMode = dialog.PathMode;
  options.PathMode_Force = true;
  options.ElimDup = dialog.ElimDup;
  options.NtOptions.NtSecurity = dialog.NtSecurity;
#ifndef Z7_NO_CRYPTO
  extractCallback->Password = dialog.Password;
  extractCallback->PasswordIsDefined = !dialog.Password.IsEmpty();
#endif

  // Normalize OutputDir (ExtractGUI.cpp ~248-254).
  if (!MyGetFullPathName(chosen, options.OutputDir))
    return E_FAIL;
  NName::NormalizeDirPathPrefix(options.OutputDir);

  return S_OK;
}
