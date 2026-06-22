// QtSplitCombine.h
// ----------------------------------------------------------------------------
// B.7c : the Split / Combine byte machinery, lifted from the original
// FileManager/PanelSplitFile.cpp. Qt-side only, no engine edits.
//
//   * CVolSeqName       (PanelSplitFile.cpp:29-78)  — volume naming <base>.NNN
//   * GetNumberOfVolumes(SplitUtils.cpp:81-96)       — the >=100 confirm count
//   * QtSplitWorker      (CThreadSplit analogue)     — raw-byte split, 1 MiB loop
//   * QtCombineWorker    (CThreadCombine analogue)   — plain concatenation
//
// Both workers subclass QtProgressThreadVirt (the Qt port of CProgressThreadVirt);
// the ONLY mechanical edit in the lifted ProcessVirt() bodies is `Sync` -> `Sync()`
// (the original references a member `Sync`; the Qt base exposes it as an accessor).
// ----------------------------------------------------------------------------

#ifndef ZIP7_INC_QT_FM_SPLIT_COMBINE_H
#define ZIP7_INC_QT_FM_SPLIT_COMBINE_H

#include "../../../../Common/MyString.h"
#include "../../../../Common/MyVector.h"

#include "../QtProgressThreadVirt.h"


// === CVolSeqName : VERBATIM from PanelSplitFile.cpp:29-78 ====================
struct CVolSeqName
{
  UString UnchangedPart;
  UString ChangedPart;
  CVolSeqName(): ChangedPart("000") {}

  void SetNumDigits(UInt64 numVolumes)
  {
    ChangedPart = "000";
    while (numVolumes > 999)
    {
      numVolumes /= 10;
      ChangedPart.Add_Char('0');
    }
  }

  bool ParseName(const UString &name)
  {
    if (name.Len() < 2)
      return false;
    if (name.Back() != L'1' || name[name.Len() - 2] != L'0')
      return false;

    unsigned pos = name.Len() - 2;
    for (; pos > 0 && name[pos - 1] == '0'; pos--);
    UnchangedPart.SetFrom(name, pos);
    ChangedPart = name.Ptr(pos);
    return true;
  }

  UString GetNextName();
};


// === GetNumberOfVolumes : VERBATIM from SplitUtils.cpp:81-96 =================
UInt64 GetNumberOfVolumes(UInt64 size, const CRecordVector<UInt64> &volSizes);


// === Split worker (CThreadSplit analogue, PanelSplitFile.cpp:80-232) ========
class QtSplitWorker Z7_final : public QtProgressThreadVirt
{
  HRESULT ProcessVirt() Z7_override;
public:
  FString FilePath;
  FString VolBasePath;
  UInt64 NumVolumes;
  CRecordVector<UInt64> VolumeSizes;
};


// === Combine worker (CThreadCombine analogue, PanelSplitFile.cpp:345-409) ====
class QtCombineWorker Z7_final : public QtProgressThreadVirt
{
  HRESULT ProcessVirt() Z7_override;
public:
  FString InputDirPrefix;
  FStringVector Names;
  FString OutputPath;
  UInt64 TotalSize;
};

#endif
