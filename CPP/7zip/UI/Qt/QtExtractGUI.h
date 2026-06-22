// QtExtractGUI.h
//
// Qt/Linux analogue of GUI/ExtractGUI.{h,cpp} (milestone C.1b).
//
//   CThreadExtracting (CProgressThreadVirt subclass whose ProcessVirt() calls
//   Extract()) -> QtThreadExtracting (QtProgressThreadVirt subclass).
//
//   ExtractGUI(...) free function (sets up the callback<->dialog wiring and calls
//   Create(title, parent)) -> QtExtractGUI(...).
//
// The worker's ProcessVirt() mirrors CThreadExtracting::ProcessVirt() exactly: it
// just calls Extract(), passing the SAME callback object cast to the three
// callback parameters (ExtractCallbackSpec, ExtractCallbackSpec,
// FolderArchiveExtractCallback) — i.e. the IOpenCallbackUI, IExtractCallbackUI and
// IFolderArchiveExtractCallback faces of QtExtractCallback.

#ifndef ZIP7_INC_QT_EXTRACT_GUI_H
#define ZIP7_INC_QT_EXTRACT_GUI_H

#include "../../../Common/MyCom.h"
#include "../../../Common/MyString.h"

#include "../Common/Extract.h"
#include "../Common/LoadCodecs.h"

#include "QtProgressThreadVirt.h"

class QtExtractCallback;
class QtPasswordPrompt;
class QtOverwritePrompt;
class QtMemDialog;       // G.8a : memory-limit request prompt

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE


// === QtThreadExtracting : mirror of CThreadExtracting ========================
class QtThreadExtracting : public QtProgressThreadVirt
{
  HRESULT ProcessVirt() Z7_override;
public:
  CCodecs *codecs;
  QtExtractCallback *ExtractCallbackSpec;
  const CObjectVector<COpenType> *FormatIndices;
  const CIntVector *ExcludedFormatIndices;

  UStringVector *ArchivePaths;
  UStringVector *ArchivePathsFull;
  const NWildcard::CCensorNode *WildcardCensor;
  const CExtractOptions *Options;

  // Holds a reference to the callback via its IFolderArchiveExtractCallback face
  // (mirrors CThreadExtracting::FolderArchiveExtractCallback).
  CMyComPtr<IFolderArchiveExtractCallback> FolderArchiveExtractCallback;
  UString Title;

  // Out: the extraction statistics, like CThreadExtracting fills via Extract().
  CDecompressStat Stat;

  QtThreadExtracting():
      codecs(nullptr),
      ExtractCallbackSpec(nullptr),
      FormatIndices(nullptr),
      ExcludedFormatIndices(nullptr),
      ArchivePaths(nullptr),
      ArchivePathsFull(nullptr),
      WildcardCensor(nullptr),
      Options(nullptr)
    { Stat.Clear(); }
};


// === QtExtractGUI : mirror of the ExtractGUI() free function =================
// Sets up the callback<->dialog<->prompt wiring and runs the worker via
// Create(title, parent). The prompt objects are created on the GUI thread and
// passed in by the caller (so they have GUI-thread affinity for the blocking
// queued connection). Returns the extraction HRESULT.
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
    QtMemDialog *memPrompt,             // G.8a : may be null
    bool disableUserQuestions,
    QWidget *parent,
    CDecompressStat &statOut);


// === QtExtractGUI_ShowDialog : mirror of ExtractGUI()'s dialog stage =========
// Mirrors GUI/ExtractGUI.cpp lines ~204-247: builds a QtExtractDialog, copies
// options -> dialog fields (DirPath defaults to the current directory, ArcPath =
// the single archive), runs it, and on accept copies dialog fields -> options
// (OutputDir/OverwriteMode/PathMode/ElimDup/NtOptions.NtSecurity) and ->
// extractCallback->Password. Returns:
//   S_OK     : user accepted; options + callback updated.
//   E_ABORT  : user cancelled (mirror ExtractGUI.cpp's "return E_ABORT").
//   E_FAIL   : could not resolve the output directory path.
//
// In headless mode (disableUserQuestions), the dialog is not shown; the preset
// option/default values are accepted directly (and still persisted), so the
// offscreen test path keeps working while the real dialog path exists.
HRESULT QtExtractGUI_ShowDialog(
    UStringVector &archivePathsFull,
    CExtractOptions &options,
    QtExtractCallback *extractCallback,
    bool disableUserQuestions,
    QWidget *parent);

#endif
