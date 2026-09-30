// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/optionsdialog.h"

#include "app/hotkeyportal.h"
#include "app/settings.h"
#include "app/translation.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ls {

namespace {

// Qt key codes are mapped to virtual-key codes here: those are what the settings store and what
// HotkeyPortal's X11 fallback translates back.
int vkFromQtKey(int key)
{
  if (key >= Qt::Key_A && key <= Qt::Key_Z)
    return 0x41 + (key - Qt::Key_A);
  if (key >= Qt::Key_0 && key <= Qt::Key_9)
    return 0x30 + (key - Qt::Key_0);
  if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
    return 0x70 + (key - Qt::Key_F1);
  switch (key) {
  case Qt::Key_Backspace: return 0x08;
  case Qt::Key_Tab: return 0x09;
  case Qt::Key_Return: return 0x0D;
  case Qt::Key_Enter: return 0x0D;
  case Qt::Key_Pause: return 0x13;
  case Qt::Key_CapsLock: return 0x14;
  case Qt::Key_Escape: return 0x1B;
  case Qt::Key_Space: return 0x20;
  case Qt::Key_PageUp: return 0x21;
  case Qt::Key_PageDown: return 0x22;
  case Qt::Key_End: return 0x23;
  case Qt::Key_Home: return 0x24;
  case Qt::Key_Left: return 0x25;
  case Qt::Key_Up: return 0x26;
  case Qt::Key_Right: return 0x27;
  case Qt::Key_Down: return 0x28;
  case Qt::Key_Print: return 0x2C;
  case Qt::Key_Insert: return 0x2D;
  case Qt::Key_Delete: return 0x2E;
  case Qt::Key_NumLock: return 0x90;
  case Qt::Key_ScrollLock: return 0x91;
  case Qt::Key_Semicolon: return 0xBA;
  case Qt::Key_Equal: return 0xBB;
  case Qt::Key_Comma: return 0xBC;
  case Qt::Key_Minus: return 0xBD;
  case Qt::Key_Period: return 0xBE;
  case Qt::Key_Slash: return 0xBF;
  case Qt::Key_QuoteLeft: return 0xC0;
  case Qt::Key_BracketLeft: return 0xDB;
  case Qt::Key_Backslash: return 0xDC;
  case Qt::Key_BracketRight: return 0xDD;
  case Qt::Key_Apostrophe: return 0xDE;
  default: return 0;
  }
}

// The proxy the environment asks the HTTP clients to use (https_proxy/HTTPS_PROXY/...).  Empty
// means "direct connection".
QString systemProxyText()
{
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  static const char* kNames[] = {"https_proxy", "HTTPS_PROXY", "http_proxy",
                                 "HTTP_PROXY",  "all_proxy",   "ALL_PROXY"};
  for (const char* name : kNames) {
    const QString value = env.value(QLatin1String(name));
    if (!value.isEmpty())
      return value;
  }
  return QString();
}

} // namespace

HotkeyEdit::HotkeyEdit(QWidget* parent)
  : QLineEdit(parent)
{
  setReadOnly(true);
  setPlaceholderText(tr("General hotkey"));
}

void HotkeyEdit::setHotkey(int mod, int vk)
{
  m_mod = mod;
  m_vk = vk;
  refreshText();
}

void HotkeyEdit::refreshText()
{
  setText(m_vk == 0 && m_mod == 0 ? QString() : HotkeyPortal::formatHotkeyText(m_mod, m_vk));
}

void HotkeyEdit::keyPressEvent(QKeyEvent* event)
{
  const int key = event->key();
  // Pure modifier presses keep the current combination.
  if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
      key == Qt::Key_AltGr || key == Qt::Key_Meta || key == Qt::Key_unknown) {
    event->ignore();
    return;
  }
  if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
    setHotkey(0, 0);
    return;
  }
  const int vk = vkFromQtKey(key);
  if (vk == 0) {
    event->ignore();
    return;
  }
  int mod = 0;
  const Qt::KeyboardModifiers modifiers = event->modifiers();
  if (modifiers.testFlag(Qt::ControlModifier))
    mod |= 2;
  if (modifiers.testFlag(Qt::ShiftModifier))
    mod |= 4;
  if (modifiers.testFlag(Qt::AltModifier))
    mod |= 1;
  if (modifiers.testFlag(Qt::MetaModifier))
    mod |= 8;
  setHotkey(mod, vk);
}

OptionsDialog::OptionsDialog(int startPage, QWidget* parent)
  : QDialog(parent)
{
  setWindowTitle(tr("Options"));

  auto* layout = new QVBoxLayout(this);
  m_tabs = new QTabWidget(this);
  m_tabs->addTab(createGeneralPage(), tr("General"));
  m_tabs->addTab(createHotkeysPage(), tr("Hotkeys"));
  m_tabs->addTab(createFormatsPage(), tr("Formats"));
  m_tabs->addTab(createProxyPage(), tr("Proxy"));
  layout->addWidget(m_tabs);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  loadSettings();
  if (startPage >= 0 && startPage < m_tabs->count())
    m_tabs->setCurrentIndex(startPage);
}

QWidget* OptionsDialog::createGeneralPage()
{
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);

  m_autoCopy = new QCheckBox(tr("Automatically copy a link after uploading"), page);
  m_autoClose = new QCheckBox(tr("Automatically close the upload window"), page);
  m_showBubbles = new QCheckBox(tr("Show notifications about copying and saving"), page);
  m_keepSelection = new QCheckBox(tr("Keep the selected area position"), page);
  m_captureCursor = new QCheckBox(tr("Capture a cursor on a screenshot"), page);
  m_uploadEnabled =
      new QCheckBox(tr("Enable uploads to prnt.sc (uploaded images are public)"), page);
  layout->addWidget(m_autoCopy);
  layout->addWidget(m_autoClose);
  layout->addWidget(m_showBubbles);
  layout->addWidget(m_keepSelection);
  layout->addWidget(m_captureCursor);
  layout->addWidget(m_uploadEnabled);

  auto* languageRow = new QHBoxLayout;
  languageRow->addWidget(new QLabel(tr("Language"), page));
  m_language = new QComboBox(page);
  m_language->addItem(tr("System default"), QString());
  const QStringList languages = availableLanguages();
  for (const QString& code : languages) {
    m_language->addItem(languageDisplayName(code), code);
    m_language->setItemData(m_language->count() - 1, code, Qt::ToolTipRole);
  }
  languageRow->addWidget(m_language, 1);
  layout->addLayout(languageRow);
  layout->addStretch(1);

  // AutoClose is disabled (and unchecked) while AutoCopy is off.
  connect(m_autoCopy, &QCheckBox::toggled, this, &OptionsDialog::updateAutoCloseState);
  connect(m_uploadEnabled, &QCheckBox::toggled, this, &OptionsDialog::onUploadEnabledToggled);
  return page;
}

QWidget* OptionsDialog::createHotkeysPage()
{
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);

  const struct {
    const char* text;
    QCheckBox** box;
    HotkeyEdit** edit;
  } rows[] = {
    {QT_TR_NOOP("General hotkey"), &m_hotkeyMainEnabled, &m_hotkeyMain},
    {QT_TR_NOOP("Instant save of the fullscreen"), &m_hotkeySaveFullEnabled, &m_hotkeySaveFull},
    {QT_TR_NOOP("Instant upload of the fullscreen"), &m_hotkeyUploadFullEnabled, &m_hotkeyUploadFull},
  };
  for (const auto& row : rows) {
    auto* line = new QHBoxLayout;
    *row.box = new QCheckBox(tr(row.text), page);
    *row.edit = new HotkeyEdit(page);
    (*row.edit)->setMinimumWidth(160);
    line->addWidget(*row.box, 1);
    line->addWidget(*row.edit);
    layout->addLayout(line);
  }
  layout->addStretch(1);
  return page;
}

QWidget* OptionsDialog::createFormatsPage()
{
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);

  auto* formatRow = new QHBoxLayout;
  formatRow->addWidget(new QLabel(tr("Upload images format"), page));
  m_uploadFormat = new QComboBox(page);
  // Item data uses the stored values: 1 = PNG (default), 2 = JPEG.
  m_uploadFormat->addItem(QStringLiteral("PNG"), 1);
  m_uploadFormat->addItem(QStringLiteral("JPEG"), 2);
  formatRow->addWidget(m_uploadFormat, 1);
  layout->addLayout(formatRow);

  auto* qualityRow = new QHBoxLayout;
  qualityRow->addWidget(new QLabel(tr("JPEG Quality"), page));
  m_jpegQuality = new QSlider(Qt::Horizontal, page);
  m_jpegQuality->setRange(50, 100);
  m_jpegQualityValue = new QLabel(page);
  qualityRow->addWidget(m_jpegQuality, 1);
  qualityRow->addWidget(m_jpegQualityValue);
  layout->addLayout(qualityRow);
  layout->addStretch(1);

  connect(m_jpegQuality, &QSlider::valueChanged, this,
          [this](int value) { m_jpegQualityValue->setText(QString::number(value)); });
  return page;
}

QWidget* OptionsDialog::createProxyPage()
{
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);

  m_proxyNone = new QRadioButton(tr("No proxy"), page);
  m_proxySystem = new QRadioButton(tr("Use system proxy settings"), page);
  m_proxyManual = new QRadioButton(tr("Manual proxy configuration"), page);
  layout->addWidget(m_proxyNone);
  layout->addWidget(m_proxySystem);

  auto* systemRow = new QHBoxLayout;
  systemRow->addWidget(new QLabel(tr("System proxy"), page));
  m_systemProxyValue = new QLineEdit(systemProxyText(), page);
  m_systemProxyValue->setReadOnly(true);
  systemRow->addWidget(m_systemProxyValue, 1);
  auto* configure = new QPushButton(tr("Configure"), page);
  connect(configure, &QPushButton::clicked, this, &OptionsDialog::openSystemProxySettings);
  systemRow->addWidget(configure);
  layout->addLayout(systemRow);

  layout->addWidget(m_proxyManual);
  auto* hostRow = new QHBoxLayout;
  m_proxyHostLabel = new QLabel(tr("HTTP(S) proxy"), page);
  m_proxyHost = new QLineEdit(page);
  hostRow->addWidget(m_proxyHostLabel);
  hostRow->addWidget(m_proxyHost, 1);
  layout->addLayout(hostRow);

  auto* portRow = new QHBoxLayout;
  m_proxyPortLabel = new QLabel(tr("Port"), page);
  m_proxyPort = new QLineEdit(page);
  m_proxyPort->setMaximumWidth(80);
  portRow->addWidget(m_proxyPortLabel);
  portRow->addWidget(m_proxyPort);
  portRow->addStretch(1);
  layout->addLayout(portRow);
  layout->addStretch(1);

  for (QRadioButton* button : {m_proxyNone, m_proxySystem, m_proxyManual})
    connect(button, &QRadioButton::toggled, this, &OptionsDialog::updateProxyControls);
  return page;
}

void OptionsDialog::updateAutoCloseState(bool autoCopyChecked)
{
  m_autoClose->setEnabled(autoCopyChecked);
  if (!autoCopyChecked)
    m_autoClose->setChecked(false);
}

void OptionsDialog::updateProxyControls()
{
  const bool manual = m_proxyManual->isChecked();
  m_proxyHostLabel->setEnabled(manual);
  m_proxyHost->setEnabled(manual);
  m_proxyPortLabel->setEnabled(manual);
  m_proxyPort->setEnabled(manual);
}

void OptionsDialog::onUploadEnabledToggled(bool enabled)
{
  if (!enabled || Settings::instance().uploadNoticeShown())
    return;
  // The notice is shown the first time uploads are switched on, then never again.
  QMessageBox::information(this, tr("Uploads are public"),
                           tr("Screenshots uploaded to prnt.sc are public: anyone who has the "
                              "link can see them."));
  Settings::instance().setUploadNoticeShown();
}

void OptionsDialog::loadSettings()
{
  const Settings& settings = Settings::instance();

  // General: AutoCopy/AutoClose are read with "== 1", the rest with "!= 0".
  m_autoCopy->setChecked(settings.autoCopy());
  m_autoClose->setChecked(settings.autoClose());
  updateAutoCloseState(settings.autoCopy());
  m_showBubbles->setChecked(settings.showBubbles());
  m_keepSelection->setChecked(settings.keepSelection());
  m_captureCursor->setChecked(settings.captureCursor());
  m_uploadEnabled->setChecked(settings.uploadEnabled());

  // An empty code is "system default"; the tray compares the stored code against the selection to
  // decide whether the translator has to be replaced.
  const QString storedLanguage = settings.value(QStringLiteral("Language")).toString();
  m_initialLanguage = storedLanguage;
  int languageIndex = 0;
  for (int i = 0; i < m_language->count(); ++i) {
    // The stored code may differ in case from the file's own code ("pt_br" vs "pt_BR").
    if (m_language->itemData(i).toString().compare(storedLanguage, Qt::CaseInsensitive) == 0) {
      languageIndex = i;
      break;
    }
  }
  m_language->setCurrentIndex(languageIndex);

  // Hotkeys
  const Settings::Hotkey main = settings.hotkey(Settings::HotkeyMain);
  const Settings::Hotkey saveFull = settings.hotkey(Settings::HotkeySaveFull);
  const Settings::Hotkey uploadFull = settings.hotkey(Settings::HotkeyUploadFull);
  m_hotkeyMainEnabled->setChecked(main.enabled);
  m_hotkeyMain->setHotkey(main.mod, main.vk);
  m_hotkeySaveFullEnabled->setChecked(saveFull.enabled);
  m_hotkeySaveFull->setHotkey(saveFull.mod, saveFull.vk);
  m_hotkeyUploadFullEnabled->setChecked(uploadFull.enabled);
  m_hotkeyUploadFull->setHotkey(uploadFull.mod, uploadFull.vk);

  // Formats
  const int uploadFormatIndex = m_uploadFormat->findData(settings.uploadFormat());
  m_uploadFormat->setCurrentIndex(uploadFormatIndex < 0 ? 0 : uploadFormatIndex);
  const int quality = settings.jpegQuality();
  m_jpegQuality->setValue(quality);
  m_jpegQualityValue->setText(QString::number(quality));

  // Proxy: 1 = no proxy, 0 = system (the default), 3 = manual.
  switch (settings.proxyType()) {
  case 1:
    m_proxyNone->setChecked(true);
    break;
  case 3:
    m_proxyManual->setChecked(true);
    break;
  default:
    m_proxySystem->setChecked(true);
    break;
  }
  const QString proxy = settings.proxyString();
  const int separator = proxy.lastIndexOf(QLatin1Char(':'));
  if (separator >= 0) {
    m_proxyHost->setText(proxy.left(separator));
    m_proxyPort->setText(proxy.mid(separator + 1));
  }
  updateProxyControls();
}

bool OptionsDialog::validateHotkeys()
{
  const struct {
    QCheckBox* box;
    HotkeyEdit* edit;
  } entries[] = {
    {m_hotkeyMainEnabled, m_hotkeyMain},
    {m_hotkeySaveFullEnabled, m_hotkeySaveFull},
    {m_hotkeyUploadFullEnabled, m_hotkeyUploadFull},
  };

  for (const auto& entry : entries) {
    if (entry.box->isChecked() && !entry.edit->hasHotkey()) {
      // A checked hotkey without a key combination is invalid.
      QMessageBox box(this);
      box.setIcon(QMessageBox::Warning);
      box.setWindowTitle(tr("Error"));
      box.setText(tr("One of your hotkeys is invalid. If you want to disable that hotkey, please "
                     "uncheck corresponding checkbox."));
      box.exec();
      return false;
    }
  }

  for (int i = 0; i < 3; ++i) {
    if (!entries[i].box->isChecked())
      continue;
    for (int j = i + 1; j < 3; ++j) {
      if (!entries[j].box->isChecked())
        continue;
      if (entries[i].edit->mod() == entries[j].edit->mod() &&
          entries[i].edit->vk() == entries[j].edit->vk()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Error"));
        box.setText(tr("You can not set the same hotkeys for different functions. Please change "
                       "one of the hotkeys."));
        box.exec();
        return false;
      }
    }
  }
  return true;
}

void OptionsDialog::saveSettings()
{
  Settings& settings = Settings::instance();

  // General
  settings.setValue(QStringLiteral("AutoCopy"), m_autoCopy->isChecked() ? 1 : 0);
  settings.setValue(QStringLiteral("AutoClose"), m_autoClose->isChecked() ? 1 : 0);
  settings.setValue(QStringLiteral("ShowBubbles"), m_showBubbles->isChecked() ? 1 : 0);
  settings.setValue(QStringLiteral("KeepSelection"), m_keepSelection->isChecked() ? 1 : 0);
  settings.setValue(QStringLiteral("CaptureCursor"), m_captureCursor->isChecked() ? 1 : 0);
  settings.setValue(QStringLiteral("UploadEnabled"), m_uploadEnabled->isChecked() ? 1 : 0);
  settings.setValue(QStringLiteral("Language"), m_language->currentData().toString());

  // Hotkeys: the enabled flag is always written; mod/vk are taken from the edit only when the row
  // is enabled and holds a combination.
  const struct {
    Settings::HotkeyId id;
    QCheckBox* box;
    HotkeyEdit* edit;
  } hotkeys[] = {
    {Settings::HotkeyMain, m_hotkeyMainEnabled, m_hotkeyMain},
    {Settings::HotkeySaveFull, m_hotkeySaveFullEnabled, m_hotkeySaveFull},
    {Settings::HotkeyUploadFull, m_hotkeyUploadFullEnabled, m_hotkeyUploadFull},
  };
  for (const auto& row : hotkeys) {
    Settings::Hotkey hotkey = settings.hotkey(row.id);
    hotkey.enabled = row.box->isChecked();
    if (hotkey.enabled && row.edit->hasHotkey()) {
      hotkey.mod = row.edit->mod();
      hotkey.vk = row.edit->vk();
    }
    settings.setHotkey(row.id, hotkey);
  }

  // Formats
  const int uploadFormat = m_uploadFormat->currentData().toInt();
  if (uploadFormat > 0)
    settings.setValue(QStringLiteral("UploadFormat"), uploadFormat);
  settings.setValue(QStringLiteral("JpegQuality"), m_jpegQuality->value());

  // Radio order maps to the stored access types: 1 direct, 0 system, 3 manual.
  int proxyType = 0;
  if (m_proxyNone->isChecked())
    proxyType = 1;
  else if (m_proxyManual->isChecked())
    proxyType = 3;
  settings.setValue(QStringLiteral("ProxyType"), proxyType);
  const QString host = m_proxyHost->text().trimmed();
  const QString port = m_proxyPort->text().trimmed();
  QString proxyString;
  if (!host.isEmpty() || !port.isEmpty())
    proxyString = host + QLatin1Char(':') + port; // stored as "host:port"; the host may be empty
  settings.setValue(QStringLiteral("ProxyString"), proxyString);

  settings.sync();
}

bool OptionsDialog::languageChanged() const
{
  if (!m_language)
    return false;
  const QString selected = m_language->currentData().toString();
  return selected.compare(m_initialLanguage, Qt::CaseInsensitive) != 0;
}

void OptionsDialog::openSystemProxySettings()
{
  // There is no single system proxy dialog across desktops; try the common ones and give up
  // silently when none of them is installed.
  const struct {
    const char* program;
    const char* argument;
  } candidates[] = {
    {"gnome-control-center", "network"},
    {"systemsettings5", "kcm_networkmanagement"},
    {"systemsettings", "kcm_networkmanagement"},
    {"kcmshell6", "kcm_networkmanagement"},
    {"kcmshell5", "kcm_networkmanagement"},
    {"cinnamon-settings", "network"},
    {"mate-network-properties", nullptr},
    {"xfce4-settings-manager", nullptr},
  };
  for (const auto& candidate : candidates) {
    const QString program = QStandardPaths::findExecutable(QLatin1String(candidate.program));
    if (program.isEmpty())
      continue;
    QStringList arguments;
    if (candidate.argument)
      arguments << QLatin1String(candidate.argument);
    if (QProcess::startDetached(program, arguments))
      return;
  }
}

void OptionsDialog::accept()
{
  // A failed validation keeps the sheet open.
  if (!validateHotkeys())
    return;
  saveSettings();
  QDialog::accept();
}

} // namespace ls
