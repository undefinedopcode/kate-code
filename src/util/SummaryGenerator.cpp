#include "SummaryGenerator.h"
#include "../acp/ACPService.h"
#include "../config/SettingsStore.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

// Hard ceiling per headless job so a silent or wedged agent cannot leak a
// subprocess per session end (and stall shutdown).
static const int SUMMARY_JOB_TIMEOUT_MS = 120000;

SummaryGenerator::SummaryGenerator(SettingsStore *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

SummaryGenerator::~SummaryGenerator()
{
    // Silently stop any running agents; children are deleted with this object.
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        disconnect(it.key(), nullptr, this, nullptr);
        it.key()->stop();
    }
    m_jobs.clear();
}

void SummaryGenerator::waitForPendingRequests(int timeoutMs)
{
    if (m_jobs.isEmpty()) {
        return;
    }

    qDebug() << "[SummaryGenerator] Waiting for" << m_jobs.size() << "pending job(s)...";

    QElapsedTimer timer;
    timer.start();

    // Guard against this generator being destroyed by a slot that runs while
    // events are pumped (e.g. the owning widget closing).
    QPointer<SummaryGenerator> self(this);
    QEventLoop loop;
    while (self && !m_jobs.isEmpty() && timer.elapsed() < timeoutMs) {
        loop.processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 100);
    }
    if (!self) {
        return;
    }

    if (!m_jobs.isEmpty()) {
        qWarning() << "[SummaryGenerator] Timeout waiting for jobs, aborting" << m_jobs.size() << "remaining";
        // failJob() mutates the map, so drain from a copy of the keys.
        const QList<ACPService *> remaining = m_jobs.keys();
        for (ACPService *svc : remaining) {
            failJob(svc, QStringLiteral("Timed out waiting for the summariser agent"));
        }
    } else {
        qDebug() << "[SummaryGenerator] All pending jobs completed";
    }
}

void SummaryGenerator::generateSummary(const QString &sessionId,
                                       const QString &projectRoot,
                                       const QString &transcriptContent)
{
    qDebug() << "[SummaryGenerator] generateSummary called for session:" << sessionId;

    if (transcriptContent.isEmpty()) {
        Q_EMIT summaryError(sessionId, QStringLiteral("No transcript content to summarise"));
        return;
    }

    // The current-agent sentinel reaches this headless path when the live
    // session is unavailable; run the active provider instead. A stale id
    // (e.g. the chosen provider was deleted) also falls back to the active
    // provider so summaries keep working.
    QString providerId = m_settings->summaryProviderId();
    ACPProvider provider = m_settings->providerById(providerId);
    if (providerId == SettingsStore::CURRENT_AGENT_PROVIDER_ID || provider.executable.isEmpty()) {
        if (provider.executable.isEmpty() && providerId != SettingsStore::CURRENT_AGENT_PROVIDER_ID) {
            qWarning() << "[SummaryGenerator] Summary provider" << providerId
                       << "not found; falling back to the active provider";
        }
        providerId = m_settings->activeProviderId();
        provider = m_settings->providerById(providerId);
    }
    if (provider.executable.isEmpty()) {
        Q_EMIT summaryError(sessionId,
                            QStringLiteral("No summariser agent configured (provider \"%1\" not found)")
                                .arg(providerId));
        return;
    }

    auto *svc = new ACPService(this);
    svc->setExecutable(provider.executable, QProcess::splitCommand(provider.options));

    SummaryJob job;
    job.sessionId = sessionId;
    job.projectRoot = projectRoot;
    job.prompt = buildPrompt(projectRoot, transcriptContent);
    m_jobs.insert(svc, job);

    connect(svc, &ACPService::connected, this, [this, svc]() {
        auto it = m_jobs.find(svc);
        if (it == m_jobs.end()) {
            return;
        }
        // Minimal client capabilities: the summariser offers no editor access.
        QJsonObject fs;
        fs[QStringLiteral("readTextFile")] = false;
        fs[QStringLiteral("writeTextFile")] = false;
        QJsonObject capabilities;
        capabilities[QStringLiteral("fs")] = fs;
        QJsonObject params;
        params[QStringLiteral("protocolVersion")] = 1;
        params[QStringLiteral("clientCapabilities")] = capabilities;
        it->initId = svc->sendRequest(QStringLiteral("initialize"), params);
        if (it->initId < 0) {
            failJob(svc, QStringLiteral("Failed to send initialize to the summariser agent"));
        }
    });
    connect(svc, &ACPService::responseReceived, this,
            [this, svc](int id, const QJsonObject &result, const QJsonObject &error) {
        onServiceResponse(svc, id, result, error);
    });
    connect(svc, &ACPService::notificationReceived, this,
            [this, svc](const QString &method, const QJsonObject &params, int requestId) {
        onServiceNotification(svc, method, params, requestId);
    });
    connect(svc, &ACPService::errorOccurred, this, [this, svc](const QString &message) {
        failJob(svc, message);
    });
    connect(svc, &ACPService::disconnected, this, [this, svc](int exitCode) {
        failJob(svc, QStringLiteral("Summariser agent exited before replying (exit code %1)").arg(exitCode));
    });

    // Hard per-job timeout; the timer dies with svc, so a finished job never
    // sees a late timeout (failJob is also a no-op once the job is gone).
    QTimer::singleShot(SUMMARY_JOB_TIMEOUT_MS, svc, [this, svc]() {
        failJob(svc, QStringLiteral("Summariser agent timed out"));
    });

    qDebug() << "[SummaryGenerator] Starting summariser agent:" << provider.executable
             << "in" << projectRoot;
    // start() reports failure through errorOccurred, which failJob() handles.
    svc->start(projectRoot);
}

void SummaryGenerator::onServiceResponse(ACPService *svc, int id,
                                         const QJsonObject &result, const QJsonObject &error)
{
    auto it = m_jobs.find(svc);
    if (it == m_jobs.end()) {
        return;
    }
    SummaryJob &job = it.value();

    if (!error.isEmpty()) {
        failJob(svc, error[QStringLiteral("message")].toString(QStringLiteral("agent error")));
        return;
    }

    if (id == job.initId) {
        QJsonObject params;
        params[QStringLiteral("cwd")] = job.projectRoot;
        // Deliberately no MCP servers: the summariser needs no Kate tools.
        params[QStringLiteral("mcpServers")] = QJsonArray();
        job.newId = svc->sendRequest(QStringLiteral("session/new"), params);
        if (job.newId < 0) {
            failJob(svc, QStringLiteral("Failed to send session/new to the summariser agent"));
        }
    } else if (id == job.newId) {
        job.acpSessionId = result[QStringLiteral("sessionId")].toString();
        if (job.acpSessionId.isEmpty()) {
            failJob(svc, QStringLiteral("Summariser agent returned no session id"));
            return;
        }
        QJsonObject textBlock;
        textBlock[QStringLiteral("type")] = QStringLiteral("text");
        textBlock[QStringLiteral("text")] = job.prompt;
        QJsonArray promptBlocks;
        promptBlocks.append(textBlock);
        QJsonObject params;
        params[QStringLiteral("sessionId")] = job.acpSessionId;
        params[QStringLiteral("prompt")] = promptBlocks;
        job.promptId = svc->sendRequest(QStringLiteral("session/prompt"), params);
        if (job.promptId < 0) {
            failJob(svc, QStringLiteral("Failed to send session/prompt to the summariser agent"));
        }
    } else if (id == job.promptId) {
        const QString summary = job.collected.trimmed();
        const QString sessionId = job.sessionId;
        const QString projectRoot = job.projectRoot;
        m_jobs.erase(it);
        svc->stop();
        svc->deleteLater();
        if (summary.isEmpty()) {
            Q_EMIT summaryError(sessionId, QStringLiteral("Summariser agent returned an empty summary"));
        } else {
            qDebug() << "[SummaryGenerator] Summary generated for session:" << sessionId;
            Q_EMIT summaryReady(sessionId, projectRoot, summary);
        }
    }
}

void SummaryGenerator::onServiceNotification(ACPService *svc, const QString &method,
                                             const QJsonObject &params, int requestId)
{
    auto it = m_jobs.find(svc);
    if (it == m_jobs.end()) {
        return;
    }

    if (method == QStringLiteral("session/update")) {
        const QJsonObject update = params[QStringLiteral("update")].toObject();
        if (update[QStringLiteral("sessionUpdate")].toString() == QStringLiteral("agent_message_chunk")) {
            it->collected += update[QStringLiteral("content")].toObject()
                                   [QStringLiteral("text")].toString();
        }
        // Thought chunks, tool calls and other update types are ignored.
    } else if (method == QStringLiteral("session/request_permission") && requestId >= 0) {
        // The summariser should not run tools; decline so the agent finishes
        // its turn instead of hanging until our timeout.
        QJsonObject outcome;
        outcome[QStringLiteral("outcome")] = QStringLiteral("cancelled");
        QJsonObject result;
        result[QStringLiteral("outcome")] = outcome;
        svc->sendResponse(requestId, result);
        qDebug() << "[SummaryGenerator] Declined a tool permission request from the summariser agent";
    }
}

void SummaryGenerator::failJob(ACPService *svc, const QString &error)
{
    auto it = m_jobs.find(svc);
    if (it == m_jobs.end()) {
        return;  // Job already finished; late signals are expected after stop().
    }
    const QString sessionId = it->sessionId;
    m_jobs.erase(it);
    svc->stop();
    svc->deleteLater();
    qWarning() << "[SummaryGenerator] Summary job failed for" << sessionId << ":" << error;
    Q_EMIT summaryError(sessionId, error);
}

QString SummaryGenerator::summaryStructure()
{
    return QStringLiteral(
        "Create a markdown summary with this EXACT structure:\n\n"
        "# [Descriptive Thematic Title]\n\n"
        "The title MUST be a specific, descriptive phrase that captures the main accomplishment or focus "
        "of the session (e.g., \"Implementing OAuth2 Authentication\", \"Debugging Memory Leak in Parser\", "
        "\"Refactoring Database Layer\"). NEVER use generic titles like \"Summary\", \"Session Summary\", "
        "or \"Coding Session\".\n\n"
        "## Overview\n"
        "A brief 1-2 sentence description categorizing the session type (feature implementation, "
        "bug fix, refactoring, debugging, configuration, etc.) and summarizing what was accomplished.\n\n"
        "## Tasks Completed\n"
        "- Bullet list of what was accomplished\n"
        "- Focus on outcomes, not process\n\n"
        "## Files Modified\n"
        "- List files that were created, modified, or deleted\n"
        "- Group by directory if many files\n\n"
        "## Key Decisions\n"
        "- Important architectural or design decisions made\n"
        "- Trade-offs considered\n"
        "- Omit this section if no significant decisions were made\n\n"
        "## Problems & Blockers\n"
        "- Errors encountered and how they were resolved\n"
        "- Unresolved issues or blockers\n"
        "- Failed approaches that were abandoned\n"
        "- Omit this section if none\n\n"
        "## Commands & Tools\n"
        "- Key build/test/deploy commands used\n"
        "- External tools or services involved\n"
        "- Omit this section if only standard editing occurred\n\n"
        "## Next Steps\n"
        "- Unfinished work or suggested follow-up tasks\n"
        "- Known issues to address\n\n"
        "Guidelines:\n"
        "- Keep the summary concise but informative - it will be used as context when resuming later\n"
        "- Prioritize information that would help someone continue this work\n"
        "- Omit sections that have no relevant content rather than writing \"None\"\n");
}

QString SummaryGenerator::buildPrompt(const QString &projectRoot, const QString &transcriptContent)
{
    QString truncated = truncateTranscript(transcriptContent);

    // Extract project name from path
    QString projectName = projectRoot;
    int lastSlash = projectRoot.lastIndexOf(QLatin1Char('/'));
    if (lastSlash >= 0) {
        projectName = projectRoot.mid(lastSlash + 1);
    }

    return QStringLiteral(
        "Summarize this coding session transcript for the project \"%1\" (at %2).\n\n"
        "%3\n"
        "- If the transcript was truncated, focus on the final state and outcomes over intermediate attempts\n"
        "- Reply with ONLY the markdown summary; do not use any tools and do not ask questions\n\n"
        "---\n\n"
        "Transcript:\n%4"
    ).arg(projectName, projectRoot, summaryStructure(), truncated);
}

QString SummaryGenerator::buildInSessionPrompt(const QString &projectRoot)
{
    return QStringLiteral(
        "This session is ending. Summarize the whole session (for the project at %1) so the "
        "summary can be injected as context if the session is resumed later.\n\n"
        "%2\n"
        "- Reply with ONLY the markdown summary; do not use any tools and do not ask questions\n"
    ).arg(projectRoot, summaryStructure());
}

QString SummaryGenerator::truncateTranscript(const QString &transcript, int maxChars)
{
    if (transcript.length() <= maxChars) {
        return transcript;
    }

    // Keep the beginning (context) and end (recent work)
    int halfMax = maxChars / 2;
    QString beginning = transcript.left(halfMax);
    QString end = transcript.right(halfMax);

    return beginning +
           QStringLiteral("\n\n... [transcript truncated for length] ...\n\n") +
           end;
}
