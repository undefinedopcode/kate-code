#include "KateCodeConfigPage.h"
#include "SettingsStore.h"

#include <KLocalizedString>

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

static QJsonValue parseSessionConfigValue(const QString &text);

static QString sessionConfigValueText(const QJsonValue &value)
{
    if (value.isString()) {
        const QString text = value.toString();
        const QJsonValue reparsed = parseSessionConfigValue(text);
        if (reparsed.isString() && reparsed.toString() == text) {
            return text;
        }

        // Quote strings such as "true", "42", or JSON-looking text so an
        // unchanged edit round-trips as a string rather than changing type.
        QJsonArray wrapper;
        wrapper.append(text);
        QString encoded = QString::fromUtf8(QJsonDocument(wrapper).toJson(QJsonDocument::Compact));
        return encoded.mid(1, encoded.size() - 2);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.isNull() || value.isUndefined()) {
        return QStringLiteral("null");
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

static QJsonValue parseSessionConfigValue(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    QJsonParseError parseError;
    const QByteArray wrapped = QByteArrayLiteral("{\"value\":") + trimmed.toUtf8() + QByteArrayLiteral("}");
    const QJsonDocument document = QJsonDocument::fromJson(wrapped, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        return document.object().value(QStringLiteral("value"));
    }
    return trimmed;
}

static QString providerToolTip(const ACPProvider &provider)
{
    QStringList lines = {
        i18n("Executable: %1", provider.executable),
        i18n("CLI options: %1", provider.options.isEmpty() ? i18n("(none)") : provider.options),
        i18n("MCP config JSON: %1", provider.mcpConfigPath.isEmpty() ? i18n("(none)") : provider.mcpConfigPath),
        i18n("True resume: %1", provider.trueResume ? i18n("Yes") : i18n("No")),
    };
    if (provider.sessionConfig.isEmpty()) {
        lines.append(i18n("ACP session configuration: (none)"));
    } else {
        lines.append(i18n("ACP session configuration:"));
        for (auto it = provider.sessionConfig.constBegin(); it != provider.sessionConfig.constEnd(); ++it) {
            lines.append(QStringLiteral("  %1 = %2").arg(it.key(), sessionConfigValueText(it.value())));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

KateCodeConfigPage::KateCodeConfigPage(SettingsStore *settings, QWidget *parent)
    : KTextEditor::ConfigPage(parent)
    , m_settings(settings)
    , m_hasChanges(false)
{
    setupUi();

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

    // Create Advanced tab
    auto *advancedTab = new QWidget();
    setupAdvancedTab(advancedTab);
    m_tabWidget->addTab(advancedTab, i18n("Advanced"));

    mainLayout->addWidget(m_tabWidget);
}

void KateCodeConfigPage::setupGeneralTab(QWidget *tab)
{
    auto *tabLayout = new QVBoxLayout(tab);

    // ACP Providers Group
    auto *providerGroup = new QGroupBox(i18n("ACP Providers"), tab);
    auto *providerLayout = new QVBoxLayout(providerGroup);

    m_providerList = new QListWidget(tab);
    m_providerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_providerList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_providerList->setDragDropMode(QAbstractItemView::InternalMove);
    m_providerList->setDefaultDropAction(Qt::MoveAction);
    m_providerList->setDropIndicatorShown(true);
    m_providerList->setToolTip(i18n("Drag providers to reorder them. Hover over a provider to see its full configuration."));
    providerLayout->addWidget(m_providerList);

    auto *buttonLayout = new QHBoxLayout();
    m_addProviderButton = new QPushButton(i18n("Add..."), tab);
    m_editProviderButton = new QPushButton(i18n("Edit..."), tab);
    m_removeProviderButton = new QPushButton(i18n("Remove"), tab);
    m_moveProviderUpButton = new QPushButton(i18n("Move Up"), tab);
    m_moveProviderDownButton = new QPushButton(i18n("Move Down"), tab);
    m_editProviderButton->setEnabled(false);
    m_removeProviderButton->setEnabled(false);
    m_moveProviderUpButton->setEnabled(false);
    m_moveProviderDownButton->setEnabled(false);
    buttonLayout->addWidget(m_addProviderButton);
    buttonLayout->addWidget(m_editProviderButton);
    buttonLayout->addWidget(m_removeProviderButton);
    buttonLayout->addWidget(m_moveProviderUpButton);
    buttonLayout->addWidget(m_moveProviderDownButton);
    buttonLayout->addStretch();
    providerLayout->addLayout(buttonLayout);

    connect(m_addProviderButton, &QPushButton::clicked, this, &KateCodeConfigPage::onAddProvider);
    connect(m_editProviderButton, &QPushButton::clicked, this, &KateCodeConfigPage::onEditProvider);
    connect(m_removeProviderButton, &QPushButton::clicked, this, &KateCodeConfigPage::onRemoveProvider);
    connect(m_moveProviderUpButton, &QPushButton::clicked, this, &KateCodeConfigPage::onMoveProviderUp);
    connect(m_moveProviderDownButton, &QPushButton::clicked, this, &KateCodeConfigPage::onMoveProviderDown);
    connect(m_providerList, &QListWidget::currentRowChanged, this, [this](int) {
        updateProviderButtons();
    });
    connect(m_providerList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        onEditProvider();
    });
    connect(m_providerList->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
        saveProviderOrder();
        updateProviderButtons();
    });

    auto *providerNote = new QLabel(i18n("Drag providers to set their order; only descriptions are shown here. Edit a provider to see its full launch, MCP, resume, and ACP session configuration. At least one provider must remain."), tab);
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

void KateCodeConfigPage::setupAdvancedTab(QWidget *tab)
{
    auto *tabLayout = new QVBoxLayout(tab);

    // Session Summaries Group
    auto *summaryGroup = new QGroupBox(i18n("Session Summaries"), tab);
    auto *summaryLayout = new QFormLayout(summaryGroup);

    m_enableSummariesCheck = new QCheckBox(i18n("Generate summaries when sessions end"), tab);
    connect(m_enableSummariesCheck, &QCheckBox::toggled,
            this, &KateCodeConfigPage::onSettingChanged);
    summaryLayout->addRow(m_enableSummariesCheck);

    m_summaryProviderCombo = new QComboBox(tab);
    connect(m_summaryProviderCombo, &QComboBox::currentIndexChanged,
            this, &KateCodeConfigPage::onSettingChanged);
    summaryLayout->addRow(i18n("Summary agent:"), m_summaryProviderCombo);

    auto *summaryNote = new QLabel(i18n("The selected agent generates the summary when a session ends. "
                                        "\"Current agent\" asks the live session to summarise itself before it disconnects; "
                                        "other agents run briefly in the background. "
                                        "Summaries are stored in ~/.kate-code/summaries/ and can be used as context when resuming sessions."), tab);
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

    m_summariseOnResumeCheck = new QCheckBox(i18n("Summarise an abandoned (raw) session before resuming"), tab);
    connect(m_summariseOnResumeCheck, &QCheckBox::toggled,
            this, &KateCodeConfigPage::onSettingChanged);
    sessionLayout->addWidget(m_summariseOnResumeCheck);

    auto *resumeNote = new QLabel(i18n("When resuming a session that has no summary yet, generate one with the summary agent first. Otherwise the raw transcript is used as context."), tab);
    resumeNote->setWordWrap(true);
    resumeNote->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    sessionLayout->addWidget(resumeNote);

    tabLayout->addWidget(sessionGroup);

    // ACP File Logging Group
    auto *logGroup = new QGroupBox(i18n("ACP Session Logging"), tab);
    auto *logLayout = new QFormLayout(logGroup);

    m_acpLogEnableCheck = new QCheckBox(i18n("Write raw ACP JSON traffic to a file"), tab);
    connect(m_acpLogEnableCheck, &QCheckBox::toggled,
            this, &KateCodeConfigPage::onSettingChanged);
    logLayout->addRow(m_acpLogEnableCheck);

    m_acpLogDirEdit = new QLineEdit(tab);
    m_acpLogDirEdit->setPlaceholderText(i18n("e.g. /tmp"));
    connect(m_acpLogDirEdit, &QLineEdit::textChanged,
            this, &KateCodeConfigPage::onSettingChanged);
    logLayout->addRow(i18n("Base directory:"), m_acpLogDirEdit);

    m_acpLogSubdirEdit = new QLineEdit(tab);
    m_acpLogSubdirEdit->setPlaceholderText(i18n("e.g. kate_code_sessions"));
    connect(m_acpLogSubdirEdit, &QLineEdit::textChanged,
            this, &KateCodeConfigPage::onSettingChanged);
    logLayout->addRow(i18n("Subdirectory name:"), m_acpLogSubdirEdit);

    auto *logNote = new QLabel(i18n("Each session writes one timestamped .json file (one JSON object per line, flushed immediately) into the subdirectory created inside the base directory. This is separate from the on-screen chat."), tab);
    logNote->setWordWrap(true);
    logNote->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    logLayout->addRow(logNote);

    tabLayout->addWidget(logGroup);

    // Command Auto-Approval Group
    auto *approvalGroup = new QGroupBox(i18n("Command Auto-Approval"), tab);
    auto *approvalLayout = new QVBoxLayout(approvalGroup);

    auto *approvalNote = new QLabel(
        i18n("Commands run by the agent whose text matches one of these glob patterns "
             "are approved automatically without prompting. Enter one pattern per line. "
             "Leave empty to always prompt.\n"
             "Examples: git status, ls *, npm run *"),
        tab);
    approvalNote->setWordWrap(true);
    approvalLayout->addWidget(approvalNote);

    m_allowedCommandsEdit = new QPlainTextEdit(tab);
    m_allowedCommandsEdit->setPlaceholderText(i18n("e.g.\ngit status\nls *\nnpm run *"));
    connect(m_allowedCommandsEdit, &QPlainTextEdit::textChanged,
            this, &KateCodeConfigPage::onSettingChanged);
    approvalLayout->addWidget(m_allowedCommandsEdit);

    tabLayout->addWidget(approvalGroup);

    // Stretch to push everything to top
    tabLayout->addStretch();
}

void KateCodeConfigPage::populateProviderList()
{
    const QString selectedId = m_providerList->currentItem()
        ? m_providerList->currentItem()->data(Qt::UserRole).toString()
        : QString();
    const QSignalBlocker blocker(m_providerList);
    m_providerList->clear();
    const auto providerList = m_settings->providers();
    for (const auto &p : providerList) {
        auto *item = new QListWidgetItem(p.description, m_providerList);
        item->setData(Qt::UserRole, p.id);
        item->setToolTip(providerToolTip(p));
        if (p.id == selectedId) {
            m_providerList->setCurrentItem(item);
        }
    }
    if (!m_providerList->currentItem() && m_providerList->count() > 0) {
        m_providerList->setCurrentRow(0);
    }
    updateProviderButtons();

    // The summary-agent dropdown mirrors the provider set, so refresh it too,
    // keeping the on-screen (possibly unapplied) selection.
    populateSummaryProviderCombo(m_summaryProviderCombo->currentData().toString());
}

void KateCodeConfigPage::populateSummaryProviderCombo(const QString &selectedId)
{
    const QSignalBlocker blocker(m_summaryProviderCombo);
    m_summaryProviderCombo->clear();
    m_summaryProviderCombo->addItem(i18n("Current agent"), SettingsStore::CURRENT_AGENT_PROVIDER_ID);
    const auto providerList = m_settings->providers();
    for (const auto &p : providerList) {
        m_summaryProviderCombo->addItem(p.description, p.id);
    }

    const int index = m_summaryProviderCombo->findData(selectedId);
    m_summaryProviderCombo->setCurrentIndex(index >= 0 ? index : 0);
}

void KateCodeConfigPage::apply()
{
    if (!m_hasChanges) {
        return;
    }

    // Save settings
    m_settings->setSummariesEnabled(m_enableSummariesCheck->isChecked());
    m_settings->setSummaryProviderId(m_summaryProviderCombo->currentData().toString());
    m_settings->setAutoResumeSessions(m_autoResumeCheck->isChecked());
    m_settings->setSummariseOnResume(m_summariseOnResumeCheck->isChecked());
    m_settings->setAcpLogEnabled(m_acpLogEnableCheck->isChecked());
    m_settings->setAcpLogDirectory(m_acpLogDirEdit->text().trimmed());
    m_settings->setAcpLogSubdirectory(m_acpLogSubdirEdit->text().trimmed());
    m_settings->setDiffColorScheme(static_cast<DiffColorScheme>(m_diffColorSchemeCombo->currentData().toInt()));
    m_settings->setDebugLogging(m_debugLoggingCheck->isChecked());

    // Save allowed command patterns (split on newlines, trim, drop blanks)
    const QStringList rawLines = m_allowedCommandsEdit->toPlainText().split(QLatin1Char('\n'));
    QStringList patterns;
    for (const QString &line : rawLines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            patterns.append(trimmed);
        }
    }
    m_settings->setAllowedCommandPatterns(patterns);

    m_hasChanges = false;
}

void KateCodeConfigPage::defaults()
{
    m_enableSummariesCheck->setChecked(false);
    m_summaryProviderCombo->setCurrentIndex(0);  // Current agent
    m_autoResumeCheck->setChecked(true);
    m_summariseOnResumeCheck->setChecked(false);
    m_acpLogEnableCheck->setChecked(false);
    m_acpLogDirEdit->setText(QDir::homePath() + QStringLiteral("/.kate-code"));
    m_acpLogSubdirEdit->setText(QStringLiteral("kate_code_sessions"));
    m_diffColorSchemeCombo->setCurrentIndex(0); // RedGreen (default)
    m_debugLoggingCheck->setChecked(false);
    m_allowedCommandsEdit->clear();
    m_hasChanges = true;
    Q_EMIT changed();
}

void KateCodeConfigPage::reset()
{
    // Load current settings
    m_enableSummariesCheck->setChecked(m_settings->summariesEnabled());
    populateSummaryProviderCombo(m_settings->summaryProviderId());

    m_autoResumeCheck->setChecked(m_settings->autoResumeSessions());
    m_summariseOnResumeCheck->setChecked(m_settings->summariseOnResume());

    // Load ACP file-logging settings
    m_acpLogEnableCheck->setChecked(m_settings->acpLogEnabled());
    m_acpLogDirEdit->setText(m_settings->acpLogDirectory());
    m_acpLogSubdirEdit->setText(m_settings->acpLogSubdirectory());

    // Load diff color scheme
    int schemeIndex = m_diffColorSchemeCombo->findData(static_cast<int>(m_settings->diffColorScheme()));
    if (schemeIndex >= 0) {
        m_diffColorSchemeCombo->setCurrentIndex(schemeIndex);
    }

    // Load debug setting
    m_debugLoggingCheck->setChecked(m_settings->debugLogging());

    // Load allowed command patterns (join with newlines for display)
    m_allowedCommandsEdit->setPlainText(m_settings->allowedCommandPatterns().join(QLatin1Char('\n')));

    // Load provider list
    populateProviderList();

    m_hasChanges = false;
}

void KateCodeConfigPage::onAddProvider()
{
    ACPProvider provider;
    provider.id = QStringLiteral("custom-%1").arg(QDateTime::currentMSecsSinceEpoch());
    provider.builtin = false;
    if (!editProviderDialog(provider, i18n("Add ACP Provider"))) {
        return;
    }

    m_settings->addCustomProvider(provider);
    populateProviderList();
    for (int row = 0; row < m_providerList->count(); ++row) {
        if (m_providerList->item(row)->data(Qt::UserRole).toString() == provider.id) {
            m_providerList->setCurrentRow(row);
            break;
        }
    }
}

void KateCodeConfigPage::onEditProvider()
{
    QListWidgetItem *item = m_providerList->currentItem();
    if (!item) {
        return;
    }

    ACPProvider provider = providerForItem(item);
    if (provider.id.isEmpty()) {
        return;
    }

    if (!editProviderDialog(provider, i18n("Edit ACP Provider"))) {
        return;
    }

    m_settings->updateCustomProvider(provider.id, provider);
    populateProviderList();
}

void KateCodeConfigPage::onRemoveProvider()
{
    QListWidgetItem *item = m_providerList->currentItem();
    if (!item) {
        return;
    }

    // Always keep at least one provider available.
    if (m_providerList->count() <= 1) {
        QMessageBox::warning(this, i18n("Cannot Remove Provider"),
            i18n("At least one provider must remain."));
        return;
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
    populateProviderList();
}

void KateCodeConfigPage::onMoveProviderUp()
{
    const int row = m_providerList->currentRow();
    if (row <= 0) {
        return;
    }
    QListWidgetItem *item = m_providerList->takeItem(row);
    m_providerList->insertItem(row - 1, item);
    m_providerList->setCurrentRow(row - 1);
    saveProviderOrder();
}

void KateCodeConfigPage::onMoveProviderDown()
{
    const int row = m_providerList->currentRow();
    if (row < 0 || row >= m_providerList->count() - 1) {
        return;
    }
    QListWidgetItem *item = m_providerList->takeItem(row);
    m_providerList->insertItem(row + 1, item);
    m_providerList->setCurrentRow(row + 1);
    saveProviderOrder();
}

ACPProvider KateCodeConfigPage::providerForItem(const QListWidgetItem *item) const
{
    if (!item) {
        return {};
    }
    const QString id = item->data(Qt::UserRole).toString();
    for (const ACPProvider &provider : m_settings->providers()) {
        if (provider.id == id) {
            return provider;
        }
    }
    return {};
}

void KateCodeConfigPage::updateProviderButtons()
{
    const int row = m_providerList->currentRow();
    const bool hasSelection = row >= 0;
    m_editProviderButton->setEnabled(hasSelection);
    m_removeProviderButton->setEnabled(hasSelection && m_providerList->count() > 1);
    m_moveProviderUpButton->setEnabled(hasSelection && row > 0);
    m_moveProviderDownButton->setEnabled(hasSelection && row < m_providerList->count() - 1);
}

void KateCodeConfigPage::saveProviderOrder()
{
    QStringList ids;
    ids.reserve(m_providerList->count());
    for (int row = 0; row < m_providerList->count(); ++row) {
        ids.append(m_providerList->item(row)->data(Qt::UserRole).toString());
    }
    m_settings->setProviderOrder(ids);
}

bool KateCodeConfigPage::editProviderDialog(ACPProvider &provider, const QString &title)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(760, 560);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    auto *formLayout = new QFormLayout();
    dialogLayout->addLayout(formLayout);

    auto *descEdit = new QLineEdit(provider.description, &dialog);
    descEdit->setPlaceholderText(i18n("e.g. Codex GPT-5.6 Sol"));
    formLayout->addRow(i18n("Description:"), descEdit);

    auto *exeEdit = new QLineEdit(provider.executable, &dialog);
    exeEdit->setPlaceholderText(i18n("e.g. codex-acp"));
    formLayout->addRow(i18n("Executable:"), exeEdit);

    auto *optEdit = new QLineEdit(provider.options, &dialog);
    optEdit->setPlaceholderText(i18n("Arguments passed to the ACP executable (optional)"));
    formLayout->addRow(i18n("CLI options:"), optEdit);

    auto *mcpEdit = new QLineEdit(provider.mcpConfigPath, &dialog);
    mcpEdit->setPlaceholderText(i18n("e.g. ~/.cursor/mcp.json (optional)"));
    formLayout->addRow(i18n("MCP config JSON:"), mcpEdit);

    auto *resumeCheck = new QCheckBox(i18n("Try real ACP session/load, fall back to context"), &dialog);
    resumeCheck->setChecked(provider.trueResume);
    formLayout->addRow(i18n("True resume:"), resumeCheck);

    auto *configLabel = new QLabel(i18n("ACP session configuration"), &dialog);
    QFont labelFont = configLabel->font();
    labelFont.setBold(true);
    configLabel->setFont(labelFont);
    dialogLayout->addWidget(configLabel);

    auto *configHelp = new QLabel(i18n("These key/value pairs are applied after session creation through ACP session/set_config_option when the agent advertises a matching option. Values may be plain text or JSON. For @agentclientprotocol/codex-acp use model (for example gpt-5.6-sol), reasoning_effort (for example xhigh), and mode (read-only, agent, or agent-full-access). The mode controls both approval and sandboxing; Codex config.toml keys such as approval_policy and sandbox_mode are not ACP session option ids."), &dialog);
    configHelp->setWordWrap(true);
    dialogLayout->addWidget(configHelp);

    auto *configTable = new QTableWidget(0, 2, &dialog);
    configTable->setHorizontalHeaderLabels({i18n("Configuration key"), i18n("Value (text or JSON)")});
    configTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    configTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    configTable->verticalHeader()->setVisible(false);
    configTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    configTable->setSelectionMode(QAbstractItemView::SingleSelection);
    dialogLayout->addWidget(configTable, 1);

    auto addConfigRow = [configTable](const QString &key = QString(), const QString &value = QString()) {
        const int row = configTable->rowCount();
        configTable->insertRow(row);
        configTable->setItem(row, 0, new QTableWidgetItem(key));
        configTable->setItem(row, 1, new QTableWidgetItem(value));
    };
    for (auto it = provider.sessionConfig.constBegin(); it != provider.sessionConfig.constEnd(); ++it) {
        addConfigRow(it.key(), sessionConfigValueText(it.value()));
    }

    auto *configButtons = new QHBoxLayout();
    auto *addConfigButton = new QPushButton(i18n("Add Parameter"), &dialog);
    auto *removeConfigButton = new QPushButton(i18n("Remove Parameter"), &dialog);
    removeConfigButton->setEnabled(false);
    configButtons->addWidget(addConfigButton);
    configButtons->addWidget(removeConfigButton);
    configButtons->addStretch();
    dialogLayout->addLayout(configButtons);

    connect(addConfigButton, &QPushButton::clicked, &dialog, [configTable, addConfigRow]() {
        addConfigRow();
        configTable->setCurrentCell(configTable->rowCount() - 1, 0);
        configTable->editItem(configTable->currentItem());
    });
    connect(removeConfigButton, &QPushButton::clicked, &dialog, [configTable]() {
        if (configTable->currentRow() >= 0) {
            configTable->removeRow(configTable->currentRow());
        }
    });
    connect(configTable, &QTableWidget::currentCellChanged, &dialog,
            [removeConfigButton](int row) { removeConfigButton->setEnabled(row >= 0); });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialogLayout->addWidget(buttons);

    while (dialog.exec() == QDialog::Accepted) {
        const QString description = descEdit->text().trimmed();
        const QString executable = exeEdit->text().trimmed();
        if (description.isEmpty() || executable.isEmpty()) {
            QMessageBox::warning(&dialog, i18n("Invalid Provider"), i18n("Description and Executable are required."));
            continue;
        }

        QJsonObject sessionConfig;
        bool valid = true;
        for (int row = 0; row < configTable->rowCount(); ++row) {
            const QString key = configTable->item(row, 0) ? configTable->item(row, 0)->text().trimmed() : QString();
            const QString value = configTable->item(row, 1) ? configTable->item(row, 1)->text() : QString();
            if (key.isEmpty() && value.trimmed().isEmpty()) {
                continue;
            }
            if (key.isEmpty()) {
                QMessageBox::warning(&dialog, i18n("Invalid Session Configuration"),
                                     i18n("Every ACP session configuration value requires a key."));
                valid = false;
                break;
            }
            if (sessionConfig.contains(key)) {
                QMessageBox::warning(&dialog, i18n("Invalid Session Configuration"),
                                     i18n("The ACP session configuration key \"%1\" is duplicated.", key));
                valid = false;
                break;
            }
            sessionConfig.insert(key, parseSessionConfigValue(value));
        }
        if (!valid) {
            continue;
        }

        provider.description = description;
        provider.executable = executable;
        provider.options = optEdit->text().trimmed();
        provider.mcpConfigPath = mcpEdit->text().trimmed();
        provider.trueResume = resumeCheck->isChecked();
        provider.sessionConfig = sessionConfig;
        return true;
    }
    return false;
}

void KateCodeConfigPage::onSettingChanged()
{
    m_hasChanges = true;
    Q_EMIT changed();
}
