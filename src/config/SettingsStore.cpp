#include "SettingsStore.h"

#include <KLocalizedString>
#include <kwallet.h>

const QString SettingsStore::WALLET_FOLDER = QStringLiteral("KateCode");
const QString SettingsStore::API_KEY_ENTRY = QStringLiteral("AnthropicApiKey");
const QString SettingsStore::DEFAULT_SUMMARY_MODEL = QStringLiteral("claude-3-5-haiku-20241022");

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("kate-code"), QStringLiteral("kate-code"))
    , m_wallet(nullptr)
    , m_walletAvailable(false)
    , m_pendingOperation(WalletOperation::None)
{
    qDebug() << "[SettingsStore] Initialized, config file:" << m_settings.fileName();
}

SettingsStore::~SettingsStore()
{
    closeWallet();
}

void SettingsStore::loadApiKey()
{
    if (m_wallet && m_wallet->isOpen()) {
        // Wallet already open, read directly
        if (!m_wallet->hasFolder(WALLET_FOLDER)) {
            m_apiKey.clear();
            Q_EMIT apiKeyLoaded(true);
            return;
        }

        m_wallet->setFolder(WALLET_FOLDER);
        QString key;
        int result = m_wallet->readPassword(API_KEY_ENTRY, key);
        if (result == 0) {
            m_apiKey = key;
            Q_EMIT apiKeyLoaded(true);
        } else {
            m_apiKey.clear();
            Q_EMIT apiKeyLoaded(false);
        }
        return;
    }

    // Need to open wallet first
    m_pendingOperation = WalletOperation::Load;
    openWallet();
}

void SettingsStore::saveApiKey(const QString &key)
{
    if (m_wallet && m_wallet->isOpen()) {
        // Wallet already open, write directly
        if (!m_wallet->hasFolder(WALLET_FOLDER)) {
            m_wallet->createFolder(WALLET_FOLDER);
        }
        m_wallet->setFolder(WALLET_FOLDER);

        int result = m_wallet->writePassword(API_KEY_ENTRY, key);
        if (result == 0) {
            m_wallet->sync();
            m_apiKey = key;
            Q_EMIT apiKeySaved(true);
            Q_EMIT settingsChanged();
        } else {
            Q_EMIT apiKeySaved(false);
            Q_EMIT walletError(i18n("Failed to save API key to wallet"));
        }
        return;
    }

    // Need to open wallet first
    m_pendingOperation = WalletOperation::Save;
    m_pendingApiKey = key;
    openWallet();
}

void SettingsStore::openWallet()
{
    if (m_wallet) {
        return;
    }

    m_wallet = KWallet::Wallet::openWallet(
        KWallet::Wallet::NetworkWallet(),
        0, // No parent window ID for async
        KWallet::Wallet::Asynchronous
    );

    if (!m_wallet) {
        m_walletAvailable = false;
        Q_EMIT walletError(i18n("KWallet is not available. Please ensure the KWallet service is running."));

        if (m_pendingOperation == WalletOperation::Load) {
            Q_EMIT apiKeyLoaded(false);
        } else if (m_pendingOperation == WalletOperation::Save) {
            Q_EMIT apiKeySaved(false);
        }
        m_pendingOperation = WalletOperation::None;
        m_pendingApiKey.clear();
        return;
    }

    connect(m_wallet, &KWallet::Wallet::walletOpened,
            this, &SettingsStore::onWalletOpened);
}

void SettingsStore::onWalletOpened(bool success)
{
    m_walletAvailable = success;

    if (!success) {
        Q_EMIT walletError(i18n("Failed to open KWallet. The wallet may be locked."));
        closeWallet();

        if (m_pendingOperation == WalletOperation::Load) {
            Q_EMIT apiKeyLoaded(false);
        } else if (m_pendingOperation == WalletOperation::Save) {
            Q_EMIT apiKeySaved(false);
        }
        m_pendingOperation = WalletOperation::None;
        m_pendingApiKey.clear();
        return;
    }

    // Process pending operation
    WalletOperation op = m_pendingOperation;
    m_pendingOperation = WalletOperation::None;

    if (op == WalletOperation::Load) {
        loadApiKey();
    } else if (op == WalletOperation::Save) {
        QString key = m_pendingApiKey;
        m_pendingApiKey.clear();
        saveApiKey(key);
    }
}

void SettingsStore::closeWallet()
{
    if (m_wallet) {
        m_wallet->deleteLater();
        m_wallet = nullptr;
    }
}

bool SettingsStore::summariesEnabled() const
{
    return m_settings.value(QStringLiteral("Summaries/enabled"), false).toBool();
}

void SettingsStore::setSummariesEnabled(bool enable)
{
    m_settings.setValue(QStringLiteral("Summaries/enabled"), enable);
    m_settings.sync();
    Q_EMIT settingsChanged();
}

QString SettingsStore::summaryModel() const
{
    return m_settings.value(QStringLiteral("Summaries/model"), DEFAULT_SUMMARY_MODEL).toString();
}

void SettingsStore::setSummaryModel(const QString &model)
{
    m_settings.setValue(QStringLiteral("Summaries/model"), model);
    m_settings.sync();
    Q_EMIT settingsChanged();
}

bool SettingsStore::autoResumeSessions() const
{
    return m_settings.value(QStringLiteral("Sessions/autoResume"), true).toBool();
}

void SettingsStore::setAutoResumeSessions(bool enable)
{
    m_settings.setValue(QStringLiteral("Sessions/autoResume"), enable);
    m_settings.sync();
    Q_EMIT settingsChanged();
}

ACPBackend SettingsStore::acpBackend() const
{
    int backend = m_settings.value(QStringLiteral("ACP/backend"), 0).toInt();
    return static_cast<ACPBackend>(backend);
}

void SettingsStore::setACPBackend(ACPBackend backend)
{
    m_settings.setValue(QStringLiteral("ACP/backend"), static_cast<int>(backend));
    m_settings.sync();
    Q_EMIT settingsChanged();
}

QString SettingsStore::acpCustomExecutable() const
{
    return m_settings.value(QStringLiteral("ACP/customExecutable")).toString();
}

void SettingsStore::setACPCustomExecutable(const QString &executable)
{
    m_settings.setValue(QStringLiteral("ACP/customExecutable"), executable);
    m_settings.sync();
    Q_EMIT settingsChanged();
}

QString SettingsStore::acpExecutableName() const
{
    switch (acpBackend()) {
    case ACPBackend::VibeACP:
        return QStringLiteral("vibe-acp");
    case ACPBackend::Custom:
        return acpCustomExecutable();
    case ACPBackend::ClaudeCodeACP:
    default:
        return QStringLiteral("claude-code-acp");
    }
}

QStringList SettingsStore::acpExecutableArgs() const
{
    // No extra arguments needed for any backend currently
    return QStringList();
}

QString SettingsStore::backendDisplayName(ACPBackend backend)
{
    switch (backend) {
    case ACPBackend::VibeACP:
        return QStringLiteral("Vibe (Mistral)");
    case ACPBackend::Custom:
        return QStringLiteral("Custom");
    case ACPBackend::ClaudeCodeACP:
    default:
        return QStringLiteral("Claude Code ACP (default)");
    }
}

DiffColorScheme SettingsStore::diffColorScheme() const
{
    int scheme = m_settings.value(QStringLiteral("Diffs/colorScheme"), 0).toInt();
    qDebug() << "[SettingsStore] diffColorScheme() returning:" << scheme;
    return static_cast<DiffColorScheme>(scheme);
}

void SettingsStore::setDiffColorScheme(DiffColorScheme scheme)
{
    m_settings.setValue(QStringLiteral("Diffs/colorScheme"), static_cast<int>(scheme));
    m_settings.sync();
    Q_EMIT settingsChanged();
}

DiffColors SettingsStore::diffColors() const
{
    return colorsForScheme(diffColorScheme());
}

DiffColors SettingsStore::colorsForScheme(DiffColorScheme scheme, bool forLightBackground)
{
    DiffColors colors;

    // Colors are optimized for contrast against their target background:
    // - Dark backgrounds: muted, darker colors that don't overwhelm
    // - Light backgrounds: brighter, more saturated colors for visibility

    if (forLightBackground) {
        // Light background colors - brighter and more saturated for contrast
        switch (scheme) {
        case DiffColorScheme::BlueOrange:
            // Colorblind-friendly: blue for deletions, orange for additions
            colors.deletionBackground = QColor(200, 210, 240);   // Light blue
            colors.deletionForeground = QColor(30, 60, 150);     // Dark blue text
            colors.additionBackground = QColor(255, 230, 200);   // Light orange
            colors.additionForeground = QColor(150, 70, 0);      // Dark orange text
            break;

        case DiffColorScheme::PurpleGreen:
            // Alternative colorblind-friendly: purple for deletions
            colors.deletionBackground = QColor(230, 210, 245);   // Light purple
            colors.deletionForeground = QColor(100, 40, 140);    // Dark purple text
            colors.additionBackground = QColor(210, 245, 210);   // Light green
            colors.additionForeground = QColor(30, 100, 30);     // Dark green text
            break;

        case DiffColorScheme::RedGreen:
        default:
            // Traditional: red for deletions, green for additions
            colors.deletionBackground = QColor(255, 220, 220);   // Light red/pink
            colors.deletionForeground = QColor(150, 30, 30);     // Dark red text
            colors.additionBackground = QColor(210, 255, 220);   // Light green
            colors.additionForeground = QColor(30, 100, 30);     // Dark green text
            break;
        }
    } else {
        // Dark background colors - muted and darker
        switch (scheme) {
        case DiffColorScheme::BlueOrange:
            // Colorblind-friendly: blue for deletions, orange for additions
            colors.deletionBackground = QColor(50, 53, 77);      // Dark muted blue
            colors.deletionForeground = QColor(50, 80, 180);     // Dark blue
            colors.additionBackground = QColor(77, 58, 40);      // Dark muted orange
            colors.additionForeground = QColor(180, 100, 40);    // Dark orange
            break;

        case DiffColorScheme::PurpleGreen:
            // Alternative colorblind-friendly: purple for deletions
            colors.deletionBackground = QColor(58, 40, 77);      // Dark muted purple
            colors.deletionForeground = QColor(120, 60, 160);    // Dark purple
            colors.additionBackground = QColor(40, 77, 40);      // Dark muted green
            colors.additionForeground = QColor(40, 140, 40);     // Dark green
            break;

        case DiffColorScheme::RedGreen:
        default:
            // Traditional: red for deletions, green for additions
            colors.deletionBackground = QColor(122, 67, 71);     // Dark muted red (#7a4347)
            colors.deletionForeground = QColor(180, 60, 60);     // Dark red
            colors.additionBackground = QColor(39, 88, 80);      // Dark muted teal (#275850)
            colors.additionForeground = QColor(60, 140, 60);     // Dark green
            break;
        }
    }

    return colors;
}

QString SettingsStore::schemeDisplayName(DiffColorScheme scheme)
{
    switch (scheme) {
    case DiffColorScheme::BlueOrange:
        return QStringLiteral("Blue / Orange (colorblind-friendly)");
    case DiffColorScheme::PurpleGreen:
        return QStringLiteral("Purple / Green (colorblind-friendly)");
    case DiffColorScheme::RedGreen:
    default:
        return QStringLiteral("Red / Green (default)");
    }
}
