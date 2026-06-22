// QtFsDnd.h
// ----------------------------------------------------------------------------
// Milestone B.4b : drag & drop for the filesystem panels, Qt-native
// (QDrag / QMimeData with "text/uri-list" = file:// URIs).
//
// This is the Qt-portable analogue of PanelDrag.cpp. It does NOT port the Win32
// OLE machinery (IDataObject / IDropSource / IDropTarget / DoDragDrop): Qt's
// QDrag + text/uri-list replaces all of it. What it DOES mirror is PanelDrag's
// move-vs-copy DECISION and the source/target file semantics:
//
//   * GetEffect_ForKeys (PanelDrag.cpp): Ctrl = COPY, Shift = MOVE, Ctrl+Shift /
//     Alt = LINK (which 7-Zip rejects -> we reject too). No modifier = default.
//   * GetEffect (PanelDrag.cpp): for the DEFAULT action (no modifier), MOVE is
//     preferred when source and destination are on the same "volume" (IsItSameDrive
//     checks that every source path is prefixed by the destination drive). On Linux
//     there are no drive letters; we approximate "same volume" with stat(2) st_dev
//     equality of the source parent vs. the destination dir. Otherwise COPY.
//
// Qt feeds us a Qt::DropAction (Qt computes Copy vs Move from the same modifiers
// + the source's supportedActions); MapDropAction() folds that down to the 7-Zip
// moveMode (0=copy, 1=move) and applies the same-volume default when Qt hands us
// an ambiguous/default action.
//
// The actual file move/copy is done ENTIRELY by the B.4a QtFsCopyWorker
// (IFolderOperations::CopyTo). We never hand-roll a copy:
//
//   * INTERNAL drop (source is one of our panels, same process): the source
//     panel's folder + its selected indices -> CopyTo(dest). Identical to Copy
//     To / Move To.
//   * EXTERNAL drop (a uri-list of filesystem paths from another app): we group
//     the paths by parent directory, bind a temporary CFSFolder at each parent,
//     resolve each item's index by name (kpidName), and CopyTo(dest). Same worker.
//
// Dropping onto / INTO an ARCHIVE panel adds the dropped items to that archive
// (B.5b add-on-drop). Dragging FROM an archive panel extracts the selection to a
// temp dir and offers it as a COPY (B.5c / G.6 extract-on-drag); move-OUT
// (extract + delete-from-archive) remains the one deferred drag item.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_FS_DND_H
#define ZIP7_INC_QT_FM_FS_DND_H

#include <QtCore/Qt>

#include "../../../../Common/MyString.h"

class QMimeData;
class QWidget;
class QtOverwritePrompt;

namespace QtFsDnd {

// Build a Qt MimeData with text/uri-list = file:// URIs of `paths` (absolute FS
// paths). Caller owns the returned object (hand it to QDrag::setMimeData, which
// takes ownership). Returns nullptr if `paths` is empty.
QMimeData *MakeUriListMime(const UStringVector &paths);

// G.6d : the right-button-drag marker. The Win32 FM signals a right-button drag to
// the drop target via its private GetTransfer clipboard format (the
// k_SourceFlags_RightButton bit, PanelDrag.cpp:2424); on Linux the Qt-native analog
// is a custom MIME entry stamped onto the drag's QMimeData by the source panel when
// the drag began with the right (or middle) button (PanelDrag.cpp:2401-2402). The
// drop handler reads it to decide whether to pop the Copy/Move/Add action menu
// (Drag_OnContextMenu) instead of inferring the action from the Qt DropAction.
// MarkRightButtonDrag() stamps the marker; IsRightButtonDrag() reads it back.
void MarkRightButtonDrag(QMimeData *mime);
bool IsRightButtonDrag(const QMimeData *mime);

// Extract the absolute filesystem paths carried by a dropped QMimeData's
// text/uri-list (file:// URIs only; non-file URIs are skipped). Appends to `out`.
// Returns the number of file paths found.
unsigned UriListToPaths(const QMimeData *mime, UStringVector &out);

// Decide moveMode (0=copy, 1=move) from Qt's drop action, mirroring PanelDrag's
// GetEffect / GetEffect_ForKeys:
//   * action == Qt::MoveAction -> move (Shift, or Qt's same-volume default)
//   * action == Qt::CopyAction -> copy (Ctrl, or cross-volume default)
//   * anything else            -> fall back to the same-volume rule below.
// `srcPaths` are the items being dropped, `destDir` the target directory; when
// the action is ambiguous we move iff every source shares destDir's volume
// (st_dev), else copy. Returns true=move, false=copy.
bool MapDropAction(Qt::DropAction action,
    const UStringVector &srcPaths, const UString &destDir);

// The PanelDrag IsItSameDrive analogue (Linux: st_dev equality). True iff every
// path in `srcPaths` lives on the same filesystem volume as `destDir`.
bool IsSameVolume(const UStringVector &srcPaths, const UString &destDir);

// Result of a scripted/dispatched drop (for the offscreen self-check transcript).
struct DropResult
{
  bool Ok;
  bool MoveMode;
  unsigned NumGroups;   // number of distinct source folders processed
  unsigned NumItems;    // total items copied/moved
  // G.6e : the first non-S_OK worker HRESULT (S_OK when every group succeeded), so
  // the caller can surface a real error while staying silent on E_ABORT (user cancel),
  // exactly as PanelDrag's MessageBox_Error_HRESULT skips DRAGDROP_S_CANCEL/E_ABORT.
  HRESULT LastError;
  DropResult(): Ok(false), MoveMode(false), NumGroups(0), NumItems(0), LastError(S_OK) {}
};

// Run the actual copy/move of `srcPaths` into `destDir` via the B.4a
// QtFsCopyWorker (CopyTo). Groups srcPaths by parent dir, binds a CFSFolder per
// group, resolves indices by name, and runs the worker once per group.
// `destDir` must be an existing FS directory; it is normalized to a trailing
// separator. `moveMode` selects copy (false) vs move (true). `prompt` is the
// GUI-thread overwrite prompt (may be null in headless). `parent`/`headless`
// match the B.4a worker.Create()/DisableUserQuestions wiring.
//
// This is the EXTERNAL-style path; the INTERNAL drop calls the worker directly
// with the source panel's own folder+indices (see QtFileManagerWindow::dropOnto).
DropResult CopyPathsInto(const UStringVector &srcPaths, const UString &destDir,
    bool moveMode, QtOverwritePrompt *prompt, QWidget *parent, bool headless);

} // namespace QtFsDnd

#endif
