// QtHashGUI.h
//
// Qt/Linux analogue of GUI/HashGUI.{h,cpp} (milestone C.4).
//
//   CHashCallbackGUI (a CProgressThreadVirt subclass that is ALSO the
//   IHashCallbackUI, whose ProcessVirt() calls HashCalc()) is split here into:
//
//     * QtHashCallback        - the IHashCallbackUI / IDirItemsCallback faces,
//                               mirroring CHashCallbackGUI's callback methods
//                               (Scan/Set*/GetStream/AfterLastFile etc.). It
//                               drives the shared CProgressSync and accumulates
//                               the result text via AddHashBundleRes().
//
//     * QtThreadHashCalc      - the QtProgressThreadVirt worker, whose
//                               ProcessVirt() calls HashCalc() over the censor,
//                               exactly like CHashCallbackGUI::ProcessVirt().
//                               (In the original these are one object; the Qt
//                               progress core keeps the thread base and the
//                               IHashCallbackUI in one class too — see below.)
//
// Because the Qt progress core puts ProcessWasFinished_GuiVirt() on the dialog
// (QtProgressDialog), the results-window stage of CHashCallbackGUI is realized
// by a small QtProgressDialog subclass (QtHashProgressDialog) created in
// QtHashGUI() — mirroring CHashCallbackGUI::ProcessWasFinished_GuiVirt() ->
// ShowHashResults(PropNameValPairs, *this).

#ifndef ZIP7_INC_QT_HASH_GUI_H
#define ZIP7_INC_QT_HASH_GUI_H

#include "../../../Common/MyString.h"

#include "../Common/HashCalc.h"
#include "../Common/Property.h"   // CProperty
#include "../Common/LoadCodecs.h"

#include "QtProgressThreadVirt.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE


// CPropNameValPairs : same typedef the original GUI uses (HashGUI.h line 15).
typedef CObjectVector<CProperty> CPropNameValPairs;

// === result aggregation : mirror of HashGUI.cpp =============================
// These are faithful ports of the original free functions; the result strings
// they build (uppercase hex digests, the "<METHOD> for data:" lines, the
// Folders/Files/Size properties) are what the results window shows.
void Qt_AddValuePair(CPropNameValPairs &pairs, const char *name, UInt64 value);
void Qt_AddSizeValue(UString &s, UInt64 value);
void Qt_AddSizeValuePair(CPropNameValPairs &pairs, const char *name, UInt64 value);
void Qt_AddHashBundleRes(CPropNameValPairs &s, const CHashBundle &hb);


// === QtThreadHashCalc : mirror of CHashCallbackGUI ==========================
// One object that is BOTH the QtProgressThreadVirt worker (ProcessVirt() calls
// HashCalc) AND the IHashCallbackUI the engine drives, exactly like the
// original CHashCallbackGUI. The accumulated result pairs (filled in
// AfterLastFile, like the original) are read by QtHashGUI() to seed the
// results window.
class QtThreadHashCalc:
    public QtProgressThreadVirt,
    public IHashCallbackUI
{
  UInt64 NumFiles;
  bool _curIsFolder;
  UString FirstFileName;

  HRESULT ProcessVirt() Z7_override;

  // Install the results-window progress dialog (QtHashProgressDialog) so that,
  // when the run finishes (and was not aborted), ProcessWasFinished_GuiVirt()
  // shows QtHashResultsDialog — mirroring CHashCallbackGUI's own override.
  QtProgressDialog *CreateProgressDialog(QWidget *parentWindow) Z7_override;

  void AddErrorMessage(HRESULT systemError, const wchar_t *name)
  {
    Sync().AddError_Code_Name(systemError, name);
  }

public:
  const NWildcard::CCensor *censor;
  const CHashOptions *options;

  DECL_EXTERNAL_CODECS_LOC_VARS_DECL

  // The accumulated name/value result pairs (HashGUI.cpp PropNameValPairs).
  CPropNameValPairs PropNameValPairs;

  Z7_IFACE_IMP(IDirItemsCallback)
  Z7_IFACE_IMP(IHashCallbackUI)

  QtThreadHashCalc():
      NumFiles(0), _curIsFolder(false),
      censor(nullptr), options(nullptr)
    {}
};


// === QtHashGUI : mirror of the HashCalcGUI() free function ==================
// Runs the hash worker with live progress; when finished (and not aborted) it
// shows the QtHashResultsDialog with the accumulated PropNameValPairs, exactly
// like HashCalcGUI -> CHashCallbackGUI::ProcessWasFinished_GuiVirt() ->
// ShowHashResults. Returns the HashCalc() HRESULT.
//
// disableUserQuestions: headless / offscreen path — the progress dialog
// auto-closes and the results dialog is not shown modally (the formatted result
// text is still printed to stdout by main_hash for the test harness).
HRESULT QtHashGUI(
    DECL_EXTERNAL_CODECS_LOC_VARS
    const NWildcard::CCensor &censor,
    const CHashOptions &options,
    bool disableUserQuestions,
    QWidget *parent,
    CPropNameValPairs &resultPairsOut);

#endif
