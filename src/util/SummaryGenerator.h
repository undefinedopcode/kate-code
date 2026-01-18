#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class SettingsStore;
class QNetworkReply;

class SummaryGenerator : public QObject
{
    Q_OBJECT

public:
    explicit SummaryGenerator(SettingsStore *settings, QObject *parent = nullptr);
    ~SummaryGenerator() override;

    void generateSummary(const QString &sessionId,
                         const QString &projectRoot,
                         const QString &transcriptContent);

    bool isGenerating() const { return !m_pendingRequests.isEmpty(); }

    // Block until all pending requests complete (for shutdown)
    void waitForPendingRequests(int timeoutMs = 30000);

Q_SIGNALS:
    void summaryReady(const QString &sessionId, const QString &projectRoot, const QString &summary);
    void summaryError(const QString &sessionId, const QString &error);

private Q_SLOTS:
    void onNetworkReply(QNetworkReply *reply);

private:
    QString buildPrompt(const QString &transcriptContent);
    QString truncateTranscript(const QString &transcript, int maxChars = 50000);

    SettingsStore *m_settings;
    QNetworkAccessManager *m_networkManager;

    struct PendingRequest {
        QString sessionId;
        QString projectRoot;
    };
    QMap<QNetworkReply *, PendingRequest> m_pendingRequests;
};
