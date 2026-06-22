// QtBenchmarkDialog.h
// ----------------------------------------------------------------------------
// G.10a : Qt/Linux mirror of GUI/BenchmarkDialog.{h,cpp}'s CBenchmarkDialog.
//
// The FM Tools->Benchmark (MyLoadMenu.cpp MyBenchmark(false)=normal /
// MyBenchmark(true)=total-mode IDM_BENCHMARK2) opens this dialog. The CLI path
// (7zqt b == GUI.cpp Main2() kBenchmark) shares the same engine driver via the
// static RunBenchmarkConsole() helper (mirrors Console/BenchCon.cpp's BenchCon).
//
// FIDELITY: the structure tracks BenchmarkDialog.cpp:
//   * CBenchProgressSync / CSyncData / CTotalBenchRes2 / CBenchPassResult — the
//     worker<->GUI shared state, lifted byte-for-byte (portable: CCriticalSection,
//     CRecordVector, the engine's CBenchInfo / CTotalBenchRes).
//   * CBenchCallback / CBenchCallback2 / CFreqCallback — the engine's
//     IBenchCallback / IBenchPrintCallback / IBenchFreqCallback consumers, lifted
//     as-is; the only port change is PostMsg(k_Message_Finished) -> a queued
//     QMetaObject::invokeMethod onto the GUI-thread dialog.
//   * the benchmark thread loop (CThreadBenchmark::Process) runs on a QThread
//     subclass; Bench() is called there exactly as the original does, off the GUI
//     thread, so the UI stays responsive.
//   * OnInit / StartBenchmark / UpdateGui / PrintBenchRes — the combos
//     (dictionary / threads / passes), the per-pass live results table (Speed /
//     Rating / Usage / R/U for compress + decompress + total), and the 1 s timer.
//
// The engine Bench()/CBenchInfo/CTotalBenchRes are compiled into the engine and
// linked WHOLE_ARCHIVE; this TU only consumes them. Bench.cpp is NOT edited.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_BENCHMARK_DIALOG_H
#define ZIP7_INC_QT_FM_BENCHMARK_DIALOG_H

#include <cstdio>

#include "../../../../Common/MyString.h"
#include "../../../../Common/MyVector.h"

#include "../../../../Windows/Synchronization.h"

#include "../../Common/Property.h"
#include "../../Common/Bench.h"   // CBenchInfo / CTotalBenchRes / Bench() / I*Callback

#include <QtWidgets/QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QThread;
class QTimer;
QT_END_NAMESPACE

// Mirror of GUI/BenchmarkDialog.h:9 (the default pass count GUI.cpp Main2() uses
// when no -i<N> was given). Defined here so consumers needn't pull the Win32-
// flavored BenchmarkDialog.h (its Benchmark() declaration takes an HWND).
const UInt32 k_NumBenchIterations_Default = 10;


// === RunBenchmarkConsole : the CLI path (mirror BenchCon.cpp::BenchCon) =======
// Drives the engine Bench() in TotalMode with a print-to-FILE callback and prints
// the textual report (the same lines `7zz b` emits). Returns the Bench() HRESULT.
// Used by main_7zqt.cpp's kBenchmark branch (GUI.cpp Main2() calls Benchmark();
// the console-equivalent driver is the faithful headless analogue).
HRESULT RunBenchmarkConsole(
    DECL_EXTERNAL_CODECS_LOC_VARS
    const CObjectVector<CProperty> &props, UInt32 numIterations, FILE *f);


// === worker<->GUI shared state (lifted from BenchmarkDialog.cpp) =============

struct CBenchPassResult2
{
  CTotalBenchRes Enc;
  CTotalBenchRes Dec;
};


struct CTotalBenchRes2: public CTotalBenchRes
{
  UInt64 UnpackSize;

  void Init()
  {
    CTotalBenchRes::Init();
    UnpackSize = 0;
  }

  void SetFrom_BenchInfo(const CBenchInfo &info)
  {
    NumIterations2 = 1;
    Generate_From_BenchInfo(info);
    UnpackSize = info.Get_UnpackSize_Full();
  }

  void Update_With_Res2(const CTotalBenchRes2 &r)
  {
    Update_With_Res(r);
    UnpackSize += r.UnpackSize;
  }
};


struct CSyncData
{
  UInt32 NumPasses_Finished;
  int RatingVector_DeletedIndex;

  bool BenchWasFinished;
  bool NeedPrint_Freq;
  bool NeedPrint_RatingVector;
  bool NeedPrint_Enc_1;
  bool NeedPrint_Enc;
  bool NeedPrint_Dec_1;
  bool NeedPrint_Dec;
  bool NeedPrint_Tot;

  CTotalBenchRes2 Enc_BenchRes_1;
  CTotalBenchRes2 Enc_BenchRes;

  CTotalBenchRes2 Dec_BenchRes_1;
  CTotalBenchRes2 Dec_BenchRes;

  void Init();
};


struct CBenchProgressSync
{
  bool Exit;
  bool TextWasChanged;

  UInt32 NumThreads;
  UInt64 DictSize;
  UInt32 NumPasses_Limit;
  int Level;

  AString Text;

  HRESULT BenchFinish_Task_HRESULT;
  HRESULT BenchFinish_Thread_HRESULT;

  UInt32 NumFreqThreadsPrev;
  UString FreqString_Sync;
  UString FreqString_GUI;

  CRecordVector<CBenchPassResult2> RatingVector;
  CSyncData sd;

  NWindows::NSynchronization::CCriticalSection CS;

  CBenchProgressSync()
  {
    NumPasses_Limit = 1;
  }

  void Init();

  void SendExit()
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(CS);
    Exit = true;
  }
};


class QtBenchmarkDialog : public QDialog
{
  Q_OBJECT

public:
  explicit QtBenchmarkDialog(QWidget *parent = nullptr);
  ~QtBenchmarkDialog() override;

  // === public data : MIRRORS CBenchmarkDialog (BenchmarkDialog.cpp) ===========
  bool TotalMode = false;                 // IDM_BENCHMARK2 (the "Total" mode)
  CObjectVector<CProperty> Props;         // benchmark props (m=*, dict, level, mt)
  CBenchProgressSync Sync;                // worker<->GUI shared state

  // Called by Benchmark() AFTER setting TotalMode/Props/Sync (mirrors OnInit's
  // tail RestartBenchmark()). Fills the combos and kicks the first pass.
  void initFromState();

  // Worker-thread entry: lifts CThreadBenchmark::Process. Runs on _thread.
  void runBenchmarkThread();

  // Posted from the worker callbacks (queued onto the GUI thread) — mirrors
  // PostMsg(k_Message_Finished, wparam). wparam==0 means "thread finished".
  Q_INVOKABLE void onFinishMessage(int wparam);

  // === the GUI Benchmark() entry (mirror BenchmarkDialog.cpp::Benchmark) ======
  // Parses props (m=* total mode, dict, level, mt), constructs+shows the modal
  // dialog. Returns S_OK (the original returns S_OK after Create()).
  static HRESULT Benchmark(const CObjectVector<CProperty> &props,
      UInt32 numIterations, bool totalMode, QWidget *parent);

private slots:
  void onSelectionChanged();     // CBN_SELCHANGE -> RestartBenchmark()
  void onStop();                 // IDB_STOP -> OnStopButton()
  void onRestart();              // IDB_RESTART -> RestartBenchmark()
  void onTimer();                // OnTimer -> UpdateGui()

protected:
  void closeEvent(QCloseEvent *e) override;   // OnCancel()

private:
  void buildUi();
  void StartBenchmark();
  void RestartBenchmark();
  void Disable_Stop_Button();
  void OnStopButton();
  void UpdateGui();

  UInt32 GetNumberOfThreads() const;
  size_t OnChangeDictionary();

  void PrintBenchRes(const CTotalBenchRes2 &info, QLabel * const lbl[5]);
  void PrintTime();

  void InitSyncNew()
  {
    NumPasses_Finished_Prev = (UInt32)(Int32)-1;
    ElapsedSec_Prev.Empty();
    Sync.Init();
  }

  // --- widgets (mirror the IDC_BENCH_* / IDT_BENCH_* controls) ---
  QComboBox      *_dictionary = nullptr;
  QComboBox      *_numThreads = nullptr;
  QComboBox      *_numPasses  = nullptr;
  QLabel         *_memoryVal  = nullptr;
  QLabel         *_elapsedVal = nullptr;
  QLabel         *_passesVal  = nullptr;
  QLabel         *_errorMsg   = nullptr;
  QLabel         *_sysInfo    = nullptr;
  QPushButton    *_stopBtn    = nullptr;
  QPushButton    *_restartBtn = nullptr;
  QPlainTextEdit *_log        = nullptr;   // the per-pass rating table / total log

  // Result grid : [row][col]. row 0 = Current, row 1 = Resulting.
  // Compress cols : Usage, Speed, R/U, Rating, Size.
  QLabel *_enc[2][5] = {};
  QLabel *_dec[2][5] = {};
  QLabel *_tot[3]    = {};   // total Usage / Rating / R/U

  // --- worker thread + state (mirror CBenchmarkDialog) ---
  QThread *_thread = nullptr;
  QTimer  *_timer  = nullptr;

  bool _finishTime_WasSet = false;
  bool WasStopped_in_GUI  = false;
  bool ExitWasAsked_in_GUI = false;
  bool NeedRestart        = false;
  bool RamSize_Defined    = false;

  size_t RamSize = 0;
  size_t RamSize_Limit = 0;

  UInt32 _startTime = 0;
  UInt32 _finishTime = 0;

  UInt32 NumPasses_Finished_Prev = (UInt32)(Int32)-1;
  UString ElapsedSec_Prev;

  bool IsMemoryUsageOK(UInt64 memUsage) const
    { return memUsage + (1 << 20) <= RamSize_Limit; }
};

#endif
