// ArchiveOpenHelper.h
// ----------------------------------------------------------------------------
// Milestone B.2 : a small reusable helper that opens an archive FILE into a root
// IFolderFolder using CAgent (the B.0 open pattern + a headless password
// callback). Factored out of main_archive_browser.cpp so BOTH the archive-only
// browser and the new FS browser (which opens an archive on the fly when the
// user activates an archive file in the filesystem view) share ONE open path.
//
// The returned `agentHolder` (the CAgent as IUnknown) MUST be kept alive for as
// long as `rootFolder` is used - the folder is a view onto the open archive.
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_ARCHIVE_OPEN_HELPER_H
#define ZIP7_INC_QT_FM_ARCHIVE_OPEN_HELPER_H

#include "../../../../Common/MyCom.h"
#include "../../../../Common/MyString.h"
#include "../../FileManager/IFolder.h"

// P.1 : forward-declared so the helper can take an OPTIONAL progress/cancel sink
// without pulling the Qt progress headers into every TU that opens an archive.
// When non-null, the open callback drives Set_Num*Total/Cur on it during the
// (slow) format scan and returns its CheckStop() HRESULT (E_ABORT on cancel) to
// abort agent->Open — the faithful mirror of the Win32 FM's open-progress.
class CProgressSync;

// Encrypted-FM : forward-declared so the helper can take an OPTIONAL GUI-thread
// password prompt without pulling QtExtractPrompts.h into every TU. When non-null
// AND the archive turns out to be header-encrypted, the open callback prompts the
// user (CPasswordDialog analogue) once and caches the answer — the faithful mirror
// of COpenCallbackImp::CryptoGetTextPassword (FileManager/OpenCallback.cpp:63). A
// Cancel returns E_ABORT so the Open fails cleanly. When null (headless / no
// prompt wired), the old behaviour stands: only an already-known password is used.
class QtPasswordPrompt;

// Opens `arcPath` as an archive.
//   on success: S_OK, rootFolder = archive root IFolderFolder,
//               agentHolder = the owning CAgent (keep alive while browsing).
//   on failure (not an archive / open error): an error HRESULT, outputs null.
// `password` may be empty (used only if the archive is encrypted).
//
// G.3d : MULTI-VOLUME archives (archive.7z.001 + .002 + ...) open transparently —
// the internal open callback ALSO implements IArchiveOpenVolumeCallback, so when the
// engine detects a volume set it pulls in the sibling parts from the SAME directory
// as `arcPath` (GetStream resolves <dir>/<volName> via CInFileStream; a missing tail
// part fails the open cleanly). This applies ONLY to the file-path open below; the
// stream variant (a nested sub-archive) is never an on-disk volume set.
//
// B.5b : `isUpdatable` (optional out) reports whether the opened archive can be
// modified in place — the faithful combination CAgent::CanUpdate() &&
// !CAgent::IsThere_ReadOnlyArc() (the latter consults Formats[].UpdateEnabled /
// arc.IsReadOnly). Read-only formats (e.g. .gz to a single stream, or a format
// with UpdateEnabled=false) come back false and the FM keeps refusing in-place
// Delete/Add on them.
// Encrypted-FM : `passwordPrompt` (optional) lets the open callback ask the user
// for a password when the archive is header-encrypted and none was pre-supplied
// (see COpenCallbackFm::CryptoGetTextPassword). THREADING: the callback runs on
// the GUI thread in the synchronous fast-path and on the worker thread in the
// threaded path; it picks the right Qt connection by comparing the current thread
// to the prompt's affinity, so the SAME prompt object is safe to pass from both.
// G.3b : `outEncrypted` (optional) reports whether the archive was header-encrypted
// (the engine asked the open callback for a password) — the faithful mirror of
// COpenResult::Encrypted (PanelItemOpen.cpp:462,484). The caller uses it on a FAILED
// open to pick IDS_CANT_OPEN_ENCRYPTED_ARCHIVE vs IDS_CANT_OPEN_ARCHIVE, exactly as
// CPanel::OpenAsArc_Msg branches on opRes.Encrypted. Set even when the open fails.
HRESULT OpenArchiveAsFolder(const UString &arcPath, const UString &password,
    bool passwordDefined,
    CMyComPtr<IFolderFolder> &rootFolder,
    CMyComPtr<IUnknown> &agentHolder,
    bool *isUpdatable = nullptr,
    CProgressSync *openSync = nullptr,
    QtPasswordPrompt *passwordPrompt = nullptr,
    bool *outEncrypted = nullptr);

// G.2b : the STREAM variant — opens a nested archive that lives INSIDE an already
// open archive. The faithful mirror of CPanel::OpenAsArc's inStream branch
// (PanelItemOpen.cpp:456): the caller QIs the CURRENT CAgentFolder for
// IInArchiveGetStream, GetStream(index)s the sub-archive's IInStream, and passes
// it here; this opens FROM THE STREAM (CAgent::Open(inStream, ...), Agent.cpp:1609)
// rather than from a path. `virtualName` is the entry name (used only as the
// archive's filePath hint for format detection, exactly as OpenAsArc passes the
// virtual path). The OUTER agent (whose CAgentFolder owns `inStream`) MUST be kept
// alive by the caller for as long as `rootFolder` is browsed — the sub-stream is a
// view onto the outer archive.
//
// Same callback / password / isUpdatable plumbing as the FILE variant; reuses the
// SAME COpenCallbackFm. A nested in-memory/stream archive is not writable in place,
// so `isUpdatable` comes back false (the engine's CanUpdate gate handles this) and
// the FM keeps refusing in-place Delete/Add on it (G.2b is read/browse/extract only).
//   on success: S_OK, rootFolder = nested archive root, agentHolder = the inner CAgent.
//   on failure: S_FALSE (the sub-stream is not an archive — caller falls back to
//               extract+xdg-open), E_ABORT (password cancelled), or another error.
HRESULT OpenArchiveStreamAsFolder(IInStream *inStream, const UString &virtualName,
    const UString &password, bool passwordDefined,
    CMyComPtr<IFolderFolder> &rootFolder,
    CMyComPtr<IUnknown> &agentHolder,
    bool *isUpdatable = nullptr,
    QtPasswordPrompt *passwordPrompt = nullptr,
    bool *outEncrypted = nullptr);

#endif
