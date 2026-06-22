// QtHashMethodsDialog.cpp

#include "QtHashMethodsDialog.h"

#include "QtLang.h"

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include "../../../Common/MyString.h"


// The offered set, mirroring Explorer/ContextMenu.cpp g_HashCommands order.
// {user label, engine method name}.
struct CHashEntry { const char *Label; const char *Method; };

static const CHashEntry k_HashEntries[] =
{
  { "CRC-32",   "CRC32"    },
  { "CRC-64",   "CRC64"    },
  { "XXH64",    "XXH64"    },
  { "MD5",      "MD5"      },
  { "SHA-1",    "SHA1"     },
  { "SHA-256",  "SHA256"   },
  { "SHA-384",  "SHA384"   },
  { "SHA-512",  "SHA512"   },
  { "SHA3-256", "SHA3-256" },
  { "BLAKE2sp", "BLAKE2sp" },
  { "All (*)",  "*"        }
};


QtHashMethodsDialog::QtHashMethodsDialog(QWidget *parent)
  : QDialog(parent)
{
  // Port-specific dialog (Windows hashes via the Explorer context menu's
  // g_HashCommands, with no algorithm-selection dialog) — the caption and label
  // have no original IDS_, so they stay plain literals (no langID to invent).
  setWindowTitle(QStringLiteral("Select checksum algorithms"));
  setModal(true);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(QStringLiteral("Checksum algorithm(s):"), this));

  QGroupBox *box = new QGroupBox(this);
  QVBoxLayout *boxLayout = new QVBoxLayout(box);
  for (const CHashEntry &e : k_HashEntries)
  {
    QCheckBox *cb = new QCheckBox(QString::fromLatin1(e.Label), box);
    cb->setProperty("method", QString::fromLatin1(e.Method));
    boxLayout->addWidget(cb);
    _checks.push_back(cb);
  }
  layout->addWidget(box);

  // Default: CRC32 (engine k_DefaultHashMethod).
  SetChecked("CRC32", true);

  QDialogButtonBox *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // OK/Cancel use langIDs 401/402 (kLangPairs, LangUtils.cpp); English from the
  // dialog-template literals.
  if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
    ok->setText(FmLang(401, QStringLiteral("OK")));
  if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
    cancel->setText(FmLang(402, QStringLiteral("Cancel")));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}


bool QtHashMethodsDialog::SetChecked(const char *methodName, bool checked)
{
  const QString want = QString::fromLatin1(methodName);
  for (QCheckBox *cb : _checks)
  {
    if (cb->property("method").toString().compare(want, Qt::CaseInsensitive) == 0)
    {
      cb->setChecked(checked);
      return true;
    }
  }
  return false;
}


UStringVector QtHashMethodsDialog::SelectedMethods() const
{
  UStringVector methods;
  for (QCheckBox *cb : _checks)
  {
    if (!cb->isChecked())
      continue;
    const QByteArray m = cb->property("method").toString().toLatin1();
    UString u;
    for (char c : m)
      u += (wchar_t)(unsigned char)c;
    methods.Add(u);
  }
  return methods;
}
