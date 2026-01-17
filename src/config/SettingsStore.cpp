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
