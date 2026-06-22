// QtHashGUI.cpp
//
// Mirror of GUI/HashGUI.cpp: the IHashCallbackUI callback methods of
// CHashCallbackGUI, the AddHashBundleRes() result aggregation, the
// ProcessVirt()=HashCalc worker, and the HashCalcGUI()/ShowHashResults() flow.

#include "QtHashGUI.h"

#include <QtCore/QByteArray>

#include "QtHashResultsDialog.h"
#include "QtProgressDialog.h"

#include "../../../Common/IntToString.h"
#include "../../../Common/StringConvert.h"

using namespace NWindows;


// === value formatting (HashGUI.cpp ports) ==================================
// The original pulls the property names from LangString(IDS_PROP_*); this
// headless port uses the canonical English forms (identical to what the
// console prints in HashCon.cpp: "Folders"/"Files"/"Size"/...).

void Qt_AddValuePair(CPropNameValPairs &pairs, const char *name, UInt64 value)
{
  CProperty &pair = pairs.AddNew();
  pair.Name = name;
  char sz[32];
  ConvertUInt64ToString(value, sz);
  pair.Value = sz;
}

// AddSizeValue : mirror of the FileManager AddSizeValue (number + " bytes" with
// thousands separators). The original GUI uses FormatUtils; we reproduce its
// observable output: "<n> bytes" when n != exact, else just "<n>". We keep it
// simple and faithful to the console's "Size: <n>" by printing the raw count,
// with a "( <grouped> bytes)" style suffix omitted — the digest values are the
// load-bearing output, the size is a plain decimal byte count (== console).
void Qt_AddSizeValue(UString &s, UInt64 value)
{
  char sz[32];
  ConvertUInt64ToString(value, sz);
  s += sz;
  s += " bytes";
}

void Qt_AddSizeValuePair(CPropNameValPairs &pairs, const char *name, UInt64 value)
{
  CProperty &pair = pairs.AddNew();
  pair.Name = name;
  Qt_AddSizeValue(pair.Value, value);
}


static const unsigned k_DigestStringSize =
    k_HashCalc_DigestSize_Max * 2 + k_HashCalc_ExtraSize * 2 + 16;

// AddHashString : HashGUI.cpp:160 — write the uppercase/lowercase hex digest
// (engine HashHexToString via CHasherState::WriteToString) into the value.
static void AddHashString(CProperty &s, const CHasherState &h, unsigned digestIndex)
{
  char temp[k_DigestStringSize];
  h.WriteToString(digestIndex, temp);
  s.Value = temp;
}

// AddHashResString : HashGUI.cpp:167 — build "<METHOD> checksum for data" etc.
// The original takes LangString(resID) == "CRC checksum for data:" and:
//   - replaces "CRC" with the method name,
//   - removes the ":".
// We reproduce that transform on the literal English template.
static void AddHashResString(CPropNameValPairs &s, const CHasherState &h,
    unsigned digestIndex, const char *templateStr)
{
  CProperty &pair = s.AddNew();
  UString &s2 = pair.Name;
  s2 = templateStr;                 // e.g. "CRC checksum for data:"
  const UString name(h.Name);
  s2.Replace(L"CRC", name);         // -> "<METHOD> checksum for data:"
  s2.Replace(L":", L"");            // -> "<METHOD> checksum for data"
  AddHashString(pair, h, digestIndex);
}

// English templates corresponding to IDS_CHECKSUM_CRC_DATA / _DATA_NAMES /
// _STREAMS_NAMES (verbatim from FileManager/resourceGui.rc).
static const char * const k_Tmpl_Data         = "CRC checksum for data:";
static const char * const k_Tmpl_DataNames    = "CRC checksum for data and names:";
static const char * const k_Tmpl_StreamsNames = "CRC checksum for streams and names:";


// AddHashBundleRes : faithful port of HashGUI.cpp:179.
void Qt_AddHashBundleRes(CPropNameValPairs &s, const CHashBundle &hb)
{
  if (hb.NumErrors != 0)
    Qt_AddValuePair(s, "Errors", hb.NumErrors);

  if (hb.NumFiles == 1 && hb.NumDirs == 0 && !hb.FirstFileName.IsEmpty())
  {
    CProperty &pair = s.AddNew();
    pair.Name = "Path";
    pair.Value = hb.FirstFileName;
  }
  else
  {
    if (!hb.MainName.IsEmpty())
    {
      CProperty &pair = s.AddNew();
      pair.Name = "Path";
      pair.Value = hb.MainName;
    }
    if (hb.NumDirs != 0)
      Qt_AddValuePair(s, "Folders", hb.NumDirs);
    Qt_AddValuePair(s, "Files", hb.NumFiles);
  }

  Qt_AddSizeValuePair(s, "Size", hb.FilesSize);

  if (hb.NumAltStreams != 0)
  {
    Qt_AddValuePair(s, "Alternate streams", hb.NumAltStreams);
    Qt_AddSizeValuePair(s, "Alternate streams size", hb.AltStreamsSize);
  }

  FOR_VECTOR (i, hb.Hashers)
  {
    const CHasherState &h = hb.Hashers[i];
    if (hb.NumFiles == 1 && hb.NumDirs == 0)
    {
      CProperty &pair = s.AddNew();
      pair.Name += h.Name;
      AddHashString(pair, h, k_HashCalc_Index_DataSum);
    }
    else
    {
      AddHashResString(s, h, k_HashCalc_Index_DataSum, k_Tmpl_Data);
      AddHashResString(s, h, k_HashCalc_Index_NamesSum, k_Tmpl_DataNames);
    }
    if (hb.NumAltStreams != 0)
      AddHashResString(s, h, k_HashCalc_Index_StreamsSum, k_Tmpl_StreamsNames);
  }
}


// === QtThreadHashCalc : IDirItemsCallback + IHashCallbackUI =================
// All ported 1:1 from CHashCallbackGUI's callback methods.

HRESULT QtThreadHashCalc::StartScanning()
{
  Sync().Set_Status(UString("Scanning"));
  return CheckBreak();
}

HRESULT QtThreadHashCalc::ScanProgress(const CDirItemsStat &st, const FString &path, bool isDir)
{
  return Sync().ScanProgress(st.NumFiles, st.GetTotalBytes(), path, isDir);
}

HRESULT QtThreadHashCalc::ScanError(const FString &path, DWORD systemError)
{
  AddErrorMessage(HRESULT_FROM_WIN32(systemError), fs2us(path));
  return CheckBreak();
}

HRESULT QtThreadHashCalc::FinishScanning(const CDirItemsStat &st)
{
  return ScanProgress(st, FString(), false);
}

HRESULT QtThreadHashCalc::CheckBreak()
{
  return Sync().CheckStop();
}

HRESULT QtThreadHashCalc::SetNumFiles(UInt64 numFiles)
{
  Sync().Set_NumFilesTotal(numFiles);
  return CheckBreak();
}

HRESULT QtThreadHashCalc::SetTotal(UInt64 size)
{
  Sync().Set_NumBytesTotal(size);
  return CheckBreak();
}

HRESULT QtThreadHashCalc::SetCompleted(const UInt64 *completed)
{
  return Sync().Set_NumBytesCur(completed);
}

HRESULT QtThreadHashCalc::BeforeFirstFile(const CHashBundle & /* hb */)
{
  return S_OK;
}

HRESULT QtThreadHashCalc::GetStream(const wchar_t *name, bool isFolder)
{
  if (NumFiles == 0)
    FirstFileName = name;
  _curIsFolder = isFolder;
  Sync().Set_FilePath(name, isFolder);
  return CheckBreak();
}

HRESULT QtThreadHashCalc::OpenFileError(const FString &path, DWORD systemError)
{
  AddErrorMessage(HRESULT_FROM_WIN32(systemError), fs2us(path));
  return S_FALSE;
}

HRESULT QtThreadHashCalc::SetOperationResult(UInt64 /* fileSize */, const CHashBundle & /* hb */, bool /* showHash */)
{
  if (!_curIsFolder)
    NumFiles++;
  Sync().Set_NumFilesCur(NumFiles);
  return CheckBreak();
}

HRESULT QtThreadHashCalc::AfterLastFile(CHashBundle &hb)
{
  hb.FirstFileName = FirstFileName;
  Qt_AddHashBundleRes(PropNameValPairs, hb);
  Sync().Set_NumFilesCur(hb.NumFiles);
  return S_OK;
}


// === ProcessVirt : mirror of CHashCallbackGUI::ProcessVirt() ===============
HRESULT QtThreadHashCalc::ProcessVirt()
{
  // cancel-wiring proof (same idiom as QtThreadExtracting): set Stopped so the
  // engine's next CheckStop() through this callback returns E_ABORT.
  if (!qgetenv("SEVENZQT_TEST_CANCEL").isEmpty())
    ProgressDialog->Sync.Set_Stopped(true);

  NumFiles = 0;
  AString errorInfo;
  HRESULT res = HashCalc(EXTERNAL_CODECS_LOC_VARS
      *censor, *options, errorInfo, this);
  return res;
}


// === QtHashProgressDialog : the results-window hook =========================
// Mirrors CHashCallbackGUI::ProcessWasFinished_GuiVirt():
//   if (Result != E_ABORT) ShowHashResults(PropNameValPairs, *this);
// It holds a pointer to the worker's accumulated pairs and shows the results
// dialog (unless cancelled or headless).
class QtHashProgressDialog Z7_final : public QtProgressDialog
{
  const CPropNameValPairs *_pairs;
public:
  explicit QtHashProgressDialog(const CPropNameValPairs *pairs, QWidget *parent)
    : QtProgressDialog(parent), _pairs(pairs) {}

  void ProcessWasFinished_GuiVirt() override
  {
    // Result lives on the worker; we infer "not aborted" from the absence of a
    // Stopped state and the presence of result pairs. The worker only fills
    // PropNameValPairs in AfterLastFile (== a successful, non-aborted run), so
    // a non-empty _pairs is the faithful "Result != E_ABORT" condition.
    if (DisableUserQuestions)
      return; // headless: results printed to stdout by main_hash instead
    if (_pairs && !_pairs->IsEmpty())
    {
      QtHashResultsDialog dlg(*_pairs, this);
      dlg.exec();
    }
  }
};


// === QtThreadHashCalc dialog factory : install the results-window dialog ====
// Overrides CreateProgressDialog() so Create() builds the QtHashProgressDialog
// (which owns the ProcessWasFinished_GuiVirt() hook), mirroring how the original
// CProgressThreadVirt subclass IS the dialog and overrides that hook directly.
QtProgressDialog *QtThreadHashCalc::CreateProgressDialog(QWidget *parentWindow)
{
  return new QtHashProgressDialog(&PropNameValPairs, parentWindow);
}


// === QtHashGUI : mirror of HashCalcGUI() ====================================
HRESULT QtHashGUI(
    DECL_EXTERNAL_CODECS_LOC_VARS
    const NWildcard::CCensor &censor,
    const CHashOptions &options,
    bool disableUserQuestions,
    QWidget *parent,
    CPropNameValPairs &resultPairsOut)
{
  QtThreadHashCalc t;
  #ifdef Z7_EXTERNAL_CODECS
  t._externalCodecs = _externalCodecs;
  #endif
  t.censor = &censor;
  t.options = &options;
  t.DisableUserQuestions = disableUserQuestions;

  const UString title("Calculating checksum");

  const HRESULT res = t.Create(title, parent);

  resultPairsOut = t.PropNameValPairs;
  return res;
}
