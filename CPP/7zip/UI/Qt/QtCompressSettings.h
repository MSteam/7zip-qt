// QtCompressSettings.h
//
// Qt/Linux analogue of Common/ZipRegistry.cpp's NCompression registry store
// (milestone C.3a). It mirrors NCompression::CInfo / NCompression::CFormatOptions
// and their Save()/Load() semantics, but persists over QSettings instead of the
// Win32 registry (we do NOT compile ZipRegistry.cpp, which is Win32-only).
//
// Upstream (ZipRegistry.cpp, namespace NCompression) keeps the compression
// settings under
//   HKCU\Software\7-Zip\Compression
// with value names: Level, Archiver (== ArcType), ShowPassword, EncryptHeaders,
// ArcHistory (the archive-path MRU), and a per-format "Options" subkey holding
// Method / EncryptionMethod / Options / Dictionary / Order / etc.
//
// Here we keep the SAME logical fields under
//   QSettings("7-Zip", "7-Zip") -> group "Compression"
// with key names mirroring the upstream value names. The struct keeps the 7-Zip
// idiom (a CInfo-like struct + CFormatOptions vector + Load()/Save(), CBoolPair
// fields), the exact shape used by QtExtractSettings (NExtractQt) so the two
// stores sit side by side.
//
// C.3a SCOPE: the dialog only writes Level / ArcType / ShowPassword /
// EncryptHeaders / ArcPaths, plus the per-format EncryptionMethod (so the chosen
// zip encryption survives). The advanced per-format CFormatOptions numeric fields
// (Dictionary, Order, BlockLogSize, NumThreads, time prec...) are persisted with
// full fidelity to the registry layout but are written at their auto sentinels by
// C.3a (no UI sets them yet); C.3b's advanced combos will start populating them.

#ifndef ZIP7_INC_QT_COMPRESS_SETTINGS_H
#define ZIP7_INC_QT_COMPRESS_SETTINGS_H

#include "../../../Common/MyTypes.h"
#include "../../../Common/MyString.h"

// NCompressionQt: the Qt-backed mirror of NCompression's registry store. Kept in
// its own namespace (not NCompression) so it does not collide with the engine's
// NCompression (ZipRegistry.h) linked in via the engine archive.
namespace NCompressionQt
{
  // Mirrors NCompression::CFormatOptions (ZipRegistry.h lines 83-134). Same
  // fields, same auto/-1 sentinels, same load/save behavior.
  struct CFormatOptions
  {
    UInt32 Level;
    UInt32 Dictionary;
    UInt32 Order;
    UInt32 BlockLogSize;
    UInt32 NumThreads;

    UInt32 TimePrec;
    CBoolPair MTime;
    CBoolPair ATime;
    CBoolPair CTime;
    CBoolPair SetArcMTime;

    UString FormatID; // upstream uses CSysString; UString is the Qt-side analogue
    UString Method;
    UString Options;
    UString EncryptionMethod;
    UString MemUse;

    void ResetForLevelChange()
    {
      BlockLogSize = NumThreads = Level = Dictionary = Order = (UInt32)(Int32)-1;
      Method.Empty();
    }
    CFormatOptions()
    {
      TimePrec = (UInt32)(Int32)-1;
      ResetForLevelChange();
    }
  };

  // Mirrors NCompression::CInfo (ZipRegistry.h lines 136-156).
  struct CInfo
  {
    UInt32 Level;
    bool ShowPassword;
    bool EncryptHeaders;

    CBoolPair NtSecurity;
    CBoolPair AltStreams;
    CBoolPair HardLinks;
    CBoolPair SymLinks;
    CBoolPair PreserveATime;

    UString ArcType;
    UStringVector ArcPaths; // archive-path MRU history (newest first)

    CObjectVector<CFormatOptions> Formats;

    void Save() const;
    void Load();

    // Convenience accessors mirroring CCompressDialog::FindRegistryFormat /
    // Get_FormatOptions: find (or create) the per-format options block by name.
    int FindFormat(const UString &name) const;
    CFormatOptions &Get_FormatOptions(const UString &name);
  };
}

#endif
