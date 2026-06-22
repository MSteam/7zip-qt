// QtCompressGUI.h
//
// Qt/Linux analogue of GUI/UpdateGUI.{h,cpp} (milestone C.3a).
//
//   CThreadUpdating (CProgressThreadVirt subclass whose ProcessVirt() calls
//   UpdateArchive()) -> QtThreadUpdating (QtProgressThreadVirt subclass).
//
//   UpdateGUI(...) free function (the CInfo->CUpdateOptions+CProperty translation
//   in ShowDialog, then runs the worker) -> QtCompressGUI(...).
//
// The worker's ProcessVirt() mirrors CThreadUpdating::ProcessVirt() exactly: it
// calls UpdateArchive(...), passing the SAME callback object as both the
// IOpenCallbackUI and the IUpdateCallbackUI2 parameters.

#ifndef ZIP7_INC_QT_COMPRESS_GUI_H
#define ZIP7_INC_QT_COMPRESS_GUI_H

#include "../../../Common/MyCom.h"
#include "../../../Common/MyString.h"

#include "../../../Common/Wildcard.h"

#include "../Common/Update.h"
#include "../Common/LoadCodecs.h"

#include "QtCompressInfo.h" // NCompressDialog::CInfo (verbatim replica)
#include "QtProgressThreadVirt.h"

class QtUpdateCallback;
class QtPasswordPrompt;

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE


// === QtThreadUpdating : mirror of CThreadUpdating (UpdateGUI.cpp 38-62) ======
class QtThreadUpdating : public QtProgressThreadVirt
{
  HRESULT ProcessVirt() Z7_override;
public:
  CCodecs *codecs;
  const CObjectVector<COpenType> *formatIndices;
  const UString *cmdArcPath;
  QtUpdateCallback *UpdateCallback;
  NWildcard::CCensor *WildcardCensor;
  CUpdateOptions *Options;
  bool needSetPath;

  QtThreadUpdating():
      codecs(nullptr),
      formatIndices(nullptr),
      cmdArcPath(nullptr),
      UpdateCallback(nullptr),
      WildcardCensor(nullptr),
      Options(nullptr),
      needSetPath(true)
    {}
};


// === QtCompressGUI : mirror of the UpdateGUI() free function =================
// Performs the CInfo -> CUpdateOptions + CProperty translation that ShowDialog()
// does in UpdateGUI.cpp (lines ~446-540) — the update-mode action-set, the format
// index/type, the password, the path mode, the archive path, and SetOutProperties
// (Level/Method/EncryptionMethod/EncryptHeaders -> the "x"/"0"/"em"/"he"
// CProperty list) — then runs QtThreadUpdating via Create(title, parent).
//
// The dialog stage (showing QtCompressDialog) is done by the caller BEFORE this,
// so by the time we get here `info` is fully populated (mirroring how ShowDialog
// fills `di` and then translates it). Returns the compression HRESULT.
HRESULT QtCompressGUI(
    CCodecs *codecs,
    const NCompressDialog::CInfo &info,
    NWildcard::CCensor &censor,
    CUpdateOptions &options,
    QtUpdateCallback *updateCallback,
    QtPasswordPrompt *passwordPrompt,
    bool disableUserQuestions,
    QWidget *parent);


// === QtCompressGUI_ShowDialog : mirror of ShowDialog()'s dialog stage ========
// Builds a QtCompressDialog seeded from `codecs->Formats` + `info` (the default
// archive name in info.ArcPath, the preselected info.FormatIndex), runs it, and
// on accept copies the dialog's filled Info back into `info`. Returns:
//   S_OK     : user accepted; info fully populated.
//   E_ABORT  : user cancelled (mirror ShowDialog's "return E_ABORT").
//
// In headless mode (disableUserQuestions) the dialog is not exec()'d; the
// preset/default values are accepted directly (and still persisted), with env
// overrides matching the extract harness:
//   SEVENZQT_LEVEL, SEVENZQT_FORMAT (format name), SEVENZQT_UPDATEMODE,
//   SEVENZQT_PATHMODE_C, SEVENZQT_PASSWORD, SEVENZQT_ENCHEADERS (0/1),
//   SEVENZQT_ENCMETHOD (e.g. "AES-256" / "ZipCrypto").
HRESULT QtCompressGUI_ShowDialog(
    CCodecs *codecs,
    NCompressDialog::CInfo &info,
    bool disableUserQuestions,
    QWidget *parent,
    // G.7d: the single source file's leaf name (mirror dialog.OriginalFileName,
    // UpdateGUI.cpp:423). Used by SetArchiveName's KeepName branch for single-file
    // gzip/bzip2; empty for the multi-file / directory case.
    const UString &originalFileName = UString());


// === QtUpdateGUI : mirror of the original UpdateGUI() free function ===========
// This is the target-shape entry the unified 7zqt dispatch calls (mirroring
// GUI.cpp Main2()'s update branch, which calls UpdateGUI(codecs, formatIndices,
// ArchiveName, Censor, UpdateOptions, ShowDialog, ...)). It takes the REAL parsed
// CUpdateOptions (already carrying the -m.../-t.../-p switch state via
// MethodMode.Properties + MethodMode.Type), exactly like the original.
//
// It mirrors UpdateGUI.cpp's body:
//   * if showDialog: run the dialog stage (seed an NCompressDialog::CInfo from the
//     parsed CUpdateOptions, exec QtCompressDialog via QtCompressGUI_ShowDialog,
//     then translate the filled CInfo back into `options`), needSetPath = false;
//   * else: use the parsed `options` as-is, needSetPath = true (the worker's
//     UpdateArchive() then calls InitFormatIndex()/SetArcPath() over `formatIndices`).
// Then it runs QtThreadUpdating via Create(title, parent) and returns its HRESULT.
//
// disableUserQuestions mirrors the original g_DisableUserQuestions: when true the
// progress dialog runs non-interactively and auto-closes.
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
    QWidget *parent);

#endif
