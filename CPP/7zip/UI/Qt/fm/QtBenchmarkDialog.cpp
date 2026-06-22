// QtBenchmarkDialog.cpp
// ----------------------------------------------------------------------------
// G.10a : Qt/Linux mirror of GUI/BenchmarkDialog.cpp's CBenchmarkDialog +
// Benchmark(), and Console/BenchCon.cpp's BenchCon() (the CLI path).
//
// See QtBenchmarkDialog.h. The worker<->GUI sync, the engine callbacks, and the
// benchmark thread loop are lifted from BenchmarkDialog.cpp essentially verbatim;
// only the Win32 dialog plumbing (CModalDialog, PostMsg, SetItemText, combos via
// NControl) is replaced with Qt widgets + a queued QMetaObject::invokeMethod for
// the cross-thread "finished" notification. Bench.cpp is NOT edited.
// ----------------------------------------------------------------------------

#include "QtBenchmarkDialog.h"

#include "../../../../../C/CpuArch.h"

#include "../../../../Common/Defs.h"
#include "../../../../Common/IntToString.h"
#include "../../../../Common/MyException.h"   // CSystemException
#include "../../../../Common/StringConvert.h"
#include "../../../../Common/StringToInt.h"

#include "../../../../Windows/System.h"
#include "../../../../Windows/SystemInfo.h"

#include "../../../Common/MethodProps.h"

#include "../../../MyVersion.h"

#include "../QtLang.h"                          // FmLang(IDS_*, "English")
#include "../../GUI/BenchmarkDialogRes.h"       // IDT_BENCH_* / IDM_BENCHMARK langIDs

#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>

using namespace NWindows;

static const int kTimerElapse = 1000; // ms (BenchmarkDialog.cpp kTimerElapse)

static const unsigned kRatingVector_NumBundlesMax = 20;

// PostMsg WPARAM values (BenchmarkDialog.cpp My_Message_WPARAM). The GUI uses
// only "thread finished" (0) vs "an intermediate update happened" (non-0).
enum { k_WPARM_Thread_Finished = 0, k_WPARM_Iter_Finished, k_WPARM_Enc1_Finished };

static const wchar_t * const kProcessingString = L"...";
static const wchar_t * const kGB = L" GB";
static const wchar_t * const kMB = L" MB";
static const wchar_t * const kKB = L" KB";
static const wchar_t * const kKBs = L" KB/s";

static const unsigned kMinDicLogSize = 18;
static const UInt32 kMinDicSize = (UInt32)1 << kMinDicLogSize;
static const size_t kMaxDicSize = (size_t)1 << (22 + sizeof(size_t) / 4 * 5);


UString HResultToMessage(HRESULT errorCode);   // QtProgressThreadVirt.cpp


// ====================================================================
//   CSyncData / CBenchProgressSync  (BenchmarkDialog.cpp lifts)
// ====================================================================

void CSyncData::Init()
{
  NumPasses_Finished = 0;
  Enc_BenchRes.Init();
  Enc_BenchRes_1.Init();
  Dec_BenchRes.Init();
  Dec_BenchRes_1.Init();
  RatingVector_DeletedIndex = -1;
  BenchWasFinished =
    NeedPrint_Freq =
    NeedPrint_RatingVector =
    NeedPrint_Enc_1 =
    NeedPrint_Enc   =
    NeedPrint_Dec_1 =
    NeedPrint_Dec   =
    NeedPrint_Tot   = false;
}

void CBenchProgressSync::Init()
{
  Exit = false;
  BenchFinish_Task_HRESULT = S_OK;
  BenchFinish_Thread_HRESULT = S_OK;
  sd.Init();
  RatingVector.Clear();
  NumFreqThreadsPrev = 0;
  FreqString_Sync.Empty();
  FreqString_GUI.Empty();
  Text.Empty();
  TextWasChanged = true;
}


// ====================================================================
//   small number formatters  (BenchmarkDialog.cpp lifts)
// ====================================================================

#define UINT_TO_STR_3(s, val) { \
  s[0] = (wchar_t)('0' + (val) / 100); \
  s[1] = (wchar_t)('0' + (val) % 100 / 10); \
  s[2] = (wchar_t)('0' + (val) % 10); \
  s += 3; s[0] = 0; }

static wchar_t *NumberToDot3(UInt64 val, wchar_t *s)
{
  s = ConvertUInt64ToString(val / 1000, s);
  const UInt32 rem = (UInt32)(val % 1000);
  *s++ = '.';
  UINT_TO_STR_3(s, rem)
  return s;
}

static UInt64 GetMips(UInt64 ips) { return (ips + 500000) / 1000000; }

static UInt64 GetUsagePercents(UInt64 usage) { return Benchmark_GetUsage_Percents(usage); }

static UInt32 GetRating(const CTotalBenchRes &info)
{
  UInt64 numIter = info.NumIterations2;
  if (numIter == 0)
    numIter = 1000000;
  const UInt64 rating64 = GetMips(info.Rating / numIter);
  UInt32 rating32 = (UInt32)rating64;
  if (rating32 != rating64)
    rating32 = (UInt32)(Int32)-1;
  return rating32;
}

static void AddUsageString(UString &s, const CTotalBenchRes &info)
{
  UInt64 numIter = info.NumIterations2;
  if (numIter == 0)
    numIter = 1000000;
  UInt64 usage = GetUsagePercents(info.Usage / numIter);
  wchar_t w[32];
  wchar_t *p = ConvertUInt64ToString(usage, w);
  p[0] = '%';
  p[1] = 0;
  unsigned len = (unsigned)(size_t)(p - w);
  while (len < 5) { s.Add_Space(); len++; }
  s += w;
}

static void Add_Dot3String(UString &s, UInt64 val)
{
  wchar_t temp[32];
  NumberToDot3(val, temp);
  s += temp;
}

static void AddRatingString(UString &s, const CTotalBenchRes &info)
{
  Add_Dot3String(s, GetRating(info));
}

static void AddRatingsLine(UString &s, const CTotalBenchRes &enc, const CTotalBenchRes &dec)
{
  AddRatingString(s, enc);
  s += "  ";
  AddRatingString(s, dec);
  CTotalBenchRes tot_BenchRes;
  tot_BenchRes.SetSum(enc, dec);
  s += "  ";
  AddRatingString(s, tot_BenchRes);
  s.Add_Space(); AddUsageString(s, tot_BenchRes);
}

// Qt label setters mirroring SetItemText / SetItemText_Number.
static QString UStringToQ(const UString &s) { return QString::fromWCharArray(s.Ptr(), (int)s.Len()); }

static void SetLabel(QLabel *l, const wchar_t *s);
static void SetLabel(QLabel *l, const UString &s);

static QString NumberWithPost(UInt64 val, const wchar_t *post)
{
  wchar_t s[64];
  ConvertUInt64ToString(val, s);
  UString u(s);
  if (post)
    u += post;
  return UStringToQ(u);
}


// ====================================================================
//   QtBenchmarkDialog
// ====================================================================

QtBenchmarkDialog::QtBenchmarkDialog(QWidget *parent)
  : QDialog(parent)
{
  setWindowTitle(QStringLiteral("7-Zip"));
  buildUi();
}

QtBenchmarkDialog::~QtBenchmarkDialog()
{
  // BenchmarkDialog.cpp ~CBenchmarkDialog : ensure the worker thread is joined.
  if (_thread)
  {
    Sync.SendExit();
    _thread->wait();
    delete _thread;
    _thread = nullptr;
  }
}


// === label helpers ===========================================================
static void SetLabel(QLabel *l, const wchar_t *s) { if (l) l->setText(QString::fromWCharArray(s)); }
static void SetLabel(QLabel *l, const UString &s) { if (l) l->setText(UStringToQ(s)); }


// === buildUi : the dialog layout (mirror IDD_BENCH / IDD_BENCH_TOTAL) =========
void QtBenchmarkDialog::buildUi()
{
  QVBoxLayout *root = new QVBoxLayout(this);

  // ---- top controls row : Dictionary / Threads / Passes + Memory + Elapsed ---
  QGridLayout *cfg = new QGridLayout();
  int r = 0;

  cfg->addWidget(new QLabel(FmLang(IDT_BENCH_DICTIONARY, QStringLiteral("Dictionary")), this), r, 0);
  _dictionary = new QComboBox(this);
  cfg->addWidget(_dictionary, r, 1);

  cfg->addWidget(new QLabel(FmLang(IDT_BENCH_MEMORY, QStringLiteral("Memory Usage:")), this), r, 2);
  _memoryVal = new QLabel(this);
  cfg->addWidget(_memoryVal, r, 3);
  r++;

  cfg->addWidget(new QLabel(FmLang(IDT_BENCH_NUM_THREADS, QStringLiteral("Number of CPU threads")), this), r, 0);
  _numThreads = new QComboBox(this);
  cfg->addWidget(_numThreads, r, 1);

  cfg->addWidget(new QLabel(FmLang(IDT_BENCH_PASSES, QStringLiteral("Number of passes:")), this), r, 2);
  _numPasses = new QComboBox(this);
  cfg->addWidget(_numPasses, r, 3);
  r++;

  root->addLayout(cfg);

  // ---- results table (IDG_BENCH_COMPRESSING / IDG_BENCH_DECOMPRESSING) -------
  // Columns mirror the dialog : <label> | Speed | Rating | Usage | R/U  (+ size).
  // Two rows per group : Current (…1) and Resulting (…2).
  QGridLayout *tbl = new QGridLayout();
  int row = 0;

  // header row
  tbl->addWidget(new QLabel(QString(), this), row, 0);
  tbl->addWidget(new QLabel(FmLang(IDT_BENCH_SPEED, QStringLiteral("Speed")), this), row, 1);
  tbl->addWidget(new QLabel(FmLang(IDT_BENCH_RATING_LABEL, QStringLiteral("Rating")), this), row, 2);
  tbl->addWidget(new QLabel(FmLang(IDT_BENCH_USAGE_LABEL, QStringLiteral("CPU Usage")), this), row, 3);
  tbl->addWidget(new QLabel(FmLang(IDT_BENCH_RPU_LABEL, QStringLiteral("Rating / Usage")), this), row, 4);
  row++;

  const QString curTxt = FmLang(IDT_BENCH_CURRENT, QStringLiteral("Current"));
  const QString resTxt = FmLang(IDT_BENCH_RESULTING, QStringLiteral("Resulting"));

  // Compressing group
  tbl->addWidget(new QLabel(FmLang(IDG_BENCH_COMPRESSING, QStringLiteral("Compressing")), this), row, 0, 1, 5);
  row++;
  // grid cell order : [*][0]=Usage [*][1]=Speed [*][2]=R/U [*][3]=Rating [*][4]=Size
  for (int rr = 0; rr < 2; rr++)
  {
    tbl->addWidget(new QLabel(rr == 0 ? curTxt : resTxt, this), row, 0);
    QLabel *sp = new QLabel(this); QLabel *ra = new QLabel(this);
    QLabel *us = new QLabel(this); QLabel *ru = new QLabel(this);
    _enc[rr][1] = sp; _enc[rr][3] = ra; _enc[rr][0] = us; _enc[rr][2] = ru;
    _enc[rr][4] = new QLabel(this);   // size (not shown in a column, tracked only)
    tbl->addWidget(sp, row, 1);
    tbl->addWidget(ra, row, 2);
    tbl->addWidget(us, row, 3);
    tbl->addWidget(ru, row, 4);
    row++;
  }

  // Decompressing group
  tbl->addWidget(new QLabel(FmLang(IDG_BENCH_DECOMPRESSING, QStringLiteral("Decompressing")), this), row, 0, 1, 5);
  row++;
  for (int rr = 0; rr < 2; rr++)
  {
    tbl->addWidget(new QLabel(rr == 0 ? curTxt : resTxt, this), row, 0);
    QLabel *sp = new QLabel(this); QLabel *ra = new QLabel(this);
    QLabel *us = new QLabel(this); QLabel *ru = new QLabel(this);
    _dec[rr][1] = sp; _dec[rr][3] = ra; _dec[rr][0] = us; _dec[rr][2] = ru;
    _dec[rr][4] = new QLabel(this);
    tbl->addWidget(sp, row, 1);
    tbl->addWidget(ra, row, 2);
    tbl->addWidget(us, row, 3);
    tbl->addWidget(ru, row, 4);
    row++;
  }

  // Total rating row (IDG_BENCH_TOTAL_RATING : Usage / Rating / R/U)
  tbl->addWidget(new QLabel(FmLang(IDG_BENCH_TOTAL_RATING, QStringLiteral("Total Rating")), this), row, 0);
  _tot[1] = new QLabel(this);   // Rating  -> col 2
  _tot[2] = new QLabel(this);   // R/U     -> col 4
  _tot[0] = new QLabel(this);   // Usage   -> col 3
  tbl->addWidget(_tot[1], row, 2);
  tbl->addWidget(_tot[0], row, 3);
  tbl->addWidget(_tot[2], row, 4);
  row++;

  root->addLayout(tbl);

  // ---- passes / elapsed / error message --------------------------------------
  QGridLayout *st = new QGridLayout();
  st->addWidget(new QLabel(FmLang(IDT_BENCH_PASSES, QStringLiteral("Number of passes:")), this), 0, 0);
  _passesVal = new QLabel(this);
  st->addWidget(_passesVal, 0, 1);
  st->addWidget(new QLabel(FmLang(IDT_BENCH_ELAPSED, QStringLiteral("Elapsed time:")), this), 0, 2);
  _elapsedVal = new QLabel(this);
  st->addWidget(_elapsedVal, 0, 3);
  root->addLayout(st);

  // ---- log box (the per-pass rating table / total-mode console text) ----------
  _log = new QPlainTextEdit(this);
  _log->setReadOnly(true);
  _log->setLineWrapMode(QPlainTextEdit::NoWrap);
  {
    QFont f = _log->font();
    f.setFamily(QStringLiteral("monospace"));
    f.setStyleHint(QFont::TypeWriter);
    _log->setFont(f);
  }
  root->addWidget(_log, 1);

  _sysInfo = new QLabel(this);
  _sysInfo->setWordWrap(true);
  root->addWidget(_sysInfo);

  _errorMsg = new QLabel(this);
  {
    QPalette p = _errorMsg->palette();
    p.setColor(QPalette::WindowText, Qt::red);
    _errorMsg->setPalette(p);
  }
  root->addWidget(_errorMsg);

  // ---- buttons : Stop / Restart / Close --------------------------------------
  QHBoxLayout *btns = new QHBoxLayout();
  _stopBtn = new QPushButton(FmLang(IDB_STOP, QStringLiteral("&Stop")), this);
  _restartBtn = new QPushButton(FmLang(IDB_RESTART, QStringLiteral("&Restart")), this);
  // The dialog's close control mirrors IDCANCEL; no STRINGTABLE id maps cleanly,
  // so it stays the inline "Close" caption.
  QPushButton *closeBtn = new QPushButton(QStringLiteral("Close"), this);
  btns->addWidget(_stopBtn);
  btns->addWidget(_restartBtn);
  btns->addStretch(1);
  btns->addWidget(closeBtn);
  root->addLayout(btns);

  connect(_stopBtn,    &QPushButton::clicked, this, &QtBenchmarkDialog::onStop);
  connect(_restartBtn, &QPushButton::clicked, this, &QtBenchmarkDialog::onRestart);
  connect(closeBtn,    &QPushButton::clicked, this, &QDialog::close);

  connect(_dictionary, qOverload<int>(&QComboBox::currentIndexChanged), this, &QtBenchmarkDialog::onSelectionChanged);
  connect(_numThreads, qOverload<int>(&QComboBox::currentIndexChanged), this, &QtBenchmarkDialog::onSelectionChanged);
  connect(_numPasses,  qOverload<int>(&QComboBox::currentIndexChanged), this, &QtBenchmarkDialog::onSelectionChanged);

  _timer = new QTimer(this);
  _timer->setInterval(kTimerElapse);
  connect(_timer, &QTimer::timeout, this, &QtBenchmarkDialog::onTimer);

  resize(560, 460);
}


// === initFromState : mirror OnInit (combos + hardware info + RestartBenchmark) =
void QtBenchmarkDialog::initFromState()
{
  InitSyncNew();

  UInt32 numCPUs = 1;
  UInt32 numCPUs_Sys = 1;
  {
    // OnInit uses CProcessAffinity::Get_and_return_NumProcessThreads_and_SysThreads
    // (Win32-only affinity path). On Linux the portable accessor is
    // NSystem::GetNumberOfProcessors() (what Bench.cpp itself falls back to);
    // we use it for both the process- and system-thread counts.
#ifndef Z7_ST
    numCPUs = NSystem::GetNumberOfProcessors();
#endif
    if (numCPUs == 0)
      numCPUs = 1;
    numCPUs_Sys = numCPUs;
    AString s1, s2;
    GetSysInfo(s1, s2);
    // OnInit uses GetCpuName_MultiLine(s, registers) (BenchmarkDialog.cpp:504).
    AString cpu, regs;
    GetCpuName_MultiLine(cpu, regs);
    AString info("7-Zip ");
    info += MY_VERSION_CPU;
    info += "\n";
    info += cpu;
    if (!s1.IsEmpty()) { info += "\n"; info += s1; }
    {
      AString feat;
      AddCpuFeatures(feat);
      if (!feat.IsEmpty()) { info += " : "; info += feat; }
    }
    _sysInfo->setText(QString::fromUtf8(info.Ptr()));
  }

  // ----- Num Threads (OnInit combo fill) -----
  UInt32 numThreads = Sync.NumThreads;
  if (numThreads == (UInt32)(Int32)-1)
    numThreads = numCPUs;
  numThreads &= ~(UInt32)1;
  if (numThreads == 0)
    numThreads = 1;
  numThreads = MyMin(numThreads, (UInt32)(1u << 14));

  if (numCPUs_Sys == 0)
    numCPUs_Sys = 1;
  const UInt32 numThreads_Combo = numCPUs_Sys * 2;
  {
    _numThreads->blockSignals(true);
    UInt32 v = 1;
    int cur = 0;
    for (; v <= numThreads_Combo;)
    {
      _numThreads->addItem(QString::number((qulonglong)v), (qulonglong)v);
      int index = _numThreads->count() - 1;
      const UInt32 vNext = v + (v < 2 ? 1 : 2);
      if (v <= numThreads)
      if (numThreads < vNext || vNext > numThreads_Combo)
      {
        if (v != numThreads)
        {
          _numThreads->addItem(QString::number((qulonglong)numThreads), (qulonglong)numThreads);
          index = _numThreads->count() - 1;
        }
        cur = index;
      }
      v = vNext;
    }
    _numThreads->setCurrentIndex(cur);
    _numThreads->blockSignals(false);
  }
  Sync.NumThreads = GetNumberOfThreads();

  // ----- Dictionary -----
  RamSize = (size_t)(sizeof(size_t)) << 29;
  RamSize_Defined = NSystem::GetRamSize(RamSize);
  RamSize_Limit = RamSize / 16 * 15;

  if (Sync.DictSize == (UInt64)(Int64)-1)
  {
    unsigned dicSizeLog = 25;
    if (RamSize_Defined)
    for (; dicSizeLog > kBenchMinDicLogSize; dicSizeLog--)
      if (IsMemoryUsageOK(GetBenchMemoryUsage(
          Sync.NumThreads, Sync.Level, (UInt64)1 << dicSizeLog, TotalMode)))
        break;
    Sync.DictSize = (UInt64)1 << dicSizeLog;
  }
  if (Sync.DictSize < kMinDicSize) Sync.DictSize = kMinDicSize;
  if (Sync.DictSize > kMaxDicSize) Sync.DictSize = kMaxDicSize;

  {
    _dictionary->blockSignals(true);
    int cur = 0;
    for (unsigned i = (kMinDicLogSize - 1) * 2; i <= (32 - 1) * 2; i++)
    {
      const size_t dict = (size_t)(2 + (i & 1)) << (i / 2);
      wchar_t s[32];
      const wchar_t *post;
      UInt32 d;
           if (dict >= ((UInt32)1 << 31)) { d = (UInt32)(dict >> 30); post = kGB; }
      else if (dict >= ((UInt32)1 << 21)) { d = (UInt32)(dict >> 20); post = kMB; }
      else                                { d = (UInt32)(dict >> 10); post = kKB; }
      ConvertUInt32ToString(d, s);
      UString item(s); item += post;
      _dictionary->addItem(UStringToQ(item), (qulonglong)(quint64)dict);
      const int index = _dictionary->count() - 1;
      if (dict <= Sync.DictSize)
        cur = index;
      if (dict >= kMaxDicSize)
        break;
    }
    _dictionary->setCurrentIndex(cur);
    _dictionary->blockSignals(false);
  }

  // ----- Num Passes -----
  {
    _numPasses->blockSignals(true);
    int cur = 0;
    UInt32 v = 1;
    for (;;)
    {
      _numPasses->addItem(QString::number((qulonglong)v), (qulonglong)v);
      int index = _numPasses->count() - 1;
      const bool isLast = (v >= 10000000);
      UInt32 vNext = v * 10;
           if (v < 2) vNext = 2;
      else if (v < 5) vNext = 5;
      else if (v < 10) vNext = 10;
      if (v <= Sync.NumPasses_Limit)
      if (isLast || Sync.NumPasses_Limit < vNext)
      {
        if (v != Sync.NumPasses_Limit)
        {
          _numPasses->addItem(QString::number((qulonglong)Sync.NumPasses_Limit), (qulonglong)Sync.NumPasses_Limit);
          index = _numPasses->count() - 1;
        }
        cur = index;
      }
      v = vNext;
      if (isLast)
        break;
    }
    _numPasses->setCurrentIndex(cur);
    _numPasses->blockSignals(false);
  }

  RestartBenchmark();
}


UInt32 QtBenchmarkDialog::GetNumberOfThreads() const
{
  return (UInt32)_numThreads->currentData().toULongLong();
}


// === OnChangeDictionary : compute the memory-usage label ======================
size_t QtBenchmarkDialog::OnChangeDictionary()
{
  const size_t dict = (size_t)_dictionary->currentData().toULongLong();
  const UInt64 memUsage = GetBenchMemoryUsage(GetNumberOfThreads(), Sync.Level, dict, false);

  UString s;
  s.Add_UInt64((memUsage + (1 << 20) - 1) >> 20); s += kMB;
  if (RamSize_Defined)
  {
    s += " / ";
    s.Add_UInt64(((UInt64)RamSize + (1 << 20) - 1) >> 20); s += kMB;
  }
  SetLabel(_memoryVal, s);
  return dict;
}


// === StartBenchmark : mirror CBenchmarkDialog::StartBenchmark =================
void QtBenchmarkDialog::StartBenchmark()
{
  NeedRestart = false;
  WasStopped_in_GUI = false;

  SetLabel(_errorMsg, UString());

  const size_t dict = OnChangeDictionary();
  const UInt32 numThreads = GetNumberOfThreads();
  const UInt32 numPasses = (UInt32)_numPasses->currentData().toULongLong();

  for (int rr = 0; rr < 2; rr++)
  {
    SetLabel(_enc[rr][0], kProcessingString); SetLabel(_enc[rr][1], kProcessingString);
    SetLabel(_enc[rr][2], kProcessingString); SetLabel(_enc[rr][3], kProcessingString);
    SetLabel(_dec[rr][0], kProcessingString); SetLabel(_dec[rr][1], kProcessingString);
    SetLabel(_dec[rr][2], kProcessingString); SetLabel(_dec[rr][3], kProcessingString);
  }
  SetLabel(_tot[0], kProcessingString); SetLabel(_tot[1], kProcessingString); SetLabel(_tot[2], kProcessingString);
  _log->clear();
  SetLabel(_elapsedVal, UString());

  const UInt64 memUsage = GetBenchMemoryUsage(numThreads, Sync.Level, dict, false);
  if (!IsMemoryUsageOK(memUsage))
  {
    UString s("ERROR: The benchmark needs more memory than the limit. Reduce the dictionary size.");
    SetLabel(_errorMsg, s);
    return;
  }

  _stopBtn->setEnabled(true);

  _startTime = GetTickCount();
  _finishTime = _startTime;
  _finishTime_WasSet = false;

  {
    NSynchronization::CCriticalSectionLock lock(Sync.CS);
    InitSyncNew();
    Sync.DictSize = dict;
    Sync.NumThreads = numThreads;
    Sync.NumPasses_Limit = numPasses;
  }

  PrintTime();

  _timer->start();

  // launch the worker thread (mirror _thread.Create(MyThreadFunction))
  if (_thread)
  {
    _thread->wait();
    delete _thread;
    _thread = nullptr;
  }
  _thread = QThread::create([this]() { runBenchmarkThread(); });
  _thread->start();
}


// === RestartBenchmark / Stop / Cancel (mirror originals) ======================
void QtBenchmarkDialog::RestartBenchmark()
{
  if (ExitWasAsked_in_GUI)
    return;
  if (_thread && _thread->isRunning())
  {
    NeedRestart = true;
    SetLabel(_errorMsg, UString(L"Stop for restart ..."));
    Sync.SendExit();
  }
  else
    StartBenchmark();
}

void QtBenchmarkDialog::Disable_Stop_Button()
{
  _stopBtn->setEnabled(false);
}

void QtBenchmarkDialog::OnStopButton()
{
  if (ExitWasAsked_in_GUI)
    return;
  Disable_Stop_Button();
  WasStopped_in_GUI = true;
  if (_thread && _thread->isRunning())
  {
    SetLabel(_errorMsg, UString(L"Stop ..."));
    Sync.SendExit();
  }
}

void QtBenchmarkDialog::onStop()    { OnStopButton(); }
void QtBenchmarkDialog::onRestart() { RestartBenchmark(); }
void QtBenchmarkDialog::onTimer()   { UpdateGui(); }

void QtBenchmarkDialog::onSelectionChanged()
{
  // CBN_SELCHANGE on dictionary / threads / passes -> RestartBenchmark()
  RestartBenchmark();
}

void QtBenchmarkDialog::closeEvent(QCloseEvent *e)
{
  // OnCancel : ask the worker to exit, then close once it has joined.
  ExitWasAsked_in_GUI = true;
  if (_thread && _thread->isRunning())
  {
    SetLabel(_errorMsg, UString(L"Cancel ..."));
    Sync.SendExit();
    _thread->wait();           // block until the worker is done (it posts finish)
  }
  if (_timer)
    _timer->stop();
  e->accept();
}


// === PrintTime / PrintBenchRes (mirror originals) =============================
void QtBenchmarkDialog::PrintTime()
{
  const UInt32 curTime = _finishTime_WasSet ? _finishTime : GetTickCount();
  const UInt32 elapsedTime = (curTime - _startTime);
  wchar_t s[64];
  wchar_t *p = ConvertUInt32ToString(elapsedTime / 1000, s);
  if (_finishTime_WasSet)
  {
    *p++ = '.';
    UINT_TO_STR_3(p, elapsedTime % 1000)
  }
  MyStringCopy(p, L" s");
  if (UString(s) == ElapsedSec_Prev)
    return;
  ElapsedSec_Prev = s;
  SetLabel(_elapsedVal, UString(s));
}

void QtBenchmarkDialog::PrintBenchRes(const CTotalBenchRes2 &info, QLabel * const lbl[5])
{
  if (info.NumIterations2 == 0)
    return;
  // [0]=Usage [1]=Speed [2]=R/U [3]=Rating [4]=Size
  if (lbl[1])
    lbl[1]->setText(NumberWithPost((info.Speed >> 10) / info.NumIterations2, kKBs));
  if (lbl[3])
  {
    wchar_t s[64];
    MyStringCopy(NumberToDot3(GetMips(info.Rating / info.NumIterations2), s), L" GIPS");
    lbl[3]->setText(QString::fromWCharArray(s));
  }
  if (lbl[2])
  {
    wchar_t s[64];
    MyStringCopy(NumberToDot3(GetMips(info.RPU / info.NumIterations2), s), L" GIPS");
    lbl[2]->setText(QString::fromWCharArray(s));
  }
  if (lbl[0])
    lbl[0]->setText(NumberWithPost(GetUsagePercents(info.Usage / info.NumIterations2), L"%"));
}


// === UpdateGui : mirror CBenchmarkDialog::UpdateGui ===========================
void QtBenchmarkDialog::UpdateGui()
{
  PrintTime();

  if (TotalMode)
  {
    bool wasChanged = false;
    UString text;
    {
      NSynchronization::CCriticalSectionLock lock(Sync.CS);
      if (Sync.TextWasChanged)
      {
        wasChanged = true;
        text = GetUnicodeString(Sync.Text, CP_UTF8);
        Sync.TextWasChanged = false;
      }
    }
    if (wasChanged)
    {
      // Total-mode emits a running console transcript; show it whole.
      _log->setPlainText(UStringToQ(text));
    }
    return;
  }

  CSyncData sd;
  CRecordVector<CBenchPassResult2> RatingVector;
  {
    NSynchronization::CCriticalSectionLock lock(Sync.CS);
    sd = Sync.sd;
    if (sd.NeedPrint_RatingVector)
      RatingVector = Sync.RatingVector;
    if (sd.NeedPrint_Freq)
    {
      Sync.FreqString_GUI = Sync.FreqString_Sync;
      sd.NeedPrint_RatingVector = true;
    }
    Sync.sd.NeedPrint_RatingVector = false;
    Sync.sd.NeedPrint_Enc_1 = false;
    Sync.sd.NeedPrint_Enc   = false;
    Sync.sd.NeedPrint_Dec_1 = false;
    Sync.sd.NeedPrint_Dec   = false;
    Sync.sd.NeedPrint_Tot   = false;
    Sync.sd.NeedPrint_Freq = false;
  }

  if (sd.NumPasses_Finished != NumPasses_Finished_Prev)
  {
    _passesVal->setText(NumberWithPost(sd.NumPasses_Finished, L" /"));
    NumPasses_Finished_Prev = sd.NumPasses_Finished;
  }

  if (sd.NeedPrint_Enc_1) PrintBenchRes(sd.Enc_BenchRes_1, _enc[0]);
  if (sd.NeedPrint_Enc)   PrintBenchRes(sd.Enc_BenchRes,   _enc[1]);
  if (sd.NeedPrint_Dec_1) PrintBenchRes(sd.Dec_BenchRes_1, _dec[0]);
  if (sd.NeedPrint_Dec)   PrintBenchRes(sd.Dec_BenchRes,   _dec[1]);

  if (sd.BenchWasFinished && sd.NeedPrint_Tot)
  {
    CTotalBenchRes2 tot_BenchRes = sd.Enc_BenchRes;
    tot_BenchRes.Update_With_Res2(sd.Dec_BenchRes);
    // total : [0]=Usage(_tot[0]) [2]=R/U(_tot[2]) [3]=Rating(_tot[1])
    QLabel *totLbl[5] = { _tot[0], nullptr, _tot[2], _tot[1], nullptr };
    PrintBenchRes(tot_BenchRes, totLbl);
  }

  if (sd.NeedPrint_RatingVector)
  {
    UString s;
    s += Sync.FreqString_GUI;
    if (!RatingVector.IsEmpty())
    {
      if (!s.IsEmpty())
        s.Add_LF();
      s += "Compr Decompr Total   CPU";
      s.Add_LF();
    }
    for (unsigned i = 0; i < RatingVector.Size(); i++)
    {
      if (i != 0)
        s.Add_LF();
      if ((int)i == sd.RatingVector_DeletedIndex)
      {
        s += "...";
        s.Add_LF();
      }
      const CBenchPassResult2 &pair = RatingVector[i];
      AddRatingsLine(s, pair.Enc, pair.Dec);
    }
    if (sd.BenchWasFinished)
    {
      s.Add_LF();
      s += "-------------";
      s.Add_LF();
      AddRatingsLine(s, sd.Enc_BenchRes, sd.Dec_BenchRes);
    }
    _log->setPlainText(UStringToQ(s));
  }
}


// ====================================================================
//   the engine callbacks (lifted from BenchmarkDialog.cpp)
// ====================================================================

struct CBenchCallback Z7_final: public IBenchCallback
{
  UInt64 dictionarySize;
  CBenchProgressSync *Sync;
  QtBenchmarkDialog *Dialog;

  HRESULT SetEncodeResult(const CBenchInfo &info, bool final) Z7_override;
  HRESULT SetDecodeResult(const CBenchInfo &info, bool final) Z7_override;
};

HRESULT CBenchCallback::SetEncodeResult(const CBenchInfo &info, bool final)
{
  bool needPost = false;
  {
    NSynchronization::CCriticalSectionLock lock(Sync->CS);
    if (Sync->Exit)
      return E_ABORT;
    CSyncData &sd = Sync->sd;
    CTotalBenchRes2 &br = sd.Enc_BenchRes_1;
    {
      UInt64 dictSize = Sync->DictSize;
      if (!final)
        if (dictSize > info.UnpackSize)
          dictSize = info.UnpackSize;
      br.Rating = info.GetRating_LzmaEnc(dictSize);
    }
    br.SetFrom_BenchInfo(info);
    sd.NeedPrint_Enc_1 = true;
    if (final)
    {
      sd.Enc_BenchRes.Update_With_Res2(br);
      sd.NeedPrint_Enc = true;
      needPost = true;
    }
  }
  if (needPost)
    QMetaObject::invokeMethod(Dialog, "onFinishMessage", Qt::QueuedConnection,
        Q_ARG(int, k_WPARM_Enc1_Finished));
  return S_OK;
}

HRESULT CBenchCallback::SetDecodeResult(const CBenchInfo &info, bool final)
{
  NSynchronization::CCriticalSectionLock lock(Sync->CS);
  if (Sync->Exit)
    return E_ABORT;
  CSyncData &sd = Sync->sd;
  CTotalBenchRes2 &br = sd.Dec_BenchRes_1;
  br.Rating = info.GetRating_LzmaDec();
  br.SetFrom_BenchInfo(info);
  sd.NeedPrint_Dec_1 = true;
  if (final)
    sd.Dec_BenchRes.Update_With_Res2(br);
  return S_OK;
}


struct CBenchCallback2 Z7_final: public IBenchPrintCallback
{
  CBenchProgressSync *Sync;
  bool TotalMode;

  void Print(const char *s) Z7_override;
  void NewLine() Z7_override;
  HRESULT CheckBreak() Z7_override;
};

void CBenchCallback2::Print(const char *s)
{
  if (TotalMode)
  {
    NSynchronization::CCriticalSectionLock lock(Sync->CS);
    Sync->Text += s;
    Sync->TextWasChanged = true;
  }
}

void CBenchCallback2::NewLine() { Print("\n"); }

HRESULT CBenchCallback2::CheckBreak()
{
  if (Sync->Exit)
    return E_ABORT;
  return S_OK;
}


struct CFreqCallback Z7_final: public IBenchFreqCallback
{
  QtBenchmarkDialog *Dialog;

  HRESULT AddCpuFreq(unsigned numThreads, UInt64 freq, UInt64 usage) Z7_override;
  HRESULT FreqsFinished(unsigned numThreads) Z7_override;
};

HRESULT CFreqCallback::AddCpuFreq(unsigned numThreads, UInt64 freq, UInt64 usage)
{
  HRESULT res;
  {
    CBenchProgressSync &sync = Dialog->Sync;
    NSynchronization::CCriticalSectionLock lock(sync.CS);
    UString &s = sync.FreqString_Sync;
    if (sync.NumFreqThreadsPrev != numThreads)
    {
      sync.NumFreqThreadsPrev = numThreads;
      if (!s.IsEmpty())
        s.Add_LF();
      s.Add_UInt32(numThreads);
      s += "T Frequency (MHz):";
      s.Add_LF();
    }
    s.Add_Space();
    if (numThreads != 1)
    {
      s.Add_UInt64(GetUsagePercents(usage));
      s.Add_Char('%');
      s.Add_Space();
    }
    s.Add_UInt64(GetMips(freq));
    res = sync.Exit ? E_ABORT : S_OK;
  }
  return res;
}

HRESULT CFreqCallback::FreqsFinished(unsigned /* numThreads */)
{
  HRESULT res;
  {
    CBenchProgressSync &sync = Dialog->Sync;
    NSynchronization::CCriticalSectionLock lock(sync.CS);
    sync.sd.NeedPrint_Freq = true;
    res = sync.Exit ? E_ABORT : S_OK;
  }
  QMetaObject::invokeMethod(Dialog, "onFinishMessage", Qt::QueuedConnection,
      Q_ARG(int, k_WPARM_Enc1_Finished));
  return res;
}


// === the worker thread loop : mirror CThreadBenchmark::Process ================
static void ParseNumberString(const UString &s, NCOM::CPropVariant &prop)
{
  const wchar_t *end;
  UInt64 result = ConvertStringToUInt64(s, &end);
  if (*end != 0 || s.IsEmpty())
    prop = s;
  else if (result <= (UInt32)0xFFFFFFFF)
    prop = (UInt32)result;
  else
    prop = result;
}

void QtBenchmarkDialog::runBenchmarkThread()
{
  CBenchProgressSync &sync = Sync;
  HRESULT finishHRESULT = S_OK;

  try
  {
    for (UInt32 passIndex = 0;; passIndex++)
    {
      UInt64 dictionarySize;
      UInt32 numThreads;
      {
        NSynchronization::CCriticalSectionLock lock(sync.CS);
        if (sync.Exit)
          break;
        dictionarySize = sync.DictSize;
        numThreads = sync.NumThreads;
      }

      CBenchCallback callback;
      callback.dictionarySize = dictionarySize;
      callback.Sync = &sync;
      callback.Dialog = this;

      CBenchCallback2 callback2;
      callback2.TotalMode = TotalMode;
      callback2.Sync = &sync;

      CFreqCallback freqCallback;
      freqCallback.Dialog = this;

      HRESULT result;
      try
      {
        CObjectVector<CProperty> props = Props;
        if (!TotalMode)
        {
          {
            CProperty prop;
            prop.Name = "mt";
            prop.Value.Add_UInt32(numThreads);
            props.Add(prop);
          }
          {
            CProperty prop;
            prop.Name = 'd';
            prop.Name.Add_UInt32((UInt32)(dictionarySize >> 10));
            prop.Name.Add_Char('k');
            props.Add(prop);
          }
        }
        result = Bench(EXTERNAL_CODECS_LOC_VARS
            TotalMode ? (IBenchPrintCallback *)&callback2 : NULL,
            TotalMode ? NULL : (IBenchCallback *)&callback,
            props, 1, false,
            (!TotalMode) && passIndex == 0 ? (IBenchFreqCallback *)&freqCallback : NULL);
      }
      catch(...)
      {
        result = E_FAIL;
      }

      bool finished = true;

      NSynchronization::CCriticalSectionLock lock(sync.CS);

      if (result != S_OK)
      {
        sync.BenchFinish_Task_HRESULT = result;
        break;
      }

      {
        CSyncData &sd = sync.sd;
        sd.NumPasses_Finished++;

        if (TotalMode)
          break;

        {
          CTotalBenchRes tot_BenchRes = sd.Enc_BenchRes_1;
          tot_BenchRes.Update_With_Res(sd.Dec_BenchRes_1);
          sd.NeedPrint_RatingVector = true;
          {
            CBenchPassResult2 pair;
            pair.Enc = sd.Enc_BenchRes_1;
            pair.Dec = sd.Dec_BenchRes_1;
            sync.RatingVector.Add(pair);
          }
        }

        sd.NeedPrint_Dec = true;
        sd.NeedPrint_Tot = true;

        if (sync.RatingVector.Size() > kRatingVector_NumBundlesMax)
        {
          sd.RatingVector_DeletedIndex = (int)(kRatingVector_NumBundlesMax / 4);
          sync.RatingVector.Delete((unsigned)(sd.RatingVector_DeletedIndex));
        }

        if (sync.sd.NumPasses_Finished < sync.NumPasses_Limit)
          finished = false;
        else
          sync.sd.BenchWasFinished = true;
      }

      if (TotalMode)
        break;

      // mirror PostMsg(k_Message_Finished, k_Msg_WPARM_Iter_Finished)
      QMetaObject::invokeMethod(this, "onFinishMessage", Qt::QueuedConnection,
          Q_ARG(int, k_WPARM_Iter_Finished));

      if (finished)
        break;
    }
  }
  catch(CSystemException &e)
  {
    finishHRESULT = e.ErrorCode;
  }
  catch(...)
  {
    finishHRESULT = E_FAIL;
  }

  if (finishHRESULT != S_OK)
  {
    NSynchronization::CCriticalSectionLock lock(sync.CS);
    sync.BenchFinish_Thread_HRESULT = finishHRESULT;
  }
  // mirror PostMsg_Finish(k_Msg_WPARM_Thread_Finished)
  QMetaObject::invokeMethod(this, "onFinishMessage", Qt::QueuedConnection,
      Q_ARG(int, k_WPARM_Thread_Finished));
}


// === onFinishMessage : mirror OnMessage(k_Message_Finished) ===================
void QtBenchmarkDialog::onFinishMessage(int wparam)
{
  if (wparam == k_WPARM_Thread_Finished)
  {
    _finishTime = GetTickCount();
    _finishTime_WasSet = true;
    if (_timer)
      _timer->stop();

    if (_thread)
    {
      _thread->wait();
      delete _thread;
      _thread = nullptr;
    }

    if (!WasStopped_in_GUI)
    {
      WasStopped_in_GUI = true;
      Disable_Stop_Button();
    }

    HRESULT res = Sync.BenchFinish_Thread_HRESULT;
    if (res != S_OK)
      SetLabel(_errorMsg, UString(L"ERROR: ") + HResultToMessage(res));

    if (ExitWasAsked_in_GUI)
    {
      UpdateGui();
      return;
    }

    res = Sync.BenchFinish_Task_HRESULT;
    if (res != S_OK)
    {
      if (!WasStopped_in_GUI || res != E_ABORT)
      {
        UString m;
        if (res == S_FALSE)
          m = "Decoding error";
        else
          m = HResultToMessage(res);
        SetLabel(_errorMsg, UString(L"ERROR: ") + m);
      }
    }

    if (NeedRestart)
    {
      StartBenchmark();
      return;
    }
  }
  UpdateGui();
}


// === Benchmark : mirror BenchmarkDialog.cpp::Benchmark ========================
HRESULT QtBenchmarkDialog::Benchmark(const CObjectVector<CProperty> &props,
    UInt32 numIterations, bool totalMode, QWidget *parent)
{
  // Like every Qt flow in this port, the dialog runs its own modal nested event
  // loop via exec() (so the worker callbacks + the 1 s timer are pumped) — the
  // synchronous analogue of the Win32 CModalDialog::Create the original uses.
  QtBenchmarkDialog bd(parent);
  QtBenchmarkDialog *const pbd = &bd;

  pbd->TotalMode = totalMode;
  pbd->Props = props;
  if (numIterations == 0)
    numIterations = 1;
  pbd->Sync.NumPasses_Limit = numIterations;
  pbd->Sync.DictSize = (UInt64)(Int64)-1;
  pbd->Sync.NumThreads = (UInt32)(Int32)-1;
  pbd->Sync.Level = -1;

  COneMethodInfo method;

  UInt32 numCPUs = 1;
  #ifndef Z7_ST
  numCPUs = NSystem::GetNumberOfProcessors();
  #endif
  UInt32 numThreads = numCPUs;

  FOR_VECTOR (i, props)
  {
    const CProperty &prop = props[i];
    UString name = prop.Name;
    name.MakeLower_Ascii();
    if (name.IsEqualTo_Ascii_NoCase("m") && prop.Value.IsEqualTo("*"))
    {
      pbd->TotalMode = true;
      continue;
    }
    NCOM::CPropVariant propVariant;
    if (!prop.Value.IsEmpty())
      ParseNumberString(prop.Value, propVariant);
    if (name.IsPrefixedBy("mt"))
    {
      #ifndef Z7_ST
      RINOK(ParseMtProp(name.Ptr(2), propVariant, numCPUs, numThreads))
      if (numThreads != numCPUs)
        pbd->Sync.NumThreads = numThreads;
      #endif
      continue;
    }
    method.ParseMethodFromPROPVARIANT(name, propVariant);
  }

  {
    UInt64 dict;
    if (method.Get_DicSize(dict))
      pbd->Sync.DictSize = dict;
  }
  pbd->Sync.Level = (int)method.GetLevel();

  pbd->initFromState();
  pbd->exec();
  return S_OK;
}


// ====================================================================
//   RunBenchmarkConsole : the CLI path (mirror BenchCon.cpp::BenchCon)
// ====================================================================

struct CPrintBenchCallback Z7_final: public IBenchPrintCallback
{
  FILE *_file;
  void Print(const char *s) Z7_override { fputs(s, _file); }
  void NewLine() Z7_override { fputc('\n', _file); }
  HRESULT CheckBreak() Z7_override { return S_OK; }
};

HRESULT RunBenchmarkConsole(
    DECL_EXTERNAL_CODECS_LOC_VARS
    const CObjectVector<CProperty> &props, UInt32 numIterations, FILE *f)
{
  CPrintBenchCallback callback;
  callback._file = f;
  return Bench(EXTERNAL_CODECS_LOC_VARS
      &callback, NULL, props, numIterations, true);
}
