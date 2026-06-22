// QtSplitDialog.cpp
// ----------------------------------------------------------------------------
// See QtSplitDialog.h. ParseVolumeSizes + k_Sizes are re-lifted VERBATIM from
// FileManager/SplitUtils.cpp (the canonical source) — exactly as QtCompressDialog
// did — because SplitUtils.cpp is Win32-tied (its header pulls in ComboBox.h) and
// the QtCompressDialog copies live in an anonymous namespace (not cross-TU
// linkable). Invent nothing: this is the same canonical block.
// ----------------------------------------------------------------------------

#include "QtSplitDialog.h"
#include "../QtLang.h"                              // P.2 : FmLang
#include "../../FileManager/SplitDialogRes.h"       // P.2 : IDD_SPLIT=7300, IDT_SPLIT_PATH/_VOLUME
#include "../../FileManager/CopyDialogRes.h"        // G.1 : IDS_SET_FOLDER=6007 (browse title)
#include "../../FileManager/resourceGui.h"          // G.1 : IDS_INCORRECT_VOLUME_SIZE=7307

#include "../../../../Common/StringToInt.h"   // ConvertStringToUInt64

#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>


namespace {

UString QStr_to_UString(const QString &s)
{
  UString u;
  const int n = s.size();
  for (int i = 0; i < n; i++)
    u += (wchar_t)s.at(i).unicode();
  return u;
}

QString UString_to_QStr(const UString &u)
{
  QString s;
  for (unsigned i = 0; i < u.Len(); i++)
    s.append(QChar((char16_t)(unsigned)u[i]));
  return s;
}

// === ParseVolumeSizes : VERBATIM from FileManager/SplitUtils.cpp:9-58 ========
bool ParseVolumeSizes(const UString &s, CRecordVector<UInt64> &values)
{
  values.Clear();
  bool prevIsNumber = false;
  for (unsigned i = 0; i < s.Len();)
  {
    wchar_t c = s[i++];
    if (c == L' ')
      continue;
    if (c == L'-')
      return true;
    if (prevIsNumber)
    {
      prevIsNumber = false;
      unsigned numBits = 0;
      switch (MyCharLower_Ascii(c))
      {
        case 'b': continue;
        case 'k': numBits = 10; break;
        case 'm': numBits = 20; break;
        case 'g': numBits = 30; break;
        case 't': numBits = 40; break;
      }
      if (numBits != 0)
      {
        UInt64 &val = values.Back();
        if (val >= ((UInt64)1 << (64 - numBits)))
          return false;
        val <<= numBits;

        for (; i < s.Len(); i++)
          if (s[i] == L' ')
            break;
        continue;
      }
    }
    i--;
    const wchar_t *start = s.Ptr(i);
    const wchar_t *end;
    UInt64 val = ConvertStringToUInt64(start, &end);
    if (start == end)
      return false;
    if (val == 0)
      return false;
    values.Add(val);
    prevIsNumber = true;
    i += (unsigned)(end - start);
  }
  return true;
}

// k_Sizes : VERBATIM from FileManager/SplitUtils.cpp:61-73 ====================
const char * const k_Sizes[] =
{
    "10M"
  , "100M"
  , "1000M"
  , "650M - CD"
  , "700M - CD"
  , "4092M - FAT"
  , "4480M - DVD"     //  4489 MiB limit
  , "8128M - DVD DL"  //  8147 MiB limit
  , "23040M - BD"     // 23866 MiB limit
  // , "1457664 - 3.5\" floppy"
};

} // namespace


// Exported thin wrapper over the re-lifted ParseVolumeSizes (for the headless
// split path; see QtSplitDialog.h).
bool QtSplit_ParseVolumeSizes(const UString &s, CRecordVector<UInt64> &values)
{
  return ParseVolumeSizes(s, values);
}


QtSplitDialog::QtSplitDialog(QWidget *parent)
  : QDialog(parent)
  , _path(nullptr)
  , _volume(nullptr)
  , _fileLabel(nullptr)
{
  setWindowTitle(FmLang(IDD_SPLIT, QStringLiteral("Split File")));   // P.2 : 7300 caption

  QVBoxLayout *top = new QVBoxLayout(this);

  _fileLabel = new QLabel(this);
  top->addWidget(_fileLabel);

  QFormLayout *form = new QFormLayout();

  // Output dir + browse "..." (IDE_SPLIT_PATH + MyBrowseForFolder).
  QHBoxLayout *pathRow = new QHBoxLayout();
  _path = new QLineEdit(this);
  pathRow->addWidget(_path);
  QToolButton *browse = new QToolButton(this);
  browse->setText(QStringLiteral("..."));
  connect(browse, &QToolButton::clicked, this, &QtSplitDialog::onBrowse);
  pathRow->addWidget(browse);
  // G.1 : IDT_SPLIT_PATH=7301 ("&Split to:" label, SplitDialog.cpp kLangIDs)
  form->addRow(FmLang(IDT_SPLIT_PATH, QStringLiteral("Split to:")), pathRow);

  // Editable volume-size combo, seeded from k_Sizes (AddVolumeItems mirror).
  _volume = new QComboBox(this);
  _volume->setEditable(true);
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_Sizes); i++)
    _volume->addItem(QString::fromLatin1(k_Sizes[i]));
  _volume->setCurrentIndex(0);
  // G.1 : IDT_SPLIT_VOLUME=7302 ("Split to &volumes,  bytes:" label, SplitDialog.cpp kLangIDs)
  form->addRow(FmLang(IDT_SPLIT_VOLUME, QStringLiteral("Split to volumes, bytes:")), _volume);

  top->addLayout(form);

  QDialogButtonBox *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // G.1 : OK/Cancel use langIDs 401/402 (kLangPairs, LangUtils.cpp); override the
  // standard-button text so a loaded txt translates them (mirror QtOptionsDialog.cpp).
  if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
    ok->setText(FmLang(401, QStringLiteral("OK")));
  if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
    cancel->setText(FmLang(402, QStringLiteral("Cancel")));
  connect(buttons, &QDialogButtonBox::accepted, this, &QtSplitDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  top->addWidget(buttons);
}

void QtSplitDialog::fillFromState()
{
  // Port-specific label prefix: upstream appends FilePath to the caption with a
  // space (SplitDialog.cpp OnInit) and has no separate "Split file:" string, so
  // there is no original langID for this phrasing -> plain literal.
  _fileLabel->setText(QStringLiteral("Split file: ") + UString_to_QStr(FilePath));
  _path->setText(UString_to_QStr(Path));
}

void QtSplitDialog::onBrowse()
{
  // G.1 : browse-folder title = IDS_SET_FOLDER=6007 (SplitDialog.cpp OnButtonSetPath).
  const QString dir = QFileDialog::getExistingDirectory(this,
      FmLang(IDS_SET_FOLDER, QStringLiteral("Specify a location for output folder")),
      _path->text());
  if (!dir.isEmpty())
    _path->setText(dir);
}

void QtSplitDialog::onAccept()
{
  // OnOK mirror (SplitDialog.cpp:102-114): read+trim the volume text, parse, and
  // reject (stay open) on empty/invalid.
  Path = QStr_to_UString(_path->text());

  UString volumeString = QStr_to_UString(_volume->currentText());
  volumeString.Trim();
  if (!ParseVolumeSizes(volumeString, VolumeSizes) || VolumeSizes.IsEmpty())
  {
    // G.1 : SplitDialog.cpp OnOK -> MessageBoxW(L"7-Zip" literal title,
    //        LangString(IDS_INCORRECT_VOLUME_SIZE=7307)).
    QMessageBox::warning(this, QStringLiteral("7-Zip"),
        FmLang(IDS_INCORRECT_VOLUME_SIZE, QStringLiteral("Incorrect volume size")));
    return;
  }
  accept();
}
