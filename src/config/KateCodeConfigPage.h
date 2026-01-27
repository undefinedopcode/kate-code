#pragma once

#include <KTextEditor/ConfigPage>

class SettingsStore;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QPushButton;
class QLabel;
class QTabWidget;

class KateCodeConfigPage : public KTextEditor::ConfigPage
{
    Q_OBJECT

public:
    explicit KateCodeConfigPage(SettingsStore *settings, QWidget *parent = nullptr);
    ~KateCodeConfigPage() override;

    // KTextEditor::ConfigPage interface
    QString name() const override;
    QString fullName() const override;
    QIcon icon() const override;

public Q_SLOTS:
    void apply() override;
    void defaults() override;
    void reset() override;

private Q_SLOTS:
    void onApiKeyLoaded(bool success);
    void onApiKeySaved(bool success);
    void onShowApiKeyToggled();
    void onWalletError(const QString &message);
    void onSettingChanged();

private:
    void setupUi();
    void setupGeneralTab(QWidget *tab);
    void setupSummariesTab(QWidget *tab);
    void updateApiKeyStatus();

    SettingsStore *m_settings;

    // Tab widget
    QTabWidget *m_tabWidget;

    // General tab - ACP Backend section
    QComboBox *m_acpBackendCombo;
    QLineEdit *m_customExecutableEdit;
    QLabel *m_customExecutableLabel;

    // General tab - Diff colors section
    QComboBox *m_diffColorSchemeCombo;

    // Summaries tab - API Key section
    QLineEdit *m_apiKeyEdit;
    QPushButton *m_showApiKeyButton;
    QLabel *m_apiKeyStatus;

    // Summaries tab - Summary options
    QCheckBox *m_enableSummariesCheck;
    QComboBox *m_summaryModelCombo;

    // Summaries tab - Session resume
    QCheckBox *m_autoResumeCheck;

    bool m_hasChanges;
    bool m_apiKeyVisible;
};
