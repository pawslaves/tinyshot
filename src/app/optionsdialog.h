// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QSlider;
class QTabWidget;

namespace ls {

// Captures one key combination and displays it as "Ctrl + Shift + Print Screen".
class HotkeyEdit : public QLineEdit {
  Q_OBJECT
public:
  explicit HotkeyEdit(QWidget* parent = nullptr);

  int mod() const { return m_mod; }
  int vk() const { return m_vk; }
  bool hasHotkey() const { return m_vk != 0; }
  void setHotkey(int mod, int vk);

protected:
  void keyPressEvent(QKeyEvent* event) override;

private:
  void refreshText();

  int m_mod = 0;
  int m_vk = 0;
};

class OptionsDialog : public QDialog {
  Q_OBJECT
public:
  // startPage: 0 General, 1 Hotkeys, 2 Formats, 3 Proxy.
  explicit OptionsDialog(int startPage = 0, QWidget* parent = nullptr);

  bool languageChanged() const;

public slots:
  void accept() override;

private:
  QWidget* createGeneralPage();
  QWidget* createHotkeysPage();
  QWidget* createFormatsPage();
  QWidget* createProxyPage();

  void loadSettings();
  void saveSettings();
  bool validateHotkeys();
  void updateAutoCloseState(bool autoCopyChecked);
  void updateProxyControls();
  void openSystemProxySettings();
  void onUploadEnabledToggled(bool enabled);

  QTabWidget* m_tabs = nullptr;

  // General
  QCheckBox* m_autoCopy = nullptr;
  QCheckBox* m_autoClose = nullptr;
  QCheckBox* m_showBubbles = nullptr;
  QCheckBox* m_keepSelection = nullptr;
  QCheckBox* m_captureCursor = nullptr;
  QCheckBox* m_uploadEnabled = nullptr;
  QComboBox* m_language = nullptr;

  // Hotkeys
  QCheckBox* m_hotkeyMainEnabled = nullptr;
  QCheckBox* m_hotkeySaveFullEnabled = nullptr;
  QCheckBox* m_hotkeyUploadFullEnabled = nullptr;
  HotkeyEdit* m_hotkeyMain = nullptr;
  HotkeyEdit* m_hotkeySaveFull = nullptr;
  HotkeyEdit* m_hotkeyUploadFull = nullptr;

  // Formats
  QComboBox* m_uploadFormat = nullptr;
  QSlider* m_jpegQuality = nullptr;
  QLabel* m_jpegQualityValue = nullptr;

  // Proxy
  QRadioButton* m_proxyNone = nullptr;
  QRadioButton* m_proxySystem = nullptr;
  QRadioButton* m_proxyManual = nullptr;
  QLabel* m_proxyHostLabel = nullptr;
  QLineEdit* m_proxyHost = nullptr;
  QLabel* m_proxyPortLabel = nullptr;
  QLineEdit* m_proxyPort = nullptr;
  QLineEdit* m_systemProxyValue = nullptr;

  QString m_initialLanguage;
};

} // namespace ls
