#pragma once

#include <QMap>
#include <QObject>
#include <QString>

class ACPService;
class SettingsStore;

// Generates session summaries by running the configured ACP provider headlessly
// (initialize -> session/new -> session/prompt -> collect the reply). When the
// summary provider is set to the current agent, callers should prefer asking
// the live session directly (ACPSession::requestSummary); this class then acts
// as the fallback and resolves the sentinel to the active provider.
class SummaryGenerator : public QObject
{
    Q_OBJECT

public:
    explicit SummaryGenerator(SettingsStore *settings, QObject *parent = nullptr);
    ~SummaryGenerator() override;

    void generateSummary(const QString &sessionId,
                         const QString &projectRoot,
                         const QString &transcriptContent);

    bool isGenerating() const { return !m_jobs.isEmpty(); }

    // Block until all pending jobs complete (for shutdown). The default is
    // generous because an ACP subprocess is slower to start than an HTTP call.
    void waitForPendingRequests(int timeoutMs = 60000);

    // Prompt asking a live session to summarise itself from its own context
    // (no transcript is attached; the agent already has the conversation).
    static QString buildInSessionPrompt(const QString &projectRoot);

Q_SIGNALS:
    void summaryReady(const QString &sessionId, const QString &projectRoot, const QString &summary);
    void summaryError(const QString &sessionId, const QString &error);

private:
    struct SummaryJob {
        QString sessionId;
        QString projectRoot;
        QString acpSessionId;
        QString prompt;
        QString collected;
        int initId = -1;
        int newId = -1;
        int promptId = -1;
    };

    static QString buildPrompt(const QString &projectRoot, const QString &transcriptContent);
    static QString truncateTranscript(const QString &transcript, int maxChars = 50000);
    static QString summaryStructure();

    void onServiceResponse(ACPService *svc, int id, const QJsonObject &result, const QJsonObject &error);
    void onServiceNotification(ACPService *svc, const QString &method, const QJsonObject &params, int requestId);
    // Remove the job, stop the subprocess and emit summaryError (or nothing
    // when error is empty). Safe to call twice for the same service.
    void failJob(ACPService *svc, const QString &error);

    SettingsStore *m_settings;
    QMap<ACPService *, SummaryJob> m_jobs;
};
