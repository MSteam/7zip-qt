// QtCompressGUI.cpp
//
// Mirror of GUI/UpdateGUI.cpp's CThreadUpdating::ProcessVirt() and the
// CInfo -> CUpdateOptions + CProperty translation done in its ShowDialog(),
// ported to the Qt threaded-progress core.

#include "QtCompressGUI.h"

#include <cstdio> // fprintf (headless mem-gate / mem-debug diagnostics)

#include <QtCore/QByteArray>

#include <QtCore/QString>
#include <QtWidgets/QDialog>

#include "QtUpdateCallback.h"
#include "QtExtractPrompts.h"
#include "QtProgressDialog.h"
#include "QtCompressDialog.h"

#include "../../../Common/StringConvert.h"
#include "../../../Common/StringToInt.h" // ConvertStringToUInt64 (IsThereMethodOverride)

#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileName.h"
#include "../../../Windows/FileFind.h"   // G.7d : NFind::CFileInfo (oneFile detection)

using namespace NWindows;
using namespace NFile;
using namespace NDir;


// === CThreadUpdating::ProcessVirt() mirror (UpdateGUI.cpp 51-62) =============
HRESULT QtThreadUpdating::ProcessVirt()
{
  // Wire the callback to the progress dialog Create() just constructed (same
  // pattern as QtThreadExtracting::ProcessVirt): storing pointers, no widget
  // touch. ProgressDialog is valid here (Create() built it before this thread).
  UpdateCallback->ProgressDialog = ProgressDialog;
  if (UpdateCallback->PasswordPrompt)
    UpdateCallback->PasswordPrompt->SetParentWidget(ProgressDialog);

  // cancel-wiring proof (test-only), same hook as the extract worker.
  if (!qgetenv("SEVENZQT_TEST_CANCEL").isEmpty())
    ProgressDialog->Sync.Set_Stopped(true);

  CUpdateErrorInfo ei;
  HRESULT res = UpdateArchive(codecs, *formatIndices, *cmdArcPath,
      *WildcardCensor, *Options,
      ei,
      UpdateCallback,   // IOpenCallbackUI
      UpdateCallback,   // IUpdateCallbackUI2
      needSetPath);
  FinalMessage.ErrorMessage.Message = ei.Message.Ptr();
  for (unsigned i = 0; i < ei.FileNames.Size(); i++)
    AddErrorPath(ei.FileNames[i]);
  if (res != S_OK)
    return res;
  return HRESULT_FROM_WIN32(ei.SystemError);
}


// ============================================================================
// === CProperty builders : VERBATIM from UpdateGUI.cpp (lines 111-201) =======
// ============================================================================
static void AddProp_UString(CObjectVector<CProperty> &properties, const char *name, const UString &value)
{
  CProperty prop;
  prop.Name = name;
  prop.Value = value;
  properties.Add(prop);
}

static void AddProp_UInt32(CObjectVector<CProperty> &properties, const char *name, UInt32 value)
{
  UString s;
  s.Add_UInt32(value);
  AddProp_UString(properties, name, s);
}

static void AddProp_bool(CObjectVector<CProperty> &properties, const char *name, bool value)
{
  AddProp_UString(properties, name, UString(value ? "on": "off"));
}

static void AddProp_BoolPair(CObjectVector<CProperty> &properties,
    const char *name, const CBoolPair &bp)
{
  if (bp.Def)
    AddProp_bool(properties, name, bp.Val);
}

static void AddProp_Size(CObjectVector<CProperty> &properties, const char *name, const UInt64 size)
{
  UString s;
  s.Add_UInt64(size);
  s.Add_Char('b');
  AddProp_UString(properties, name, s);
}


// === Options-string pipeline : VERBATIM from UpdateGUI.cpp (lines 141-193) ===
// G.7a: these three lift the free-form Parameters string (di.Options) into the
// engine's CProperty list, exactly as UpdateGUI.cpp does. SplitOptionsToStrings
// splits on whitespace and strips a leading "-m" off each token; IsThereMethodOverride
// detects whether the user typed an explicit method switch (so the combo method/dict/
// order are suppressed via setMethod=false); ParseAndAddPropertires turns each
// "name=value" token into a CProperty.
static void SplitOptionsToStrings(const UString &src, UStringVector &strings)
{
  SplitString(src, strings);
  FOR_VECTOR (i, strings)
  {
    UString &s = strings[i];
    if (s.Len() > 2
        && s[0] == '-'
        && MyCharLower_Ascii(s[1]) == 'm')
      s.DeleteFrontal(2);
  }
}

static bool IsThereMethodOverride(bool is7z, const UStringVector &strings)
{
  FOR_VECTOR (i, strings)
  {
    const UString &s = strings[i];
    if (is7z)
    {
      const wchar_t *end;
      UInt64 n = ConvertStringToUInt64(s, &end);
      if (n == 0 && *end == L'=')
        return true;
    }
    else
    {
      if (s.Len() > 0)
        if (s[0] == L'm' && s[1] == L'=')
          return true;
    }
  }
  return false;
}

static void ParseAndAddPropertires(CObjectVector<CProperty> &properties,
    const UStringVector &strings)
{
  FOR_VECTOR (i, strings)
  {
    const UString &s = strings[i];
    CProperty property;
    const int index = s.Find(L'=');
    if (index < 0)
      property.Name = s;
    else
    {
      property.Name.SetFrom(s, (unsigned)index);
      property.Value = s.Ptr(index + 1);
    }
    properties.Add(property);
  }
}


// === SetOutProperties : VERBATIM from UpdateGUI.cpp (lines 205-281) ==========
// This is THE CInfo->CProperty translation. For C.3a only Level / EncryptionMethod
// / EncryptHeaders are non-sentinel (Method empty, Dict64/Order/NumThreads = -1,
// SolidIsSpecified false, MemUsage undefined, the time CBoolPairs undefined), so
// only "x", "em" and "he" are emitted in practice. The full block is copied
// verbatim so that when C.3b starts setting the advanced fields, the exact same
// translation already applies (no behavioral drift from upstream).
static void SetOutProperties(
    CObjectVector<CProperty> &properties,
    const NCompressDialog::CInfo &di,
    bool is7z,
    bool setMethod)
{
  if (di.Level != (UInt32)(Int32)-1)
    AddProp_UInt32(properties, "x", (UInt32)di.Level);
  if (setMethod)
  {
    if (!di.Method.IsEmpty())
      AddProp_UString(properties, is7z ? "0": "m", di.Method);
    if (di.Dict64 != (UInt64)(Int64)-1)
    {
      AString name;
      if (is7z)
        name = "0";
      name += (di.OrderMode ? "mem" : "d");
      AddProp_Size(properties, name, di.Dict64);
    }
    if (di.Order != (UInt32)(Int32)-1)
    {
      AString name;
      if (is7z)
        name = "0";
      name += (di.OrderMode ? "o" : "fb");
      AddProp_UInt32(properties, name, (UInt32)di.Order);
    }
  }

  if (!di.EncryptionMethod.IsEmpty())
    AddProp_UString(properties, "em", di.EncryptionMethod);

  if (di.EncryptHeadersIsAllowed)
    AddProp_bool(properties, "he", di.EncryptHeaders);

  if (di.SolidIsSpecified)
    AddProp_Size(properties, "s", di.SolidBlockSize);

  if (di.NumThreads != (UInt32)(Int32)-1)
    AddProp_UInt32(properties, "mt", di.NumThreads);

  const NCompression::CMemUse &memUse = di.MemUsage;
  if (memUse.IsDefined)
  {
    const char *kMemUse = "memuse";
    if (memUse.IsPercent)
    {
      UString s;
      s.Add_UInt64(memUse.Val);
      s.Add_Char('%');
      AddProp_UString(properties, kMemUse, s);
    }
    else
      AddProp_Size(properties, kMemUse, memUse.Val);
  }

  AddProp_BoolPair(properties, "tm", di.MTime);
  AddProp_BoolPair(properties, "tc", di.CTime);
  AddProp_BoolPair(properties, "ta", di.ATime);

  if (di.TimePrec != (UInt32)(Int32)-1)
    AddProp_UInt32(properties, "tp", di.TimePrec);
}


// === update-mode -> action-set table : VERBATIM from UpdateGUI.cpp 284-296 ===
struct C_UpdateMode_ToAction_Pair
{
  NCompressDialog::NUpdateMode::EEnum UpdateMode;
  const NUpdateArchive::CActionSet *ActionSet;
};

static const C_UpdateMode_ToAction_Pair g_UpdateMode_Pairs[] =
{
  { NCompressDialog::NUpdateMode::kAdd,    &NUpdateArchive::k_ActionSet_Add },
  { NCompressDialog::NUpdateMode::kUpdate, &NUpdateArchive::k_ActionSet_Update },
  { NCompressDialog::NUpdateMode::kFresh,  &NUpdateArchive::k_ActionSet_Fresh },
  { NCompressDialog::NUpdateMode::kSync,   &NUpdateArchive::k_ActionSet_Sync }
};

static int FindUpdateMode(NCompressDialog::NUpdateMode::EEnum mode)
{
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_UpdateMode_Pairs); i++)
    if (mode == g_UpdateMode_Pairs[i].UpdateMode)
      return (int)i;
  return -1;
}


// === QtCompressGUI : mirror of UpdateGUI()'s translation + run ===============
// Replays the second half of ShowDialog() (UpdateGUI.cpp 460-540): the bits that
// run AFTER the dialog returns IDOK and copy `di` (== info) into `options`.
HRESULT QtCompressGUI(
    CCodecs *codecs,
    const NCompressDialog::CInfo &info,
    NWildcard::CCensor &censor,
    CUpdateOptions &options,
    QtUpdateCallback *updateCallback,
    QtPasswordPrompt *passwordPrompt,
    bool disableUserQuestions,
    QWidget *parent)
{
  const NCompressDialog::CInfo &di = info;

  // ---- the action-set for the chosen update mode (UpdateGUI.cpp 486-491) ----
  if (options.Commands.Size() != 1)
  {
    // mirror "It must be one command" precondition; we ensure exactly one.
    options.SetActionCommand_Add();
  }
  {
    const int index = FindUpdateMode(di.UpdateMode);
    if (index < 0)
      return E_FAIL;
    options.Commands.Front().ActionSet = *g_UpdateMode_Pairs[(unsigned)index].ActionSet;
  }

  options.PathMode = di.PathMode; // (UpdateGUI.cpp 493)

  const CArcInfoEx &archiverInfo = codecs->Formats[(unsigned)di.FormatIndex];

  // ---- password (UpdateGUI.cpp 496-498) ------------------------------------
  updateCallback->PasswordIsDefined = (!di.Password.IsEmpty());
  if (updateCallback->PasswordIsDefined)
    updateCallback->Password = di.Password;
  // If a password is set we never need to ask; if not, leave AskPassword as set
  // by the caller (the engine asks via CryptoGetTextPassword2 only if AskPassword).

  // ---- clear cmdline props, fill from dialog (UpdateGUI.cpp 501-514) --------
  options.MethodMode.Properties.Clear();

  const bool is7z = archiverInfo.Is_7z();

  // G.7a: the free-form Parameters string (di.Options) now flows to the engine
  // exactly as UpdateGUI.cpp 505-514: split into switch tokens, detect an explicit
  // method override (which suppresses the combo method/dict/order via setMethod=false),
  // emit the combo properties, then append the user's parsed switches. When di.Options
  // is empty this is identical to the previous behavior (no tokens, methodOverride=false).
  UStringVector optionStrings;
  SplitOptionsToStrings(di.Options, optionStrings);
  const bool methodOverride = IsThereMethodOverride(is7z, optionStrings);

  SetOutProperties(options.MethodMode.Properties, di, is7z, !methodOverride /* setMethod */);

  options.OpenShareForWrite = di.OpenShareForWrite;
  ParseAndAddPropertires(options.MethodMode.Properties, optionStrings);

  // ---- Link / security / arc-mtime options (UpdateGUI.cpp 462-468) ----------
  // These reach the engine via CUpdateOptions fields (Update.cpp turns them into
  // dirItems.SymLinks / updateCallbackSpec->StoreSymLinks / dirItems.ReadSecure /
  // dirItems.ScanAltStreams), NOT via CProperty/SetOutProperties. The time props
  // (tm/tc/ta/tp) above DO go through SetOutProperties. This six-line block was
  // deferred in C.3 and is added by B.9 as a verbatim mirror of UpdateGUI.cpp.
  // AltStreams/NtSecurity are NT-only: their CBoolPair.Def is never set on Linux
  // (the Options dialog's two NT controls are disabled and clear), so these copy
  // the engine's harmless defaults; SymLinks/HardLinks are functional on Linux.
  options.SymLinks   = di.SymLinks;
  options.HardLinks  = di.HardLinks;
  options.AltStreams = di.AltStreams;
  options.NtSecurity = di.NtSecurity;
  options.SetArcMTime = di.SetArcMTime.Val;
  if (di.PreserveATime.Def)
    options.PreserveATime = di.PreserveATime.Val;

  // G.7b: delete the source files after a successful compress (UpdateGUI.cpp:460).
  // Update.cpp removes the censored input files once the archive is written OK.
  options.DeleteAfterCompressing = di.DeleteAfterCompressing;

  // ---- SFX is out of scope for C.3a; keep options.SfxMode as-is -------------
  if (di.SFXMode)
    options.SfxMode = true;

  // ---- format type (UpdateGUI.cpp 518-520) ---------------------------------
  options.MethodMode.Type = COpenType();
  options.MethodMode.Type_Defined = true;
  options.MethodMode.Type.FormatIndex = di.FormatIndex;

  // ---- archive path parsing (UpdateGUI.cpp 522-527) ------------------------
  options.ArchivePath.VolExtension = archiverInfo.GetMainExt();
  options.ArchivePath.BaseExtension = options.ArchivePath.VolExtension;
  options.ArchivePath.ParseFromPath(di.ArcPath, k_ArcNameMode_Smart);

  options.VolumesSizes = di.VolumeSizes; // empty in C.3a

  // ---- the worker (UpdateGUI.cpp 567-604) ----------------------------------
  // Since we always run the dialog stage, needSetPath is false: the path is
  // already parsed into options.ArchivePath and the output format is already in
  // options.MethodMode.Type, mirroring UpdateGUI.cpp's "needSetPath = false"
  // after ShowDialog. With needSetPath==false UpdateArchive does not consult the
  // `types` argument (it's only used by InitFormatIndex on the needSetPath path),
  // so an empty types vector is correct here. NOTE: UpdateArchive itself calls
  // censor.AddPathsToCensor(options.PathMode) (Update.cpp:1157), so the caller
  // must leave the input items in censor.CensorPaths (NOT pre-converted).
  CObjectVector<COpenType> emptyTypes;

  QtThreadUpdating tu;
  tu.needSetPath = false;
  tu.codecs = codecs;
  tu.formatIndices = &emptyTypes;

  const UString cmdArcPath = di.ArcPath;
  tu.cmdArcPath = &cmdArcPath;

  tu.UpdateCallback = updateCallback;
  tu.UpdateCallback->PasswordPrompt = passwordPrompt;
  tu.UpdateCallback->ProgressDialog = nullptr; // set in ProcessVirt
  tu.UpdateCallback->Init();

  tu.WildcardCensor = &censor;
  tu.Options = &options;
  tu.DisableUserQuestions = disableUserQuestions;

  const UString title("Compressing");
  const HRESULT res = tu.Create(title, parent);
  return res;
}


// === QtCompressGUI_ShowDialog : the dialog stage ============================
static UString QStrToU(const QString &s)
{
  UString u;
  const int n = s.size();
  for (int i = 0; i < n; i++)
    u += (wchar_t)s.at(i).unicode();
  return u;
}

HRESULT QtCompressGUI_ShowDialog(
    CCodecs *codecs,
    NCompressDialog::CInfo &info,
    bool disableUserQuestions,
    QWidget *parent,
    const UString &originalFileName)
{
  QtCompressDialog dialog(parent);
  dialog.ArcFormats = &codecs->Formats;
  dialog.Info = info; // seed default name / FormatIndex / Password
  // G.7d: the single source file's leaf name (mirror dialog.OriginalFileName,
  // UpdateGUI.cpp:423), used by SetArchiveName when a KeepName format (gzip/bzip2)
  // is selected for a single file.
  dialog.OriginalFileName = originalFileName;

  // SetMethods (UpdateGUI.cpp 390-392): enumerate the external (plugin) codecs and
  // hand them to the dialog so the 7z Method list can include them. Must run BEFORE
  // fillFromState() (which builds the Method combo).
  {
    CObjectVector<CCodecInfoUser> userCodecs;
    codecs->Get_CodecsInfoUser_Vector(userCodecs);
    dialog.setMethods(userCodecs);
  }

  // OnInit-equivalent: load persisted settings + populate widgets.
  dialog.fillFromState();

  if (disableUserQuestions)
  {
    // Headless: do not exec(); accept presets/defaults. The real dialog widgets
    // exist and are wired. We drive the REAL combos for Format / Level / Method /
    // Dictionary / Order FIRST (so the same SetMethod/SetDictionary2/SetOrder2
    // cascade the OK button relies on runs), THEN read the result back via the
    // SAME syncInfoFromWidgets() the OK button uses, THEN apply the remaining
    // (non-cascading) env overrides on top.

    // Format (by name): drive the combo so onFormatChanged rebuilds the cascade.
    {
      const QByteArray v = qgetenv("SEVENZQT_FORMAT");
      if (!v.isEmpty())
        dialog.selectFormatByName(QStrToU(QString::fromLatin1(v)));
    }
    // Level: drive the combo so the SetMethod cascade re-runs for the new level.
    {
      const QByteArray v = qgetenv("SEVENZQT_LEVEL");
      if (!v.isEmpty())
      {
        bool ok = false;
        const int lv = QString::fromLatin1(v).toInt(&ok);
        if (ok)
          dialog.selectLevelByValue((UInt32)lv);
      }
    }
    // Method (by name): drive the combo so MethodChanged rebuilds Dictionary+Order.
    {
      const QByteArray v = qgetenv("SEVENZQT_METHOD");
      if (!v.isEmpty())
        dialog.selectMethodByName(QStrToU(QString::fromLatin1(v)));
    }
    // Dictionary (real size in bytes, e.g. 65536 or "64K"/"64M" handled by caller).
    {
      const QByteArray v = qgetenv("SEVENZQT_DICT");
      if (!v.isEmpty())
      {
        bool ok = false;
        const qulonglong d = QString::fromLatin1(v).toULongLong(&ok);
        if (ok)
          dialog.selectDictionaryByValue((UInt64)d);
      }
    }
    // Order (word size / fb / PPMd order, e.g. 273 / 64 / 16).
    {
      const QByteArray v = qgetenv("SEVENZQT_ORDER");
      if (!v.isEmpty())
      {
        bool ok = false;
        const uint o = QString::fromLatin1(v).toUInt(&ok);
        if (ok)
          dialog.selectOrderByValue((UInt32)o);
      }
    }
    // C.3b-2: Solid / Threads / MemUse drive the REAL combos (before the read-back),
    // so the SAME SetSolidBlockSize2/SetNumThreads2/SetMemUseCombo cascade and the
    // SAME getters (GetBlockSizeSpec/GetNumThreadsSpec/Get_MemUse_Spec) the OK path
    // relies on run. SEVENZQT_SOLID is the block-size LOG (0=Non-solid, 64=Solid,
    // else 2^log); SEVENZQT_THREADS is the thread count; SEVENZQT_MEMUSE is the
    // registry-form mem spec (e.g. "90%", "16M").
    const bool memDebug = !qgetenv("SEVENZQT_MEMDEBUG").isEmpty();
    {
      const QByteArray v = qgetenv("SEVENZQT_SOLID");
      if (!v.isEmpty())
      {
        bool ok = false;
        const uint log = QString::fromLatin1(v).toUInt(&ok);
        if (ok)
        {
          const bool applied = dialog.selectSolidByLogSize((UInt32)log);
          if (memDebug)
            fprintf(stderr, "SEVENZQT_SOLID applied=%d (combo greyed/absent if 0)\n", applied ? 1 : 0);
        }
      }
    }
    {
      const QByteArray v = qgetenv("SEVENZQT_THREADS");
      if (!v.isEmpty())
      {
        bool ok = false;
        const uint n = QString::fromLatin1(v).toUInt(&ok);
        if (ok)
        {
          const bool applied = dialog.selectNumThreadsByValue((UInt32)n);
          if (memDebug)
            fprintf(stderr, "SEVENZQT_THREADS applied=%d (combo greyed/absent if 0)\n", applied ? 1 : 0);
        }
      }
    }
    {
      const QByteArray v = qgetenv("SEVENZQT_MEMUSE");
      if (!v.isEmpty())
      {
        const bool applied = dialog.selectMemUseByText(QStrToU(QString::fromLatin1(v)));
        if (memDebug)
          fprintf(stderr, "SEVENZQT_MEMUSE applied=%d\n", applied ? 1 : 0);
      }
    }

    // C.6: drive the REAL Volume combo (before the read-back), so the SAME
    // ParseVolumeSizes the OK path runs fills Info.VolumeSizes (-> options.VolumesSizes
    // via QtCompressGUI). SEVENZQT_VOLUME is the volume-size string the user would
    // type into m_Volume (e.g. "100K", "650M", "10k 20m"). Empty == single archive.
    // If the dialog was already seeded with a `-v` value (info.VolumeSizes non-empty),
    // the env var takes precedence only when set.
    {
      const QByteArray v = qgetenv("SEVENZQT_VOLUME");
      if (!v.isEmpty())
        dialog.setVolumeText(QStrToU(QString::fromLatin1(v)));
    }

    // G.7a: drive the REAL Parameters edit (before the read-back), so the SAME
    // m_Params.GetText -> Info.Options the OK path runs picks up the free-form switch
    // string and QtCompressGUI's SplitOptionsToStrings/ParseAndAddPropertires feed it
    // to the engine. SEVENZQT_PARAMS is the verbatim switch string (e.g. "x=9 lc=4").
    {
      const QByteArray v = qgetenv("SEVENZQT_PARAMS");
      if (!v.isEmpty())
        dialog.setParamsText(QStrToU(QString::fromUtf8(v)));
    }
    // G.7b: drive the REAL Delete-after checkbox. SEVENZQT_DELAFTER=1 deletes the
    // source files after a successful compress (Info.DeleteAfterCompressing).
    {
      const QByteArray v = qgetenv("SEVENZQT_DELAFTER");
      if (!v.isEmpty())
        dialog.setDeleteAfterCompressing(v != "0");
    }
    // G.7c: drive the REAL archive-path edit text, exactly as a user typing into
    // m_ArchivePath. This fires the editTextChanged -> ArcPath_WasChanged auto-switch,
    // so if the typed name's extension belongs to a different format than the one
    // currently selected, the Format combo re-selects (and syncInfoFromWidgets below
    // then reads di.FormatIndex == the switched format). The on-disk archive 7zz l
    // identifies it as that format — the verifiable EFFECT. Apply AFTER SEVENZQT_FORMAT
    // so a typed extension reflects the final intent (a user typing "out.zip" wins).
    {
      const QByteArray v = qgetenv("SEVENZQT_ARCNAME");
      if (!v.isEmpty())
        dialog.setArchivePathText(QStrToU(QString::fromUtf8(v)));
    }
    // G.7c debug: dump the Browse filter list + the (possibly auto-switched) current
    // Format-combo row so the harness can verify both the filter build and the switch.
    if (!qgetenv("SEVENZQT_BROWSEFILTERS").isEmpty())
    {
      const UStringVector filters = dialog.browseFilterStrings();
      for (unsigned i = 0; i < filters.Size(); i++)
        fprintf(stderr, "SEVENZQT_BROWSEFILTER %s\n", GetAnsiString(filters[i]).Ptr());
      const int fi = dialog.Info.FormatIndex;
      if (fi >= 0)
        fprintf(stderr, "SEVENZQT_CURFORMAT %s\n",
            GetAnsiString(codecs->Formats[(unsigned)dialog.Info.FormatIndex].Name).Ptr());
    }

    // --- RAM gate (OnOK 1097-1119) ------------------------------------------
    // Headless mirror of the OK button's accept-blocking gate. SEVENZQT_MEMGATE
    // controls test bypass/force: "off" force-skips the gate (proves the success
    // path even with a tiny limit), "on"/default lets the real gate fire. When the
    // gate fires we report E_ABORT (the archive is NOT created), exactly as the
    // interactive dialog would refuse to accept().
    {
      const QByteArray gateMode = qgetenv("SEVENZQT_MEMGATE");
      if (gateMode != "off")
      {
        UString msg;
        if (dialog.memGateBlocks(msg))
        {
          // Surface the gate decision + estimate on stderr for the harness.
          UInt64 decompMem = 0;
          const UInt64 est = dialog.estimatedCompressMemory(decompMem);
          fprintf(stderr, "SEVENZQT_MEMGATE_FIRED estCompressBytes=%llu limitBytes=%llu\n",
              (unsigned long long)est, (unsigned long long)dialog.getMemUseBytes());
          return E_ABORT;
        }
      }
    }

    // Debug line: the estimated compress/decompress memory for the chosen settings
    // (proves the estimator runs and scales with dictionary). Printed only when
    // SEVENZQT_MEMDEBUG is set, so normal runs stay quiet.
    if (!qgetenv("SEVENZQT_MEMDEBUG").isEmpty())
    {
      UInt64 decompMem = 0;
      const UInt64 est = dialog.estimatedCompressMemory(decompMem);
      fprintf(stderr, "SEVENZQT_MEMDEBUG estCompressBytes=%llu estDecompressBytes=%llu limitBytes=%llu\n",
          (unsigned long long)est, (unsigned long long)decompMem,
          (unsigned long long)dialog.getMemUseBytes());
    }

    // Read the cascade result back into Info (the OK read-back path).
    dialog.syncInfoFromWidgets();
    NCompressDialog::CInfo &di = dialog.Info;

    // B.9 link / time options (headless seam). These mirror the Options sub-dialog
    // populating Info's CBoolPairs + TimePrec. syncInfoFromWidgets() does NOT touch
    // these fields, so seeding them here reaches QtCompressGUI() unchanged (the
    // link/security options-copy + the tm/tc/ta/tp SetOutProperties props).
    // SymLinks/HardLinks/time options are functional on Linux; AltStreams/NtSecurity
    // are NT-only and intentionally NOT exposed here (their Def stays false).
    if (!qgetenv("SEVENZQT_SYMLINKS").isEmpty())  { di.SymLinks.Def = true;  di.SymLinks.Val  = (qgetenv("SEVENZQT_SYMLINKS")  != "0"); }
    if (!qgetenv("SEVENZQT_HARDLINKS").isEmpty()) { di.HardLinks.Def = true; di.HardLinks.Val = (qgetenv("SEVENZQT_HARDLINKS") != "0"); }
    if (!qgetenv("SEVENZQT_MTIME").isEmpty())     { di.MTime.Def = true;     di.MTime.Val     = (qgetenv("SEVENZQT_MTIME")     != "0"); }
    if (!qgetenv("SEVENZQT_CTIME").isEmpty())     { di.CTime.Def = true;     di.CTime.Val     = (qgetenv("SEVENZQT_CTIME")     != "0"); }
    if (!qgetenv("SEVENZQT_ATIME").isEmpty())     { di.ATime.Def = true;     di.ATime.Val     = (qgetenv("SEVENZQT_ATIME")     != "0"); }
    if (!qgetenv("SEVENZQT_SETARCMTIME").isEmpty()){ di.SetArcMTime.Def = true; di.SetArcMTime.Val = (qgetenv("SEVENZQT_SETARCMTIME") != "0"); }
    if (!qgetenv("SEVENZQT_PRESERVEATIME").isEmpty()){ di.PreserveATime.Def = true; di.PreserveATime.Val = (qgetenv("SEVENZQT_PRESERVEATIME") != "0"); }
    { const QByteArray v = qgetenv("SEVENZQT_TIMEPREC"); bool ok=false; const uint p=QString::fromLatin1(v).toUInt(&ok); if (ok) di.TimePrec=(UInt32)p; }

    // Level fallback: if no level was offered/selected, default to Normal so the
    // engine still produces a real archive (mirrors a sensible default).
    if (di.Level == (UInt32)(Int32)-1)
      di.Level = 5;

    // Update mode
    {
      const QByteArray v = qgetenv("SEVENZQT_UPDATEMODE");
      if (!v.isEmpty())
      {
        bool ok = false;
        const int m = QString::fromLatin1(v).toInt(&ok);
        if (ok)
          di.UpdateMode = (NCompressDialog::NUpdateMode::EEnum)m;
      }
    }
    // Path mode
    {
      const QByteArray v = qgetenv("SEVENZQT_PATHMODE_C");
      if (!v.isEmpty())
      {
        bool ok = false;
        const int m = QString::fromLatin1(v).toInt(&ok);
        if (ok)
          di.PathMode = (NWildcard::ECensorPathMode)m;
      }
    }
    // Password
    {
      const QByteArray v = qgetenv("SEVENZQT_PASSWORD");
      if (!v.isEmpty())
        di.Password = QStrToU(QString::fromUtf8(v));
    }
    // Encryption method (only emitted when != format default; here we set it
    // verbatim, matching what the combo's GetEncryptionMethodSpec would return:
    // dash-stripped, e.g. AES256). Tests pass "AES256" or "ZipCrypto".
    {
      const QByteArray v = qgetenv("SEVENZQT_ENCMETHOD");
      if (!v.isEmpty())
        di.EncryptionMethod = QStrToU(QString::fromLatin1(v));
    }
    // Encrypt headers
    {
      const QByteArray v = qgetenv("SEVENZQT_ENCHEADERS");
      if (!v.isEmpty())
        di.EncryptHeaders = (v != "0");
    }

    // EncryptHeadersIsAllowed depends on format being 7z (mirror the dialog's
    // SetEncryptionMethod). Recompute it for the (possibly overridden) format.
    if (di.FormatIndex >= 0)
      di.EncryptHeadersIsAllowed = codecs->Formats[(unsigned)di.FormatIndex].Is_7z();

    // Re-derive the archive-name EXTENSION for the (possibly overridden) format.
    // In the interactive dialog this is done by onFormatChanged() -> SetArchiveName
    // when the user changes the Format combo; here the format override is applied
    // after the name was built, so we redo the extension swap now. Mirrors
    // SetArchiveName: strip any existing extension, append the format's main ext.
    if (di.FormatIndex >= 0 && !di.ArcPath.IsEmpty())
    {
      UString base = di.ArcPath;
      const int dot = base.ReverseFind_Dot();
      const int sep = base.ReverseFind_PathSepar();
      if (dot > sep + 1) // a real extension, not a leading "./"
        base.DeleteFrom(dot);
      base.Add_Dot();
      base += codecs->Formats[(unsigned)di.FormatIndex].GetMainExt();
      di.ArcPath = base;
    }

    // Persist the (possibly overridden) choices exactly like onAccept(): write
    // Level / ArcType / ShowPassword / EncryptHeaders + the per-format Level /
    // EncryptionMethod + the ArcPath MRU to NCompressionQt::CInfo, so the
    // "Compression" group is written and read back as the default next run. This
    // drives the persistence test. (onAccept() does the same via _regInfo.)
    {
      NCompressionQt::CInfo reg;
      reg.Load();
      reg.Level = di.Level;
      reg.ShowPassword = false;
      reg.EncryptHeaders = di.EncryptHeaders;
      if (di.FormatIndex >= 0)
        reg.ArcType = codecs->Formats[(unsigned)di.FormatIndex].Name;
      {
        NCompressionQt::CFormatOptions &fo = reg.Get_FormatOptions(reg.ArcType);
        fo.Level = di.Level;
        fo.EncryptionMethod = di.EncryptionMethod;
      }
      {
        // Build the final archive name with the correct extension for the MRU,
        // mirroring how the dialog stores the typed path.
        UStringVector arcPaths;
        UString p = di.ArcPath;
        if (!p.IsEmpty())
          arcPaths.Add(p);
        for (unsigned i = 0; i < reg.ArcPaths.Size() && arcPaths.Size() < 16; i++)
        {
          bool dup = false;
          for (unsigned j = 0; j < arcPaths.Size(); j++)
            if (reg.ArcPaths[i].IsEqualTo_NoCase(arcPaths[j])) { dup = true; break; }
          if (!dup)
            arcPaths.Add(reg.ArcPaths[i]);
        }
        reg.ArcPaths = arcPaths;
      }
      reg.Save();
    }

    info = di;
    return S_OK;
  }

  if (dialog.exec() != QDialog::Accepted)
    return E_ABORT;

  info = dialog.Info;
  return S_OK;
}


// === QtUpdateGUI : mirror of the original UpdateGUI() free function ===========
// GUI.cpp Main2()'s update branch hands us the REAL parsed CUpdateOptions (with
// the -m... method props in MethodMode.Properties and the -t... format in
// MethodMode.Type). We mirror UpdateGUI.cpp's body: optionally run the dialog
// stage (seed a CInfo from `options`, exec the dialog, translate back), else use
// the parsed options directly, then run QtThreadUpdating.
HRESULT QtUpdateGUI(
    CCodecs *codecs,
    const CObjectVector<COpenType> &formatIndices,
    const UString &cmdArcPath,
    NWildcard::CCensor &censor,
    CUpdateOptions &options,
    bool showDialog,
    bool disableUserQuestions,
    QtUpdateCallback *updateCallback,
    QtPasswordPrompt *passwordPrompt,
    QWidget *parent)
{
  bool needSetPath = true;

  if (showDialog)
  {
    // ---- dialog stage (mirror UpdateGUI.cpp's ShowDialog seeding) ------------
    // Seed an NCompressDialog::CInfo from the parsed CUpdateOptions, exactly the
    // bits ShowDialog() copies out of `options` before exec()'ing the dialog:
    // the preselected format, the path mode, the password, and the default
    // archive name (base, no extension).
    NCompressDialog::CInfo info;

    if (options.MethodMode.Type_Defined)
      info.FormatIndex = options.MethodMode.Type.FormatIndex;

    info.PathMode = options.PathMode;
    info.SFXMode = options.SfxMode;
    info.OpenShareForWrite = options.OpenShareForWrite;
    // G.7b: seed the dialog's Delete checkbox from a parsed `-sdel` switch
    // (UpdateGUI.cpp:430 di.DeleteAfterCompressing = options.DeleteAfterCompressing).
    info.DeleteAfterCompressing = options.DeleteAfterCompressing;

    // C.6: seed the dialog's Volume field from a parsed `-v` switch so the dialog
    // shows (and re-emits) the split sizes the user asked for on the command line.
    // ShowDialog (UpdateGUI.cpp) does the same via di.VolumeSizes = options.VolumesSizes
    // before exec()'ing the dialog. The dialog's OnOK then re-copies it back.
    info.VolumeSizes = options.VolumesSizes;

    if (updateCallback->PasswordIsDefined)
      info.Password = updateCallback->Password;

    // di.ArcPath = options.ArchivePath.GetPathWithoutExt() — but the parser does
    // not pre-populate ArchivePath here (that happens in InitFormatIndex/SetArcPath
    // on the needSetPath path). The unified main passes the raw archive name as
    // cmdArcPath, which the dialog uses as the default base name.
    {
      UString base = cmdArcPath;
      const int dot = base.ReverseFind_Dot();
      const int sep = base.ReverseFind_PathSepar();
      if (dot > sep + 1)
        base.DeleteFrom(dot);
      info.ArcPath = base;
    }

    // G.7d: detect the single-file case exactly as ShowDialog (UpdateGUI.cpp:334-376):
    // oneFile is true only when the censor has exactly one included path that resolves
    // to a regular file (not a directory). For one file di.KeepName = false (the
    // source's extension is stripped so foo.txt -> foo.gz); for >1 file / a directory
    // di.KeepName = true (the base name is kept whole). dialog.OriginalFileName is the
    // resolved leaf name, used by the KeepName-format naming branch.
    bool oneFile = false;
    UString originalFileName;
    {
      NFind::CFileInfo fileInfo;
      const CObjectVector<NWildcard::CCensorPath> &cps = censor.CensorPaths;
      if (cps.Size() > 0)
      {
        const NWildcard::CCensorPath &cp = cps[0];
        if (cp.Include)
        {
          if (fileInfo.Find(us2fs(cp.Path)))
          {
            originalFileName = fs2us(fileInfo.Name);
            if (cps.Size() == 1)
              oneFile = !fileInfo.IsDir();
          }
        }
      }
    }
    info.KeepName = !oneFile;

    const HRESULT dres = QtCompressGUI_ShowDialog(codecs, info, disableUserQuestions, parent, originalFileName);
    if (dres != S_OK)
      return dres; // E_ABORT on cancel (mirror ShowDialog's "return E_ABORT")

    // ---- translate the filled CInfo back into `options` + run the worker -----
    // QtCompressGUI() replays the second half of ShowDialog() (the CInfo ->
    // CUpdateOptions + CProperty translation) and runs QtThreadUpdating with
    // needSetPath = false (path/format already parsed into options). It uses
    // `cmdArcPath = info.ArcPath` internally.
    return QtCompressGUI(codecs, info, censor, options, updateCallback,
        passwordPrompt, disableUserQuestions, parent);
  }

  // ---- no dialog: run the worker directly with the parsed options ----------
  // needSetPath = true => UpdateArchive() calls InitFormatIndex()/SetArcPath()
  // over `formatIndices`, exactly like the console path. The real -m.../-t.../-p
  // switches reach the engine untouched via options.MethodMode.
  CObjectVector<COpenType> types = formatIndices;

  QtThreadUpdating tu;
  tu.needSetPath = needSetPath;
  tu.codecs = codecs;
  tu.formatIndices = &types;
  tu.cmdArcPath = &cmdArcPath;

  tu.UpdateCallback = updateCallback;
  tu.UpdateCallback->PasswordPrompt = passwordPrompt;
  tu.UpdateCallback->ProgressDialog = nullptr; // set in ProcessVirt
  tu.UpdateCallback->Init();

  tu.WildcardCensor = &censor;
  tu.Options = &options;
  tu.DisableUserQuestions = disableUserQuestions;

  const UString title("Compressing");
  return tu.Create(title, parent);
}
