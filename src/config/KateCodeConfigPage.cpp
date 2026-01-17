#include "KateCodeConfigPage.h"
#include "SettingsStore.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

KateCodeConfigPage::KateCodeConfigPage(SettingsStore *settings, QWidget *parent)
    : KTextEditor::ConfigPage(parent)
    , m_settings(settings)
    , m_hasChanges(false)
    , m_apiKeyVisible(false)
{
    setupUi();

    // Connect settings signals
    connect(m_settings, &SettingsStore::apiKeyLoaded,
            this, &KateCodeConfigPage::onApiKeyLoaded);
    connect(m_settings, &SettingsStore::apiKeySaved,
            this, &KateCodeConfigPage::onApiKeySaved);
    connect(m_settings, &SettingsStore::walletError,
            this, &KateCodeConfigPage::onWalletError);

    // Load current API key
    m_settings->loadApiKey();

    // Load current settings
    reset();
}

KateCodeConfigPage::~KateCodeConfigPage() = default;

QString KateCodeConfigPage::name() const
{
    return i18n("Kate Code");
}

QString KateCodeConfigPage::fullName() const
{
    return i18n("Kate Code Plugin Settings");
}

QIcon KateCodeConfigPage::icon() const
{
    return QIcon::fromTheme(QStringLiteral("claude"));
}

void KateCodeConfigPage::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // API Key Group
    auto *apiGroup = new QGroupBox(i18n("Anthropic API Key"), this);
    auto *apiLayout = new QVBoxLayout(apiGroup);

    auto *apiKeyLayout = new QHBoxLayout();
    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(i18n("Enter your Anthropic API key"));
    connect(m_apiKeyEdit, &QLineEdit::textChanged,
            this, &KateCodeConfigPage::onSettingChanged);

    m_showApiKeyButton = new QPushButton(i18n("Show"), this);
    m_showApiKeyButton->setCheckable(true);
    connect(m_showApiKeyButton, &QPushButton::clicked,
            this, &KateCodeConfigPage::onShowApiKeyToggled);

    apiKeyLayout->addWidget(m_apiKeyEdit);
    apiKeyLayout->addWidget(m_showApiKeyButton);
    apiLayout->addLayout(apiKeyLayout);

    m_apiKeyStatus = new QLabel(this);
    m_apiKeyStatus->setWordWrap(true);
    apiLayout->addWidget(m_apiKeyStatus);

    auto *apiNote = new QLabel(i18n("The API key is stored securely in KWallet and used for generating session summaries."), this);
    apiNote->setWordWrap(true);
    apiNote->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    apiLayout->addWidget(apiNote);

    mainLayout->addWidget(apiGroup);

    // Session Summaries Group
    auto *summaryGroup = new QGroupBox(i18n("Session Summaries"), this);
    auto *summaryLayout = new QFormLayout(summaryGroup);

    m_enableSummariesCheck = new QCheckBox(i18n("Generate summaries when sessions end"), this);
    connect(m_enableSummariesCheck, &QCheckBox::toggled,
            this, &KateCodeConfigPage::onSettingChanged);
    summaryLayout->addRow(m_enableSummariesCheck);

    m_summaryModelCombo = new QComboBox(this);
    m_summaryModelCombo->addItem(QStringLiteral("claude-3-5-haiku-20241022"), QStringLiteral("claude-3-5-haiku-20241022"));
    m_summaryModelCombo->addItem(QStringLiteral("claude-3-5-sonnet-20241022"), QStringLiteral("claude-3-5-sonnet-20241022"));
    m_summaryModelCombo->addItem(QStringLiteral("claude-3-haiku-20240307"), QStringLiteral("claude-3-haiku-20240307"));
    connect(m_summaryModelCombo, &QComboBox::currentIndexChanged,
            this, &KateCodeConfigPage::onSettingChanged);
    summaryLayout->addRow(i18n("Summary model:"), m_summaryModelCombo);

    auto *summaryNote = new QLabel(i18n("Summaries are stored in ~/.kate-code/summaries/ and can be used as context when resuming sessions."), this);
    summaryNote->setWordWrap(true);
    summaryNote->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    summaryLayout->addRow(summaryNote);

    mainLayout->addWidget(summaryGroup);

    // Session Management Group
    auto *sessionGroup = new QGroupBox(i18n("Session Management"), this);
    auto *sessionLayout = new QVBoxLayout(sessionGroup);

    m_autoResumeCheck = new QCheckBox(i18n("Prompt to resume previous session when connecting"), this);
    connect(m_autoResumeCheck, &QCheckBox::toggled,
            this, &KateCodeConfigPage::onSettingChanged);
    sessionLayout->addWidget(m_autoResumeCheck);

    mainLayout->addWidget(sessionGroup);

    // Stretch to push everything to top
    mainLayout->addStretch();

    updateApiKeyStatus();
}

void KateCodeConfigPage::apply()
{
    if (!m_hasChanges) {
        return;
    }

    // Save API key if changed
    QString newApiKey = m_apiKeyEdit->text();
    if (newApiKey != m_settings->apiKey()) {
        if (!newApiKey.isEmpty()) {
            m_settings->saveApiKey(newApiKey);
        }
        // Note: If key is now empty but was set before, user needs to clear it manually in KWallet
    }

    // Save other settings
    m_settings->setSummariesEnabled(m_enableSummariesCheck->isChecked());
    m_settings->setSummaryModel(m_summaryModelCombo->currentData().toString());
    m_settings->setAutoResumeSessions(m_autoResumeCheck->isChecked());

    m_hasChanges = false;
}

void KateCodeConfigPage::defaults()
{
    m_apiKeyEdit->clear();
    m_enableSummariesCheck->setChecked(false);
    m_summaryModelCombo->setCurrentIndex(0);
    m_autoResumeCheck->setChecked(true);
    m_hasChanges = true;
    Q_EMIT changed();
}

void KateCodeConfigPage::reset()
{
    // Load current settings
    m_enableSummariesCheck->setChecked(m_settings->summariesEnabled());

    QString currentModel = m_settings->summaryModel();
    int modelIndex = m_summaryModelCombo->findData(currentModel);
    if (modelIndex >= 0) {
        m_summaryModelCombo->setCurrentIndex(modelIndex);
    }

    m_autoResumeCheck->setChecked(m_settings->autoResumeSessions());

    // API key is loaded asynchronously
    m_hasChanges = false;
}

void KateCodeConfigPage::onApiKeyLoaded(bool success)
{
    if (success && m_settings->hasApiKey()) {
        // Show placeholder for existing key
        m_apiKeyEdit->setPlaceholderText(i18n("(API key is stored in KWallet)"));
        // Don't show actual key, just indicate it exists
        m_apiKeyEdit->clear();
    }
    updateApiKeyStatus();
}

void KateCodeConfigPage::onApiKeySaved(bool success)
{
    if (success) {
        m_apiKeyEdit->clear();
        m_apiKeyEdit->setPlaceholderText(i18n("(API key is stored in KWallet)"));
    }
    updateApiKeyStatus();
}

void KateCodeConfigPage::onShowApiKeyToggled()
{
    m_apiKeyVisible = m_showApiKeyButton->isChecked();
    m_apiKeyEdit->setEchoMode(m_apiKeyVisible ? QLineEdit::Normal : QLineEdit::Password);
    m_showApiKeyButton->setText(m_apiKeyVisible ? i18n("Hide") : i18n("Show"));
}

void KateCodeConfigPage::onWalletError(const QString &message)
{
    m_apiKeyStatus->setText(i18n("<span style='color: red;'>%1</span>", message));
}

void KateCodeConfigPage::onSettingChanged()
{
    m_hasChanges = true;
    Q_EMIT changed();
}

void KateCodeConfigPage::updateApiKeyStatus()
{
    if (!m_settings->isWalletAvailable()) {
        m_apiKeyStatus->setText(i18n("<span style='color: orange;'>KWallet is not available. Session summaries will be disabled.</span>"));
        m_enableSummariesCheck->setEnabled(false);
        m_summaryModelCombo->setEnabled(false);
    } else if (m_settings->hasApiKey()) {
        m_apiKeyStatus->setText(i18n("<span style='color: green;'>API key is stored in KWallet</span>"));
        m_enableSummariesCheck->setEnabled(true);
        m_summaryModelCombo->setEnabled(true);
    } else {
        m_apiKeyStatus->setText(i18n("No API key configured. Enter your key and click Apply to save."));
        m_enableSummariesCheck->setEnabled(false);
        m_summaryModelCombo->setEnabled(false);
    }
}
