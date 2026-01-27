#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QString>

namespace KWallet {
class Wallet;
}

// ACP backend types
enum class ACPBackend {
    ClaudeCodeACP,  // claude-code-acp (default)
    VibeACP,        // vibe (mistral-vibe)
    Custom,         // User-specified executable
};

// Color schemes for diff highlighting (colorblind-friendly options)
enum class DiffColorScheme {
    RedGreen,    // Traditional: red for deletions (default)
    BlueOrange,  // Colorblind-friendly: blue for deletions, orange for additions
    PurpleGreen, // Alternative colorblind-friendly
};

// Color pair for diff highlighting
struct DiffColors {
    QColor deletionBackground;
    QColor deletionForeground;
    QColor additionBackground;
    QColor additionForeground;
};

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

    // ACP backend settings
    ACPBackend acpBackend() const;
    void setACPBackend(ACPBackend backend);
    QString acpCustomExecutable() const;
    void setACPCustomExecutable(const QString &executable);

    // Returns the executable name and arguments for the current backend
    QString acpExecutableName() const;
    QStringList acpExecutableArgs() const;

    static QString backendDisplayName(ACPBackend backend);

    // Diff color scheme settings
    DiffColorScheme diffColorScheme() const;
    void setDiffColorScheme(DiffColorScheme scheme);
    DiffColors diffColors() const;

    // Static helper to get colors for a scheme
    // forLightBackground: true if the code block background is light (needs brighter diff colors)
    static DiffColors colorsForScheme(DiffColorScheme scheme, bool forLightBackground = false);
    static QString schemeDisplayName(DiffColorScheme scheme);

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
