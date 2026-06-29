#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

namespace KWallet {
class Wallet;
}

// ACP provider definition
struct ACPProvider {
    QString id;           // Stable identifier (e.g. "claude-code", "vibe-mistral", "custom-1")
    QString description;  // Display name
    QString executable;   // Binary name or path
    QString options;      // Command-line arguments string
    QString mcpConfigPath; // Path to MCP server config JSON file (e.g. ~/.cursor/mcp.json)
    QJsonObject sessionConfig; // ACP session config option id -> desired JSON value
    bool builtin = false; // true for the default-seeded providers (kept for reference only)
    bool trueResume = false; // try ACP session/load (option B) before falling back to context injection (option A)
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

    // Summarise an abandoned (raw) session with the summary model when resuming
    bool summariseOnResume() const;
    void setSummariseOnResume(bool enable);

    // ACP session logging to file (separate from on-screen output)
    bool acpLogEnabled() const;
    void setAcpLogEnabled(bool enable);
    QString acpLogDirectory() const;       // base directory to place the log folder in
    void setAcpLogDirectory(const QString &dir);
    QString acpLogSubdirectory() const;    // name of the folder created inside the base directory
    void setAcpLogSubdirectory(const QString &name);

    // ACP provider management
    QList<ACPProvider> providers() const;
    ACPProvider activeProvider() const;
    QString activeProviderId() const;
    void setActiveProviderId(const QString &id);

    void addCustomProvider(const ACPProvider &provider);
    void updateCustomProvider(const QString &id, const ACPProvider &provider);
    void removeCustomProvider(const QString &id);
    void setProviderOrder(const QStringList &providerIds);

    // Check if an executable can be found on PATH or common directories
    static bool isExecutableAvailable(const QString &executable);

    // Debug settings
    bool debugLogging() const;
    void setDebugLogging(bool enable);

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
    void migrateOldBackendSettings();
    void migrateOldSummaryModel();
    void seedDefaultProvidersIfNeeded();
    QList<ACPProvider> defaultProviders() const;
    QList<ACPProvider> storedProviders() const;
    void writeProviders(const QList<ACPProvider> &list);

    mutable QSettings m_settings;
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
