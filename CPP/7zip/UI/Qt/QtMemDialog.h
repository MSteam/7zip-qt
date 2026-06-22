// QtMemDialog.h
//
// G.8a : the Qt/Linux analogue of FileManager/MemDialog.{h,cpp} (CMemDialog) —
// the "this archive needs more decompression memory than the allowed limit"
// request dialog that CExtractCallbackImp::RequestMemoryUse shows.
//
// CMemDialog (the Win32 dialog) shows the required / current-limit / RAM sizes,
// lets the user raise the GB limit (and optionally remember it for later
// operations) and choose Allow archive unpacking vs Skip archive unpacking; on
// OK it reports back Limit_GB / NeedSave / Remember / SkipArc.
//
// This object is the GUI-thread prompt sibling of QtPasswordPrompt /
// QtOverwritePrompt (QtExtractPrompts.h): it follows the SAME threading contract.
// The worker thread (inside RequestMemoryUse) fills the in-fields, then calls
//   QMetaObject::invokeMethod(memPrompt, "Ask", Qt::BlockingQueuedConnection, ...)
// which queues Ask() onto the GUI thread and BLOCKS the worker until the user
// answers; Ask() shows the real modal QDialog (parented to the progress dialog)
// and writes the result fields the worker reads after the blocking call returns.
// This mirrors the Win32 build's `dialog.Create(*ProgressDialog)` blocking the
// worker thread.
//
// Headless fallback (offscreen / minimal QPA / no display): Ask() does NOT show a
// dialog (there is no human to answer); it returns Allow with the limit unchanged,
// exactly the non-interactive default the engine itself uses when
// g_DisableUserQuestions is set (RequestMemoryUse leaves the default k_Allow and
// returns S_OK without showing CMemDialog). A test override
// (SEVENZQT_MEM_SKIP=1) lets the harness drive the Skip-archive answer.
//
// The result-carrying fields mirror CMemDialog's public members verbatim
// (Required_GB, Limit_GB, TestMode, ArcPath, FilePath, ShowRemember, in;
// Limit_GB, NeedSave, Remember, SkipArc, plus an Accepted flag, out).

#ifndef ZIP7_INC_QT_MEM_DIALOG_H
#define ZIP7_INC_QT_MEM_DIALOG_H

#include <QtCore/QObject>
#include <QtCore/QString>

#include "../../../Common/MyTypes.h"   // UInt32

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

// GUI-thread prompt; analogue of CMemDialog as driven by RequestMemoryUse.
class QtMemDialog : public QObject
{
  Q_OBJECT
public:
  explicit QtMemDialog(QObject *parent = nullptr) : QObject(parent), _parentWidget(nullptr) {}

  // The progress dialog acts as the modal parent (mirrors dialog.Create(*ProgressDialog)).
  void SetParentWidget(QWidget *w) { _parentWidget = w; }

  // Invoked on the GUI thread (BlockingQueuedConnection from the worker). All the
  // in/out values are passed by pointer in one struct-shaped argument list so the
  // worker can read the user's choices back after the blocking call. Mirrors
  // CMemDialog's member set; see the field comments below.
  //
  //   in : requiredGB, limitGB (current allowed), testMode, arcPath, filePath,
  //        showRemember (CMemDialog.ShowRemember).
  //   out: *outLimitGB, *outNeedSave, *outRemember, *outSkipArc, *outAccepted.
  // (int / int* are used — not unsigned — to match the proven Q_ARG pointer-arg
  // pattern of QtExtractPrompts' BlockingQueuedConnection slots; a GB value never
  // exceeds the spin's 1<<14 max so the signedness is immaterial.)
  Q_INVOKABLE void Ask(
      int requiredGB, int limitGB,
      bool testMode, QString arcPath, QString filePath,
      bool showRemember,
      int *outLimitGB, bool *outNeedSave, bool *outRemember,
      bool *outSkipArc, bool *outAccepted);

private:
  QWidget *_parentWidget;
};

#endif
