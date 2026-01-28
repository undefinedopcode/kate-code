#include "KateCodeConfigPage.h"
#include "SettingsStore.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
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
    return QIcon::fromTheme(QStringLiteral("code-context"));
}

void KateCodeConfigPage::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Create tab widget
    m_tabWidget = new QTabWidget(this);

    // Create General tab
    auto *generalTab = new QWidget();
    setupGeneralTab(generalTab);
    m_tabWidget->addTab(generalTab, i18n("General"));

    // Create Summaries tab
    auto *summariesTab = new QWidget();
    setupSummariesTab(summariesTab);
    m_tabWidget->addTab(summariesTab, i18n("Summaries"));

    mainLayout->addWidget(m_tabWidget);

    updateApiKeyStatus();
}

void KateCodeConfigPage::setupGeneralTab(QWidget *tab)
{
    auto *tabLayout = new QVBoxLayout(tab);

    // ACP Providers Group
    auto *providerGroup = new QGroupBox(i18n("ACP Providers"), tab);
    auto *providerLayout = new QVBoxLayout(providerGroup);

    m_providerTable = new QTableWidget(0, 3, tab);
    m_providerTable->setHorizontalHeaderLabels({i18n("Description"), i18n("Executable"), i18n("Options")});
    m_providerTable->horizontalHeader()->setStretchLastSection(true);
    m_providerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_providerTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_providerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_providerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_providerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_providerTable->verticalHeader()->setVisible(false);
    providerLayout->addWidget(m_providerTable);

    auto *buttonLayout = new QHBoxLayout();
    m_addProviderButton = new QPushButton(i18n("Add..."), tab);
    m_editProviderButton = new QPushButton(i18n("Edit..."), tab);
    m_removeProviderButton = new QPushButton(i18n("Remove"), tab);
    m_editProviderButton->setEnabled(false);
    m_removeProviderButton->setEnabled(false);
    buttonLayout->addWidget(m_addProviderButton);
    buttonLayout->addWidget(m_editProviderButton);
    buttonLayout->addWidget(m_removeProviderButton);
    buttonLayout->addStretch();
    providerLayout->addLayout(buttonLayout);

    connect(m_addProviderButton, &QPushButton::clicked, this, &KateCodeConfigPage::onAddProvider);
    connect(m_editProviderButton, &QPushButton::clicked, this, &KateCodeConfigPage::onEditProvider);
    connect(m_removeProviderButton, &QPushButton::clicked, this, &KateCodeConfigPage::onRemoveProvider);
    connect(m_providerTable, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (row < 0) {
            m_editProviderButton->setEnabled(false);
            m_removeProviderButton->setEnabled(false);
            return;
        }
        // Check if this row is a builtin provider (stored in column 0 user data)
        auto *item = m_providerTable->item(row, 0);
        bool isBuiltin = item && item->data(Qt::UserRole + 1).toBool();
        m_editProviderButton->setEnabled(!isBuiltin);
        m_removeProviderButton->setEnabled(!isBuiltin);
    });

    auto *providerNote = new QLabel(i18n("Built-in providers cannot be edited or removed. Use the dropdown in the chat panel header to select the active provider."), tab);
    providerNote->setWordWrap(true);
    providerNote->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    providerLayout->addWidget(providerNote);

    tabLayout->addWidget(providerGroup);

    // Diff Colors Group
    auto *diffGroup = new QGroupBox(i18n("Diff Highlighting"), tab);
    auto *diffLayout = new QFormLayout(diffGroup);

    m_diffColorSchemeCombo = new QComboBox(tab);
    m_diffColorSchemeCombo->addItem(
        SettingsStore::schemeDisplayName(DiffColorScheme::RedGreen),
        static_cast<int>(DiffColorScheme::RedGreen));
    m_diffColorSchemeCombo->addItem(
        SettingsStore::schemeDisplayName(DiffColorScheme::BlueOrange),
        static_cast<int>(DiffColorScheme::BlueOrange));
    m_diffColorSchemeCombo->addItem(
        SettingsStore::schemeDisplayName(DiffColorScheme::PurpleGreen),
        static_cast<int>(DiffColorScheme::PurpleGreen));
    connect(m_diffColorSchemeCombo, &QComboBox::currentIndexChanged,
            this, &KateCodeConfigPage::onSettingChanged);
    diffLayout->addRow(i18n("Color scheme:"), m_diffColorSchemeCombo);

    auto *diffNote = new QLabel(i18n("Choose a colorblind-friendly scheme if you have difficulty distinguishing red and green."), tab);
    diffNote->setWordWrap(true);
    diffNote->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    diffLayout->addRow(diffNote);

    tabLayout->addWidget(diffGroup);

    // Debugging Group
    auto *debugGroup = new QGroupBox(i18n("Debugging"), tab);
    auto *debugLayout = new QVBoxLayout(debugGroup);

    m_debugLoggingCheck = new QCheckBox(i18n("Log ACP protocol JSON to Output view"), tab);
    connect(m_debugLoggingCheck, &QCheckBox::toggled,
            this, &KateCodeConfigPage::onSettingChanged);
    debugLayout->addWidget(m_debugLoggingCheck);

    auto *debugNote = new QLabel(i18n("When enabled, all JSON-RPC messages sent to and received from the ACP server are logged to Kate's Output panel."), tab);
    debugNote->setWordWrap(true);
    debugNote->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    debugLayout->addWidget(debugNote);

    tabLayout->addWidget(debugGroup);

    // Stretch to push everything to top
    tabLayout->addStretch();
}

void KateCodeConfigPage::setupSummariesTab(QWidget *tab)
{
    auto *tabLayout = new QVBoxLayout(tab);

    // API Key Group
    auto *apiGroup = new QGroupBox(i18n("Anthropic API Key"), tab);
    auto *apiLayout = new QVBoxLayout(apiGroup);

    auto *apiKeyLayout = new QHBoxLayout();
    m_apiKeyEdit = new QLineEdit(tab);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(i18n("Enter your Anthropic API key"));
    connect(m_apiKeyEdit, &QLineEdit::textChanged,
            this, &KateCodeConfigPage::onSettingChanged);

    m_showApiKeyButton = new QPushButton(i18n("Show"), tab);
    m_showApiKeyButton->setCheckable(true);
    connect(m_showApiKeyButton, &QPushButton::clicked,
            this, &KateCodeConfigPage::onShowApiKeyToggled);

    apiKeyLayout->addWidget(m_apiKeyEdit);
    apiKeyLayout->addWidget(m_showApiKeyButton);
    apiLayout->addLayout(apiKeyLayout);

    m_apiKeyStatus = new QLabel(tab);
    m_apiKeyStatus->setWordWrap(true);
    apiLayout->addWidget(m_apiKeyStatus);

    auto *apiNote = new QLabel(i18n("The API key is stored securely in KWallet and used for generating session summaries."), tab);
    apiNote->setWordWrap(true);
    apiNote->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    apiLayout->addWidget(apiNote);

    tabLayout->addWidget(apiGroup);

    // Session Summaries Group
    auto *summaryGroup = new QGroupBox(i18n("Session Summaries"), tab);
    auto *summaryLayout = new QFormLayout(summaryGroup);

    m_enableSummariesCheck = new QCheckBox(i18n("Generate summaries when sessions end"), tab);
    connect(m_enableSummariesCheck, &QCheckBox::toggled,
            this, &KateCodeConfigPage::onSettingChanged);
    summaryLayout->addRow(m_enableSummariesCheck);

    m_summaryModelCombo = new QComboBox(tab);
    m_summaryModelCombo->addItem(QStringLiteral("claude-3-5-haiku-20241022"), QStringLiteral("claude-3-5-haiku-20241022"));
    m_summaryModelCombo->addItem(QStringLiteral("claude-3-5-sonnet-20241022"), QStringLiteral("claude-3-5-sonnet-20241022"));
    m_summaryModelCombo->addItem(QStringLiteral("claude-3-haiku-20240307"), QStringLiteral("claude-3-haiku-20240307"));
    connect(m_summaryModelCombo, &QComboBox::currentIndexChanged,
            this, &KateCodeConfigPage::onSettingChanged);
    summaryLayout->addRow(i18n("Summary model:"), m_summaryModelCombo);

    auto *summaryNote = new QLabel(i18n("Summaries are stored in ~/.kate-code/summaries/ and can be used as context when resuming sessions."), tab);
    summaryNote->setWordWrap(true);
    summaryNote->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    summaryLayout->addRow(summaryNote);

    tabLayout->addWidget(summaryGroup);

    // Session Resume Group
    auto *sessionGroup = new QGroupBox(i18n("Session Resume"), tab);
    auto *sessionLayout = new QVBoxLayout(sessionGroup);

    m_autoResumeCheck = new QCheckBox(i18n("Prompt to resume previous session when connecting"), tab);
    connect(m_autoResumeCheck, &QCheckBox::toggled,
            this, &KateCodeConfigPage::onSettingChanged);
    sessionLayout->addWidget(m_autoResumeCheck);

    tabLayout->addWidget(sessionGroup);

    // Stretch to push everything to top
    tabLayout->addStretch();
}

void KateCodeConfigPage::populateProviderTable()
{
    m_providerTable->setRowCount(0);
    const auto providerList = m_settings->providers();
    for (const auto &p : providerList) {
        int row = m_providerTable->rowCount();
        m_providerTable->insertRow(row);

        auto *descItem = new QTableWidgetItem(p.description);
        descItem->setData(Qt::UserRole, p.id);           // Store provider id
        descItem->setData(Qt::UserRole + 1, p.builtin);  // Store builtin flag
        if (p.builtin) {
            descItem->setFlags(descItem->flags() & ~Qt::ItemIsEditable);
        }
        m_providerTable->setItem(row, 0, descItem);

        auto *exeItem = new QTableWidgetItem(p.executable);
        m_providerTable->setItem(row, 1, exeItem);

        auto *optItem = new QTableWidgetItem(p.options);
        m_providerTable->setItem(row, 2, optItem);
    }
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
    }

    // Save other settings
    m_settings->setSummariesEnabled(m_enableSummariesCheck->isChecked());
    m_settings->setSummaryModel(m_summaryModelCombo->currentData().toString());
    m_settings->setAutoResumeSessions(m_autoResumeCheck->isChecked());
    m_settings->setDiffColorScheme(static_cast<DiffColorScheme>(m_diffColorSchemeCombo->currentData().toInt()));
    m_settings->setDebugLogging(m_debugLoggingCheck->isChecked());

    m_hasChanges = false;
}

void KateCodeConfigPage::defaults()
{
    m_apiKeyEdit->clear();
    m_enableSummariesCheck->setChecked(false);
    m_summaryModelCombo->setCurrentIndex(0);
    m_autoResumeCheck->setChecked(true);
    m_diffColorSchemeCombo->setCurrentIndex(0); // RedGreen (default)
    m_debugLoggingCheck->setChecked(false);
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

    // Load diff color scheme
    int schemeIndex = m_diffColorSchemeCombo->findData(static_cast<int>(m_settings->diffColorScheme()));
    if (schemeIndex >= 0) {
        m_diffColorSchemeCombo->setCurrentIndex(schemeIndex);
    }

    // Load debug setting
    m_debugLoggingCheck->setChecked(m_settings->debugLogging());

    // Load provider table
    populateProviderTable();

    // API key is loaded asynchronously
    m_hasChanges = false;
}

void KateCodeConfigPage::onAddProvider()
{
    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Add ACP Provider"));
    auto *layout = new QFormLayout(&dialog);

    auto *descEdit = new QLineEdit(&dialog);
    descEdit->setPlaceholderText(i18n("e.g. Gemini"));
    layout->addRow(i18n("Description:"), descEdit);

    auto *exeEdit = new QLineEdit(&dialog);
    exeEdit->setPlaceholderText(i18n("e.g. terminal-agent"));
    layout->addRow(i18n("Executable:"), exeEdit);

    auto *optEdit = new QLineEdit(&dialog);
    optEdit->setPlaceholderText(i18n("e.g. --experimental-acp"));
    layout->addRow(i18n("Options:"), optEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString desc = descEdit->text().trimmed();
    QString exe = exeEdit->text().trimmed();
    if (desc.isEmpty() || exe.isEmpty()) {
        QMessageBox::warning(this, i18n("Invalid Provider"), i18n("Description and Executable are required."));
        return;
    }

    // Generate a unique id
    QString id = QStringLiteral("custom-%1").arg(QDateTime::currentMSecsSinceEpoch());

    ACPProvider provider;
    provider.id = id;
    provider.description = desc;
    provider.executable = exe;
    provider.options = optEdit->text().trimmed();
    provider.builtin = false;

    m_settings->addCustomProvider(provider);
    populateProviderTable();
}

void KateCodeConfigPage::onEditProvider()
{
    int row = m_providerTable->currentRow();
    if (row < 0) {
        return;
    }

    auto *item = m_providerTable->item(row, 0);
    if (!item || item->data(Qt::UserRole + 1).toBool()) {
        return; // Can't edit builtins
    }

    QString providerId = item->data(Qt::UserRole).toString();
    QString currentDesc = item->text();
    QString currentExe = m_providerTable->item(row, 1)->text();
    QString currentOpt = m_providerTable->item(row, 2)->text();

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Edit ACP Provider"));
    auto *layout = new QFormLayout(&dialog);

    auto *descEdit = new QLineEdit(currentDesc, &dialog);
    layout->addRow(i18n("Description:"), descEdit);

    auto *exeEdit = new QLineEdit(currentExe, &dialog);
    layout->addRow(i18n("Executable:"), exeEdit);

    auto *optEdit = new QLineEdit(currentOpt, &dialog);
    layout->addRow(i18n("Options:"), optEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString desc = descEdit->text().trimmed();
    QString exe = exeEdit->text().trimmed();
    if (desc.isEmpty() || exe.isEmpty()) {
        QMessageBox::warning(this, i18n("Invalid Provider"), i18n("Description and Executable are required."));
        return;
    }

    ACPProvider provider;
    provider.id = providerId;
    provider.description = desc;
    provider.executable = exe;
    provider.options = optEdit->text().trimmed();
    provider.builtin = false;

    m_settings->updateCustomProvider(providerId, provider);
    populateProviderTable();
}

void KateCodeConfigPage::onRemoveProvider()
{
    int row = m_providerTable->currentRow();
    if (row < 0) {
        return;
    }

    auto *item = m_providerTable->item(row, 0);
    if (!item || item->data(Qt::UserRole + 1).toBool()) {
        return; // Can't remove builtins
    }

    QString providerId = item->data(Qt::UserRole).toString();
    QString providerName = item->text();

    int result = QMessageBox::question(this,
        i18n("Remove Provider"),
        i18n("Remove provider \"%1\"?", providerName),
        QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) {
        return;
    }

    m_settings->removeCustomProvider(providerId);
    populateProviderTable();
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
