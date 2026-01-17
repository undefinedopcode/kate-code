#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

namespace KWallet {
class Wallet;
}

class SettingsStore : public QObject
{
    Q_OBJECT

public:
    explicit SettingsStore(QObject *parent = nullptr);
    ~SettingsStore() override;

    // API Key (stored in KWallet)
    void loadApiKey();
    void saveApiKey(const QString &key);
    QString apiKey() const { return m_apiKey; }
    bool hasApiKey() const { return !m_apiKey.isEmpty(); }
    bool isWalletAvailable() const { return m_walletAvailable; }

    // Summary settings (stored in QSettings)
    bool summariesEnabled() const;
    void setSummariesEnabled(bool enable);

    QString summaryModel() const;
    void setSummaryModel(const QString &model);

    // Session settings
    bool autoResumeSessions() const;
    void setAutoResumeSessions(bool enable);

Q_SIGNALS:
    void apiKeyLoaded(bool success);
    void apiKeySaved(bool success);
    void settingsChanged();
    void walletError(const QString &message);

private Q_SLOTS:
    void onWalletOpened(bool success);

private:
    void openWallet();
    void closeWallet();

    QSettings m_settings;
    KWallet::Wallet *m_wallet;
    QString m_apiKey;
    bool m_walletAvailable;

    enum class WalletOperation { None, Load, Save };
    WalletOperation m_pendingOperation;
    QString m_pendingApiKey;

    static const QString WALLET_FOLDER;
    static const QString API_KEY_ENTRY;
    static const QString DEFAULT_SUMMARY_MODEL;
};
