// QtProgressDialog.cpp
//
// Qt/Linux port of CProgressDialog (CPP/7zip/UI/FileManager/ProgressDialog2.cpp).
//
// The numeric/display logic (UpdateStatInfo, SetProgressRange/Pos, ShowSize,
// GetTimeString, ConvertSizeToString, MyMultAndDiv, the speed smoothing) is
// ported faithfully from the original; only the rendering target changed from
// Win32 dialog-item HWNDs to Qt widgets, and the WM_TIMER pump became a QTimer.

#include "QtProgressDialog.h"

#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>     // G.8b : CopyToClipboard()
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>      // G.8b : Ctrl+C / Ctrl+Ins / Ctrl+A handling
#include <QtCore/QEvent>

#include "../../../Common/IntToString.h"

#include "QtLang.h"
// G.1d : the original CProgressDialog langIDs. The IDT_PROGRESS_* control IDs
// double as their langIDs (consumed by LangSetDlgItems/LangSetDlgItems_Colon,
// ProgressDialog2.cpp:377-378); IDS_*/IDB_* are direct LangString ids.
#include "../FileManager/ProgressDialog2Res.h"

using namespace NWindows;

// ---- ported constants from ProgressDialog2.cpp ----------------------------
static const int kTimerElapse = 200; // kTimerElapse (non-CE) in the original

#define UNDEFINED_VAL         ((UInt64)(Int64)-1)
#define INIT_AS_UNDEFINED(v)  v = UNDEFINED_VAL;
#define IS_UNDEFINED_VAL(v)   ((v) == UNDEFINED_VAL)
#define IS_DEFINED_VAL(v)     ((v) != UNDEFINED_VAL)

// GetTickCount() is declared by Common/MyWindows.h (pulled in transitively via
// MyCom.h) and implemented in Common/MyWindows.cpp (part of sevenzip_engine).

static const unsigned kTitleFileNameSizeLimit = 36;
static const unsigned kCurrentFileNameSizeLimit = 82;

// ReduceString : verbatim from ProgressDialog2.cpp
static void ReduceString(UString &s, unsigned size)
{
  if (s.Len() <= size)
    return;
  s.Delete(size / 2, s.Len() - size);
  s.Insert(size / 2, L" ... ");
}

#define UINT_TO_STR_2(val) { s[0] = (wchar_t)('0' + (val) / 10); s[1] = (wchar_t)('0' + (val) % 10); s += 2; }

// GetTimeString : verbatim from ProgressDialog2.cpp
static void GetTimeString(UInt64 timeValue, wchar_t *s)
{
  UInt64 hours = timeValue / 3600;
  UInt32 seconds = (UInt32)(timeValue - hours * 3600);
  UInt32 minutes = seconds / 60;
  seconds %= 60;
  if (hours > 99)
  {
    ConvertUInt64ToString(hours, s);
    for (; *s != 0; s++);
  }
  else
  {
    UInt32 hours32 = (UInt32)hours;
    UINT_TO_STR_2(hours32)
  }
  *s++ = ':'; UINT_TO_STR_2(minutes)
  *s++ = ':'; UINT_TO_STR_2(seconds)
  *s = 0;
}

// ConvertSizeToString : verbatim from ProgressDialog2.cpp
static void ConvertSizeToString(UInt64 v, wchar_t *s)
{
  Byte c = 0;
       if (v >= ((UInt64)100000 << 20)) { v >>= 30; c = 'G'; }
  else if (v >= ((UInt64)100000 << 10)) { v >>= 20; c = 'M'; }
  else if (v >= ((UInt64)100000 <<  0)) { v >>= 10; c = 'K'; }
  ConvertUInt64ToString(v, s);
  if (c != 0)
  {
    s += MyStringLen(s);
    *s++ = ' ';
    *s++ = c;
    *s++ = 'B';
    *s++ = 0;
  }
}

static void GetChangedString(const UString &newStr, UString &prevStr, bool &hasChanged)
{
  hasChanged = !(prevStr == newStr);
  if (hasChanged)
    prevStr = newStr;
}

// GetPower32/64 + MyMultAndDiv : verbatim from ProgressDialog2.cpp
static unsigned GetPower32(UInt32 val)
{
  const unsigned kStart = 32;
  UInt32 mask = ((UInt32)1 << (kStart - 1));
  for (unsigned i = kStart;; i--)
  {
    if (i == 0 || (val & mask) != 0)
      return i;
    mask >>= 1;
  }
}

static unsigned GetPower64(UInt64 val)
{
  UInt32 high = (UInt32)(val >> 32);
  if (high == 0)
    return GetPower32((UInt32)val);
  return GetPower32(high) + 32;
}

static UInt64 MyMultAndDiv(UInt64 mult1, UInt64 mult2, UInt64 divider)
{
  unsigned pow1 = GetPower64(mult1);
  unsigned pow2 = GetPower64(mult2);
  while (pow1 + pow2 > 64)
  {
    if (pow1 > pow2) { pow1--; mult1 >>= 1; }
    else             { pow2--; mult2 >>= 1; }
    divider >>= 1;
  }
  UInt64 res = mult1 * mult2;
  if (divider != 0)
    res /= divider;
  return res;
}

static QString US2Q(const UString &s)
{
  return QString::fromWCharArray(s.Ptr(), (int)s.Len());
}

// Ascending compare for CRecordVector<unsigned>::Sort (G.8b CopyToClipboard).
static int CompareUInts(const unsigned *a, const unsigned *b, void * /*param*/)
{
  if (*a < *b) return -1;
  if (*a > *b) return 1;
  return 0;
}

static QString WCS2Q(const wchar_t *s)
{
  return QString::fromWCharArray(s);
}

// ---------------------------------------------------------------------------

QtProgressDialog::QtProgressDialog(QWidget *parent)
  : QDialog(parent),
    _isDir(false),
    _numPostedMessages(0),
    _title_arg(0),
    _errorsWereDisplayed(false),
    _externalCloseHandled(false),
    _numMessages(0),
    CompressingMode(true),
    MessagesDisplayed(false),
    DisableUserQuestions(false)
{
  buildUi();

  // OnInit() equivalent: initialise all the "previous value" trackers exactly
  // as CProgressDialog::OnInit() does.
  INIT_AS_UNDEFINED(_progressBar_Range)
  INIT_AS_UNDEFINED(_progressBar_Pos)
  INIT_AS_UNDEFINED(_prevPercentValue)
  INIT_AS_UNDEFINED(_prevElapsedSec)
  INIT_AS_UNDEFINED(_prevRemainingSec)
  INIT_AS_UNDEFINED(_prevSpeed)
  _prevSpeed_MoveBits = 0;

  _prevTime = ::GetTickCount();
  _elapsedTime = 0;

  INIT_AS_UNDEFINED(_totalBytes_Prev)
  INIT_AS_UNDEFINED(_processed_Prev)
  INIT_AS_UNDEFINED(_packed_Prev)
  INIT_AS_UNDEFINED(_ratio_Prev)
  _filesStr_Prev.Empty();
  _filesTotStr_Prev.Empty();

  _timer = new QTimer(this);
  connect(_timer, &QTimer::timeout, this, &QtProgressDialog::onTimer);
  _timer->start(kTimerElapse);
}

QtProgressDialog::~QtProgressDialog() {}

static QLabel *makeVal(const char *initial = "")
{
  QLabel *l = new QLabel(QString::fromLatin1(initial));
  l->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return l;
}

void QtProgressDialog::buildUi()
{
  setMinimumWidth(520);

  _statusLabel  = new QLabel(this);
  _fileNameLabel = new QLabel(this);
  _fileNameLabel->setTextFormat(Qt::PlainText);
  _fileNameLabel->setWordWrap(false);

  _progressBar = new QProgressBar(this);
  _progressBar->setRange(0, 0); // indeterminate until total is known
  _progressBar->setTextVisible(true);

  // value labels mirroring the CProgressDialog IDT_PROGRESS_* fields
  _elapsedVal   = makeVal("00:00:00");
  _remainingVal = makeVal();
  _filesVal     = makeVal("0");
  _filesTotal   = makeVal();
  _errorsVal    = makeVal("0");
  _totalVal     = makeVal();
  _speedVal     = makeVal();
  _processedVal = makeVal();
  _packedVal    = makeVal();
  _ratioVal     = makeVal();

  QGridLayout *grid = new QGridLayout();
  int r = 0;
  grid->addWidget(new QLabel(FmLang(IDT_PROGRESS_ELAPSED, QStringLiteral("Elapsed time:"))),   r, 0); grid->addWidget(_elapsedVal,   r, 1);
  grid->addWidget(new QLabel(FmLang(IDT_PROGRESS_TOTAL, QStringLiteral("Total size:"))),     r, 2); grid->addWidget(_totalVal,     r, 3); r++;
  grid->addWidget(new QLabel(FmLang(IDT_PROGRESS_REMAINING, QStringLiteral("Remaining time:"))), r, 0); grid->addWidget(_remainingVal, r, 1);
  grid->addWidget(new QLabel(FmLang(IDT_PROGRESS_SPEED, QStringLiteral("Speed:"))),          r, 2); grid->addWidget(_speedVal,     r, 3); r++;
  {
    QWidget *filesCell = new QWidget(this);
    QHBoxLayout *fh = new QHBoxLayout(filesCell);
    fh->setContentsMargins(0, 0, 0, 0);
    fh->addWidget(_filesVal);
    fh->addWidget(_filesTotal);
    fh->addStretch();
    grid->addWidget(new QLabel(FmLang(IDT_PROGRESS_FILES, QStringLiteral("Files:"))),        r, 0); grid->addWidget(filesCell,     r, 1);
  }
  grid->addWidget(new QLabel(FmLang(IDT_PROGRESS_PROCESSED, QStringLiteral("Processed size:"))), r, 2); grid->addWidget(_processedVal, r, 3); r++;
  grid->addWidget(new QLabel(FmLang(IDT_PROGRESS_ERRORS, QStringLiteral("Errors:"))),         r, 0); grid->addWidget(_errorsVal,    r, 1);
  grid->addWidget(new QLabel(FmLang(IDT_PROGRESS_PACKED, QStringLiteral("Compressed size:"))),r, 2); grid->addWidget(_packedVal,    r, 3); r++;
  grid->addWidget(new QLabel(FmLang(IDT_PROGRESS_RATIO, QStringLiteral("Ratio:"))),          r, 2); grid->addWidget(_ratioVal,     r, 3); r++;

  _messageList = new QListWidget(this);
  // G.8b : mirror the original message ListView — multi-row (extended) selection
  // so Ctrl+A / shift-/ctrl-click select multiple rows, then Ctrl+C / Ctrl+Ins
  // copy the selected-or-all rows to the clipboard. The keyboard handling is
  // ported from CProgressDialog::OnNotify(LVN_KEYDOWN) via an event filter.
  _messageList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  _messageList->installEventFilter(this);

  _cancelButton = new QPushButton(FmLang(402, QStringLiteral("Cancel")), this);
  _pauseButton  = new QPushButton(FmLang(IDB_PAUSE, QStringLiteral("&Pause")), this);
  // Background / priority button: deferred. See ProcessWasFinished_GuiVirt notes.
  // TODO(C.1+): port IDB_PROGRESS_BACKGROUND (priority class) — not portable as-is.
  QPushButton *backgroundButton = new QPushButton(FmLang(IDB_PROGRESS_BACKGROUND, QStringLiteral("&Background")), this);
  backgroundButton->setEnabled(false);
  backgroundButton->setToolTip(QStringLiteral("Deferred: priority/background control (TODO)"));

  connect(_cancelButton, &QPushButton::clicked, this, &QtProgressDialog::onCancel);
  connect(_pauseButton,  &QPushButton::clicked, this, &QtProgressDialog::onPause);

  QHBoxLayout *buttons = new QHBoxLayout();
  buttons->addStretch();
  buttons->addWidget(backgroundButton);
  buttons->addWidget(_pauseButton);
  buttons->addWidget(_cancelButton);

  QVBoxLayout *root = new QVBoxLayout(this);
  root->addWidget(_statusLabel);
  root->addWidget(_fileNameLabel);
  root->addWidget(_progressBar);
  root->addLayout(grid);
  root->addWidget(_messageList, 1);
  root->addLayout(buttons);
}

void QtProgressDialog::SetTitle(const UString &title)
{
  _title = title;
  setWindowTitle(US2Q(title));
}

// SetProgressRange : ported from CProgressDialog::SetProgressRange
void QtProgressDialog::SetProgressRange(UInt64 range)
{
  if (range == _progressBar_Range)
    return;
  _progressBar_Range = range;
  INIT_AS_UNDEFINED(_progressBar_Pos)
  _progressConv.Init(range);
  _progressBar->setRange(0, _progressConv.Count(range));
}

// SetProgressPos : ported from CProgressDialog::SetProgressPos
void QtProgressDialog::SetProgressPos(UInt64 pos)
{
  if (pos >= _progressBar_Range ||
      pos <= _progressBar_Pos ||
      pos - _progressBar_Pos >= (_progressBar_Range >> 10))
  {
    _progressBar->setValue(_progressConv.Count(pos));
    _progressBar_Pos = pos;
  }
}

// ShowSize : ported from CProgressDialog::ShowSize
void QtProgressDialog::ShowSize(QLabel *label, UInt64 val, UInt64 &prev)
{
  if (val == prev)
    return;
  prev = val;
  wchar_t s[40];
  s[0] = 0;
  if (IS_DEFINED_VAL(val))
    ConvertSizeToString(val, s);
  label->setText(WCS2Q(s));
}

// AddMessageDirect : ported from CProgressDialog::AddMessageDirect.
// The original is a two-column ListView (column 0 = number, column 1 = message);
// here we keep a QListWidget and render the number as a right-aligned, padded
// prefix followed by the message text. The RAW message (without the number) is
// stored in _messageStrings so CopyToClipboard matches the original (which copies
// _messageStrings[index], i.e. message text only — see the commented-out
// "s.Add_UInt32(index)" in the original CopyToClipboard).
void QtProgressDialog::AddMessageDirect(const UString &message, bool needNumber)
{
  QString num;
  if (needNumber)
  {
    wchar_t sz[16];
    ConvertUInt32ToString(_numMessages + 1, sz);
    num = WCS2Q(sz);
  }
  // Two-column feel within a single QListWidget row: a fixed-width number field
  // (right-justified) + a gap + the message. This mirrors the original's
  // number-column / message-column split (and leaves continuation rows blank in
  // the number field, exactly like AddMessage's needNumber=false follow-ups).
  const int kNumFieldWidth = 6;
  QString prefix = num.rightJustified(kNumFieldWidth, QLatin1Char(' '));
  _messageList->addItem(prefix + QStringLiteral("  ") + US2Q(message));
  _messageStrings.Add(message);
}

// AddMessage : ported VERBATIM from CProgressDialog::AddMessage — split the
// message on embedded '\n' into separate rows; ONLY the first row is numbered,
// the continuation rows are un-numbered; then bump _numMessages once.
void QtProgressDialog::AddMessage(const UString &message)
{
  UString s = message;
  bool needNumber = true;
  while (!s.IsEmpty())
  {
    const int pos = s.Find(L'\n');
    if (pos < 0)
      break;
    AddMessageDirect(s.Left((unsigned)pos), needNumber);
    needNumber = false;
    s.DeleteFrontal((unsigned)pos + 1);
  }
  AddMessageDirect(s, needNumber);
  _numMessages++;
}

// UpdateMessagesDialog : ported from CProgressDialog::UpdateMessagesDialog
// (drains the new Sync.Messages into the list via AddMessage, which splits on
// '\n' and numbers only the first row of each message).
void QtProgressDialog::UpdateMessagesDialog()
{
  UStringVector messages;
  {
    NSynchronization::CCriticalSectionLock lock(Sync._cs);
    const unsigned num = Sync.Messages.Size();
    if (num > _numPostedMessages)
    {
      messages.ClearAndReserve(num - _numPostedMessages);
      for (unsigned i = _numPostedMessages; i < num; i++)
        messages.AddInReserved(Sync.Messages[i]);
      _numPostedMessages = num;
    }
  }
  if (!messages.IsEmpty())
  {
    FOR_VECTOR (i, messages)
      AddMessage(messages[i]);
  }
}

// CopyToClipboard : ported from CProgressDialog::CopyToClipboard — copy the
// selected-or-all RAW message strings (verbatim _messageStrings, not the number
// column), one per line. With nothing selected, copy ALL rows.
void QtProgressDialog::CopyToClipboard()
{
  CUIntVector indexes;
  {
    const QList<QListWidgetItem *> sel = _messageList->selectedItems();
    for (QListWidgetItem *it : sel)
    {
      const int row = _messageList->row(it);
      if (row >= 0)
        indexes.Add((unsigned)row);
    }
  }
  // selectedItems() order is unspecified; sort to match the row order the
  // original's GetNextSelectedItem walk produces.
  indexes.Sort(CompareUInts, NULL);

  UString s;
  unsigned numIndexes = indexes.Size();
  if (numIndexes == 0)
    numIndexes = (unsigned)_messageStrings.Size();

  for (unsigned i = 0; i < numIndexes; i++)
  {
    const unsigned index = (i < indexes.Size() ? indexes[i] : i);
    if (index >= _messageStrings.Size())
      continue;
    s += _messageStrings[index];
    // On Linux the original uses "\n" (the _WIN32 branch is "\r\n").
    s.Add_LF();
  }

  QGuiApplication::clipboard()->setText(US2Q(s));
}

// eventFilter : ported from CProgressDialog::OnNotify(LVN_KEYDOWN) — Ctrl+A
// selects all rows; Ctrl+C / Ctrl+Insert copy the selected-or-all rows.
bool QtProgressDialog::eventFilter(QObject *obj, QEvent *ev)
{
  if (obj == _messageList && ev->type() == QEvent::KeyPress)
  {
    QKeyEvent *ke = static_cast<QKeyEvent *>(ev);
    if (ke->modifiers() & Qt::ControlModifier)
    {
      switch (ke->key())
      {
        case Qt::Key_A:
          _messageList->selectAll();
          return true;
        case Qt::Key_C:
        case Qt::Key_Insert:
          CopyToClipboard();
          return true;
        default:
          break;
      }
    }
  }
  return QDialog::eventFilter(obj, ev);
}

// --- G.8b/G.8e headless test accessors --------------------------------------
int QtProgressDialog::messageRowCount() const
{
  return _messageList->count();
}

UString QtProgressDialog::messageRowText(int row) const
{
  if (row < 0 || row >= _messageList->count())
    return UString();
  const QString t = _messageList->item(row)->text();
  UString u;
  for (int i = 0; i < t.size(); i++)
    u += (wchar_t)t.at(i).unicode();
  return u;
}

UString QtProgressDialog::messageRawText(int row) const
{
  if (row < 0 || (unsigned)row >= _messageStrings.Size())
    return UString();
  return _messageStrings[(unsigned)row];
}

void QtProgressDialog::selectMessageRow(int row)
{
  if (row >= 0 && row < _messageList->count())
    _messageList->item(row)->setSelected(true);
}

// SetTitleText : ported from CProgressDialog::SetTitleText (paused + percent +
// title + reduced titleFileName). Foreground/background string omitted (deferred).
void QtProgressDialog::SetTitleText()
{
  UString s;
  if (Sync.Get_Paused())
  {
    s += L"Paused";
    s.Add_Space();
  }
  if (IS_DEFINED_VAL(_prevPercentValue))
  {
    s.Add_UInt64(_prevPercentValue);
    s.Add_Char('%');
  }
  s.Add_Space();
  s += _title;
  if (!_titleFileName.IsEmpty())
  {
    UString fileName = _titleFileName;
    ReduceString(fileName, kTitleFileNameSizeLimit);
    s.Add_Space();
    s += fileName;
  }
  setWindowTitle(US2Q(s));
}

// UpdateStatInfo : ported faithfully from CProgressDialog::UpdateStatInfo.
void QtProgressDialog::UpdateStatInfo(bool showAll)
{
  UInt64 total, completed, totalFiles, completedFiles, inSize, outSize;
  bool filesProgressMode;

  bool titleFileName_Changed;
  bool curFilePath_Changed;
  bool status_Changed;
  unsigned numErrors;
  {
    NSynchronization::CCriticalSectionLock lock(Sync._cs);
    total = Sync._totalBytes;
    completed = Sync._completedBytes;
    totalFiles = Sync._totalFiles;
    completedFiles = Sync._curFiles;
    inSize = Sync._inSize;
    outSize = Sync._outSize;
    filesProgressMode = Sync._filesProgressMode;

    GetChangedString(Sync._titleFileName, _titleFileName, titleFileName_Changed);
    GetChangedString(Sync._filePath, _filePath, curFilePath_Changed);
    GetChangedString(Sync._status, _status, status_Changed);
    if (_isDir != Sync._isDir)
    {
      curFilePath_Changed = true;
      _isDir = Sync._isDir;
    }
    numErrors = Sync.Messages.Size();
  }

  UInt32 curTime = ::GetTickCount();

  const UInt64 progressTotal = filesProgressMode ? totalFiles : total;
  const UInt64 progressCompleted = filesProgressMode ? completedFiles : completed;
  {
    if (IS_UNDEFINED_VAL(progressTotal))
    {
      // SetPos(0);
      // SetRange(progressCompleted);
    }
    else
    {
      if (_progressBar_Pos != 0 || progressCompleted != 0 ||
          (_progressBar_Range == 0 && progressTotal != 0))
      {
        SetProgressRange(progressTotal);
        SetProgressPos(progressCompleted);
      }
    }
  }

  ShowSize(_totalVal, total, _totalBytes_Prev);

  _elapsedTime += (curTime - _prevTime);
  _prevTime = curTime;
  UInt64 elapsedSec = _elapsedTime / 1000;
  bool elapsedChanged = false;
  if (elapsedSec != _prevElapsedSec)
  {
    _prevElapsedSec = elapsedSec;
    elapsedChanged = true;
    wchar_t s[40];
    GetTimeString(elapsedSec, s);
    _elapsedVal->setText(WCS2Q(s));
  }

  bool needSetTitle = false;
  if (elapsedChanged || showAll)
  {
    if (numErrors > _numPostedMessages)
    {
      UpdateMessagesDialog();
      wchar_t s[32];
      ConvertUInt64ToString(numErrors, s);
      _errorsVal->setText(WCS2Q(s));
      if (!_errorsWereDisplayed)
        _errorsWereDisplayed = true;
    }

    if (progressCompleted != 0)
    {
      if (IS_UNDEFINED_VAL(progressTotal))
      {
        if (IS_DEFINED_VAL(_prevRemainingSec))
        {
          INIT_AS_UNDEFINED(_prevRemainingSec)
          _remainingVal->setText(QString());
        }
      }
      else
      {
        UInt64 remainingTime = 0;
        if (progressCompleted < progressTotal)
          remainingTime = MyMultAndDiv(_elapsedTime, progressTotal - progressCompleted, progressCompleted);
        UInt64 remainingSec = remainingTime / 1000;
        if (remainingSec != _prevRemainingSec)
        {
          _prevRemainingSec = remainingSec;
          wchar_t s[40];
          GetTimeString(remainingSec, s);
          _remainingVal->setText(WCS2Q(s));
        }
      }
      {
        const UInt64 elapsedTime = (_elapsedTime == 0) ? 1 : _elapsedTime;
        // 22.02: progressCompleted can be for number of files
        UInt64 v = (completed * 1000) / elapsedTime;
        Byte c = 0;
        unsigned moveBits = 0;
             if (v >= ((UInt64)10000 << 10)) { moveBits = 20; c = 'M'; }
        else if (v >= ((UInt64)10000 <<  0)) { moveBits = 10; c = 'K'; }
        v >>= moveBits;
        if (moveBits != _prevSpeed_MoveBits || v != _prevSpeed)
        {
          _prevSpeed_MoveBits = moveBits;
          _prevSpeed = v;
          wchar_t s[40];
          ConvertUInt64ToString(v, s);
          unsigned pos = MyStringLen(s);
          s[pos++] = ' ';
          if (moveBits != 0)
            s[pos++] = c;
          s[pos++] = 'B';
          s[pos++] = '/';
          s[pos++] = 's';
          s[pos++] = 0;
          _speedVal->setText(WCS2Q(s));
        }
      }
    }

    {
      UInt64 percent = 0;
      {
        if (IS_DEFINED_VAL(progressTotal))
        {
          percent = progressCompleted * 100;
          if (progressTotal != 0)
            percent /= progressTotal;
        }
      }
      if (percent != _prevPercentValue)
      {
        _prevPercentValue = percent;
        needSetTitle = true;
      }
    }

    {
      wchar_t s[64];

      ConvertUInt64ToString(completedFiles, s);
      if (_filesStr_Prev != s)
      {
        _filesStr_Prev = s;
        _filesVal->setText(WCS2Q(s));
      }

      s[0] = 0;
      if (IS_DEFINED_VAL(totalFiles))
      {
        MyStringCopy(s, L" / ");
        ConvertUInt64ToString(totalFiles, s + MyStringLen(s));
      }
      if (_filesTotStr_Prev != s)
      {
        _filesTotStr_Prev = s;
        _filesTotal->setText(WCS2Q(s));
      }
    }

    const UInt64 packSize   = CompressingMode ? outSize : inSize;
    const UInt64 unpackSize = CompressingMode ? inSize : outSize;

    if (IS_UNDEFINED_VAL(unpackSize) &&
        IS_UNDEFINED_VAL(packSize))
    {
      ShowSize(_processedVal, completed, _processed_Prev);
      ShowSize(_packedVal, UNDEFINED_VAL, _packed_Prev);
    }
    else
    {
      ShowSize(_processedVal, unpackSize, _processed_Prev);
      ShowSize(_packedVal, packSize, _packed_Prev);

      if (IS_DEFINED_VAL(packSize) &&
          IS_DEFINED_VAL(unpackSize) &&
          unpackSize != 0)
      {
        wchar_t s[32];
        UInt64 ratio = packSize * 100 / unpackSize;
        if (_ratio_Prev != ratio)
        {
          _ratio_Prev = ratio;
          ConvertUInt64ToString(ratio, s);
          MyStringCat(s, L"%");
          _ratioVal->setText(WCS2Q(s));
        }
      }
    }
  }

  if (needSetTitle || titleFileName_Changed)
    SetTitleText();

  if (status_Changed)
  {
    UString s = _status;
    ReduceString(s, kCurrentFileNameSizeLimit);
    _statusLabel->setText(US2Q(s));
  }

  if (curFilePath_Changed)
  {
    UString s1, s2;
    if (_isDir)
      s1 = _filePath;
    else
    {
      int slashPos = _filePath.ReverseFind_PathSepar();
      if (slashPos >= 0)
      {
        s1.SetFrom(_filePath, (unsigned)(slashPos + 1));
        s2 = _filePath.Ptr((unsigned)(slashPos + 1));
      }
      else
        s2 = _filePath;
    }
    ReduceString(s1, kCurrentFileNameSizeLimit);
    ReduceString(s2, kCurrentFileNameSizeLimit);
    s1.Add_LF();
    s1 += s2;
    _fileNameLabel->setText(US2Q(s1));
  }
}

// OnTimer : ported from CProgressDialog::OnTimer
void QtProgressDialog::onTimer()
{
  if (Sync.Get_Paused())
    return;
  UpdateStatInfo(false);
}

void QtProgressDialog::SetPauseText()
{
  _pauseButton->setText(Sync.Get_Paused()
      ? FmLang(IDS_CONTINUE, QStringLiteral("&Continue"))
      : FmLang(IDB_PAUSE, QStringLiteral("&Pause")));
  SetTitleText();
}

// OnPauseButton : ported from CProgressDialog::OnPauseButton (elapsed-time
// accounting across the pause boundary preserved).
void QtProgressDialog::onPause()
{
  bool paused = !Sync.Get_Paused();
  Sync.Set_Paused(paused);
  UInt32 curTime = ::GetTickCount();
  if (paused)
    _elapsedTime += (curTime - _prevTime);
  _prevTime = curTime;
  SetPauseText();
}

// OnButtonClicked(IDCANCEL) : ported from CProgressDialog. Pause while the
// confirmation box is up, then Sync.Set_Stopped(true) on "Yes" (which is the
// original's OnCancel()).
void QtProgressDialog::onCancel()
{
  const bool paused = Sync.Get_Paused();
  if (!paused)
    onPause();

  // IDS_PROGRESS_ASK_CANCEL (ProgressDialog2.cpp:1259); Yes/No are standard
  // buttons -> langIDs 406/407 (LangUtils.cpp kLangPairs).
  QMessageBox box(QMessageBox::Question, US2Q(_title),
      FmLang(IDS_PROGRESS_ASK_CANCEL, QStringLiteral("Are you sure you want to cancel?")),
      QMessageBox::Yes | QMessageBox::No, this);
  box.setDefaultButton(QMessageBox::No);
  if (QAbstractButton *yes = box.button(QMessageBox::Yes))
    yes->setText(FmLang(406, QStringLiteral("Yes")));
  if (QAbstractButton *no = box.button(QMessageBox::No))
    no->setText(FmLang(407, QStringLiteral("No")));
  box.exec();
  const QMessageBox::StandardButton res =
      static_cast<QMessageBox::StandardButton>(box.standardButton(box.clickedButton()));

  if (!paused)
    onPause();

  if (res == QMessageBox::Yes)
  {
    MessagesDisplayed = true;
    Sync.Set_Stopped(true); // == CProgressDialog::OnCancel()
  }
}

// ProcessWasFinished : ported from CProgressDialog::OnExternalCloseMessage.
// Runs on the GUI thread (queued from the worker base). Stops the timer, does a
// final UI sync, runs the GUI virt hook, shows Sync.FinalMessage / leaves the
// messages visible, then closes the modal dialog.
void QtProgressDialog::ProcessWasFinished()
{
  if (_externalCloseHandled)
    return;
  _externalCloseHandled = true;

  if (_timer)
    _timer->stop();

  UpdateStatInfo(true);

  // mirror: turn Cancel into a Close button, hide Pause/Background
  // IDCANCEL -> IDS_CLOSE (ProgressDialog2.cpp:1002).
  _cancelButton->setText(FmLang(IDS_CLOSE, QStringLiteral("Close")));
  _pauseButton->setVisible(false);

  ProcessWasFinished_GuiVirt();

  bool thereAreMessages;
  CProgressFinalMessage fm;
  {
    NSynchronization::CCriticalSectionLock lock(Sync._cs);
    thereAreMessages = !Sync.Messages.IsEmpty();
    fm = Sync.FinalMessage;
  }

  if (!fm.ErrorMessage.Message.IsEmpty())
  {
    MessagesDisplayed = true;
    if (fm.ErrorMessage.Title.IsEmpty())
      fm.ErrorMessage.Title = "7-Zip";
    if (!DisableUserQuestions)
      QMessageBox::critical(this, US2Q(fm.ErrorMessage.Title), US2Q(fm.ErrorMessage.Message));
  }
  else if (!thereAreMessages)
  {
    MessagesDisplayed = true;
    if (!fm.OkMessage.Message.IsEmpty())
    {
      if (fm.OkMessage.Title.IsEmpty())
        fm.OkMessage.Title = "7-Zip";
      if (!DisableUserQuestions)
        QMessageBox::information(this, US2Q(fm.OkMessage.Title), US2Q(fm.OkMessage.Message));
    }
  }

  // mirror: `if (!g_DisableUserQuestions) if (thereAreMessages && !cancelWasPressed)`
  // keep the dialog open so the user can read the message list; the (re-labelled)
  // Cancel->Close button closes it. In non-interactive mode we just close.
  if (!DisableUserQuestions)
  if (thereAreMessages)
  {
    MessagesDisplayed = true;
    disconnect(_cancelButton, &QPushButton::clicked, this, &QtProgressDialog::onCancel);
    connect(_cancelButton, &QPushButton::clicked, this, &QDialog::accept);
    UpdateMessagesDialog();
    // P.1 : with the delayed-show (QtProgressThreadVirt::Create) a FAST op may
    // finish before the dialog was ever shown. If we are keeping it open so the
    // user can read the messages, it must be made visible now (the Win32 FM
    // likewise forces the window when it has to display messages).
    if (!isVisible())
      show();
    return;
  }

  accept(); // End(0)
}
