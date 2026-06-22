// QtProgressDialog.h
//
// Qt/Linux port of CPP/7zip/UI/FileManager/ProgressDialog2.h : CProgressDialog.
//
// This QDialog mirrors the OBSERVABLE behaviour of the Win32 CProgressDialog:
// it OWNS a public `CProgressSync Sync;` (exactly like CProgressDialog::Sync),
// drives the UI from a QTimer (in place of the Win32 WM_TIMER / OnTimer), and
// ports the elapsed/remaining/speed/percent arithmetic of
// CProgressDialog::UpdateStatInfo() faithfully (the CU64ToI32Converter for the
// progress-bar range, the speed smoothing via _prevSpeed_MoveBits, etc.).
//
// THREADING RULE (mirrors the original):
//   The worker thread ONLY touches `Sync` (which is internally locked by its
//   own _cs critical section). It must NEVER touch the QWidgets here directly.
//   All widget updates happen on the GUI thread: the QTimer tick reads the
//   Sync fields, and the worker->GUI "finished" notification is marshalled as a
//   queued call to ProcessWasFinished() (mirrors the original PostMsg(kCloseMessage)).

#ifndef ZIP7_INC_QT_PROGRESS_DIALOG_H
#define ZIP7_INC_QT_PROGRESS_DIALOG_H

#include <QtWidgets/QDialog>

#include "ProgressSync.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTimer;
QT_END_NAMESPACE

class QtProgressDialog : public QDialog
{
  Q_OBJECT

  // --- ported 1:1 from CProgressDialog ---------------------------------------
  // CU64ToI32Converter: maps a 64-bit progress range onto the (small) int range
  // a progress bar accepts. Copied verbatim from ProgressDialog2.h.
  class CU64ToI32Converter
  {
    unsigned _numShiftBits;
    UInt64 _range;
  public:
    CU64ToI32Converter(): _numShiftBits(0), _range(1) {}
    void Init(UInt64 range)
    {
      _range = range;
      // Windows CE doesn't like big number for ProgressBar.
      for (_numShiftBits = 0; range >= ((UInt32)1 << 15); _numShiftBits++)
        range >>= 1;
    }
    int Count(UInt64 val)
    {
      int res = (int)(val >> _numShiftBits);
      if (val == _range)
        res++;
      return res;
    }
  };

  CU64ToI32Converter _progressConv;
  UInt64 _progressBar_Pos;
  UInt64 _progressBar_Range;

  bool _isDir;

  UString _titleFileName;
  UString _filePath;
  UString _status;

  UInt32 _prevTime;
  UInt64 _elapsedTime;

  UInt64 _prevPercentValue;
  UInt64 _prevElapsedSec;
  UInt64 _prevRemainingSec;

  UInt64 _totalBytes_Prev;
  UInt64 _processed_Prev;
  UInt64 _packed_Prev;
  UInt64 _ratio_Prev;

  UString _filesStr_Prev;
  UString _filesTotStr_Prev;

  unsigned _prevSpeed_MoveBits;
  UInt64 _prevSpeed;

  unsigned _numPostedMessages; // number of Sync.Messages already shown
  UInt64 _title_arg;           // remembered for SetTitleText()

  // G.8b : mirrors CProgressDialog::_messageStrings — the RAW message text of
  // each displayed row (one per row, AFTER the '\n'-split), used by
  // CopyToClipboard so the clipboard carries the message text without the leading
  // number column (matching the original, which copies _messageStrings[index]).
  UStringVector _messageStrings;

  bool _errorsWereDisplayed;
  bool _externalCloseHandled;

  // --- Qt widgets ------------------------------------------------------------
  QProgressBar *_progressBar;
  QLabel  *_statusLabel;
  QLabel  *_fileNameLabel;
  QLabel  *_elapsedVal;
  QLabel  *_remainingVal;
  QLabel  *_totalVal;
  QLabel  *_speedVal;
  QLabel  *_processedVal;
  QLabel  *_packedVal;
  QLabel  *_ratioVal;
  QLabel  *_filesVal;
  QLabel  *_filesTotal;
  QLabel  *_errorsVal;
  QListWidget *_messageList;
  QPushButton *_cancelButton;
  QPushButton *_pauseButton;

  QTimer *_timer;

  void buildUi();
  void SetProgressRange(UInt64 range);  // ported from CProgressDialog
  void SetProgressPos(UInt64 pos);      // ported from CProgressDialog
  void ShowSize(QLabel *label, UInt64 val, UInt64 &prev); // ported
  void UpdateStatInfo(bool showAll);    // ported from CProgressDialog
  void SetTitleText();                  // ported from CProgressDialog
  void UpdateMessagesDialog();          // ported from CProgressDialog (message drain)
  void SetPauseText();

  // G.8e : ported from CProgressDialog::AddMessage / AddMessageDirect — split a
  // message on embedded '\n' into separate rows (only the FIRST is numbered) and
  // append each to _messageList.
  void AddMessage(const UString &message);
  void AddMessageDirect(const UString &message, bool needNumber);

  unsigned _numMessages; // mirrors CProgressDialog::_numMessages (message-row numbering)

protected:
  // G.8b : ported from CProgressDialog::OnNotify(LVN_KEYDOWN) — Ctrl+C / Ctrl+Ins
  // copy the selected-or-all message rows; Ctrl+A selects all. Routed through the
  // event filter installed on _messageList.
  bool eventFilter(QObject *obj, QEvent *ev) override;

private:
  // G.8b : ported from CProgressDialog::CopyToClipboard — copy selected-or-all
  // raw message strings (verbatim _messageStrings, NOT the displayed number col).
  void CopyToClipboard();

private slots:
  void onTimer();
  void onCancel();
  void onPause();

public:
  CProgressSync Sync;          // public, exactly like CProgressDialog::Sync
  bool CompressingMode;        // mirrors CProgressDialog::CompressingMode
  bool MessagesDisplayed;      // mirrors CProgressDialog::MessagesDisplayed

  // Mirrors the original's `extern bool g_DisableUserQuestions`. When true the
  // dialog never blocks on user questions: it shows no message boxes and does
  // not wait for a manual Close at the end (it just closes). Used for headless /
  // non-interactive runs. Defaults to false (interactive, like the GUI build).
  bool DisableUserQuestions;

  explicit QtProgressDialog(QWidget *parent = nullptr);
  ~QtProgressDialog() override;

  void SetTitle(const UString &title);

  // G.8b/G.8e : headless test accessors (offscreen self-test only). They expose
  // the message-list state so the '\n'-split row layout and the clipboard
  // copy-selected-or-all logic are verifiable without driving the live dialog.
  int messageRowCount() const;
  UString messageRowText(int row) const;     // the displayed row text (number + msg)
  UString messageRawText(int row) const;      // the raw _messageStrings[row]
  void selectMessageRow(int row);             // mark a row selected (for copy test)
  void addMessage_ForTest(const UString &m) { AddMessage(m); }
  void copyMessagesToClipboard_ForTest() { CopyToClipboard(); }

  /* how it works (mirrors ProgressDialog2.h lines ~269-280):
     1) the working thread calls (via the thread base) a queued ProcessWasFinished()
        that runs on the CProgressDialog (GUI) thread;
     2) ProcessWasFinished() calls ProcessWasFinished_GuiVirt() and shows the
        special results window (Sync.FinalMessage) in the GUI thread with this
        dialog as parent. */
  virtual void ProcessWasFinished_GuiVirt() {}

  // True once ProcessWasFinished() has begun handling the worker's "finished"
  // notification (set at the very top of ProcessWasFinished, before any results
  // window is shown). The delayed-show timer in QtProgressThreadVirt::Create()
  // checks this so a fast op never flashes the progress dialog at 100%.
  bool wasExternallyClosed() const { return _externalCloseHandled; }

public slots:
  // Marshalled onto the GUI thread by the worker base (queued connection).
  // Mirrors CProgressDialog::OnExternalCloseMessage(): stops the timer, does a
  // final UI sync, shows Sync.FinalMessage / drains Sync.Messages, then closes.
  void ProcessWasFinished();

private:
  UString _title;
};

#endif
