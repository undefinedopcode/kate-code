#pragma once

#include <KTextEditor/ConfigPage>

class SettingsStore;
struct ACPProvider;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QListWidget;
class QListWidgetItem;
class QTabWidget;
class QTableWidget;

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
    void onSettingChanged();

    void onAddProvider();
    void onEditProvider();
    void onRemoveProvider();
    void onMoveProviderUp();
    void onMoveProviderDown();

private:
    void setupUi();
    void setupGeneralTab(QWidget *tab);
    void setupAdvancedTab(QWidget *tab);
    void populateProviderList();
    void populateSummaryProviderCombo(const QString &selectedId);
    bool editProviderDialog(ACPProvider &provider, const QString &title);
    ACPProvider providerForItem(const QListWidgetItem *item) const;
    void updateProviderButtons();
    void saveProviderOrder();

    SettingsStore *m_settings;

    // Tab widget
    QTabWidget *m_tabWidget;

    // General tab - ACP Providers section
    QListWidget *m_providerList;
    QPushButton *m_addProviderButton;
    QPushButton *m_editProviderButton;
    QPushButton *m_removeProviderButton;
    QPushButton *m_moveProviderUpButton;
    QPushButton *m_moveProviderDownButton;

    // General tab - Diff colors section
    QComboBox *m_diffColorSchemeCombo;

    // Advanced tab - Summary options
    QCheckBox *m_enableSummariesCheck;
    QComboBox *m_summaryProviderCombo;

    // Advanced tab - Session resume
    QCheckBox *m_autoResumeCheck;
    QCheckBox *m_summariseOnResumeCheck;

    // Advanced tab - ACP file logging
    QCheckBox *m_acpLogEnableCheck;
    QLineEdit *m_acpLogDirEdit;
    QLineEdit *m_acpLogSubdirEdit;

    // General tab - Debug section
    QCheckBox *m_debugLoggingCheck;

    // Advanced tab - Command auto-approval section
    QPlainTextEdit *m_allowedCommandsEdit;

    bool m_hasChanges;
};
