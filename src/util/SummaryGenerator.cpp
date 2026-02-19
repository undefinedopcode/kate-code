#include "SummaryGenerator.h"
#include "../config/SettingsStore.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

SummaryGenerator::SummaryGenerator(SettingsStore *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &SummaryGenerator::onNetworkReply);
}

SummaryGenerator::~SummaryGenerator()
{
    // Cancel pending requests
    for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end(); ++it) {
        it.key()->abort();
    }
}

void SummaryGenerator::waitForPendingRequests(int timeoutMs)
{
    if (m_pendingRequests.isEmpty()) {
        return;
    }

    qDebug() << "[SummaryGenerator] Waiting for" << m_pendingRequests.size() << "pending request(s)...";

    QElapsedTimer timer;
    timer.start();

    QEventLoop loop;
    while (!m_pendingRequests.isEmpty() && timer.elapsed() < timeoutMs) {
        loop.processEvents(QEventLoop::AllEvents, 100);
    }

    if (!m_pendingRequests.isEmpty()) {
        qWarning() << "[SummaryGenerator] Timeout waiting for requests, aborting" << m_pendingRequests.size() << "remaining";
        for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end(); ++it) {
            it.key()->abort();
        }
        m_pendingRequests.clear();
    } else {
        qDebug() << "[SummaryGenerator] All pending requests completed";
    }
}

void SummaryGenerator::generateSummary(const QString &sessionId,
                                         const QString &projectRoot,
                                         const QString &transcriptContent)
{
    qDebug() << "[SummaryGenerator] generateSummary called for session:" << sessionId;
    QString apiKey = m_settings->apiKey();
    if (apiKey.isEmpty()) {
        qDebug() << "[SummaryGenerator] No API key configured";
        Q_EMIT summaryError(sessionId, QStringLiteral("No API key configured"));
        return;
    }

    if (transcriptContent.isEmpty()) {
        qDebug() << "[SummaryGenerator] No transcript content";
        Q_EMIT summaryError(sessionId, QStringLiteral("No transcript content to summarize"));
        return;
    }

    qDebug() << "[SummaryGenerator] Making API request to Anthropic...";

    QUrl url(QStringLiteral("https://api.anthropic.com/v1/messages"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("x-api-key", apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setRawHeader("anthropic-dangerous-direct-browser-access", "true");

    // Set timeout
    request.setTransferTimeout(30000); // 30 seconds

    QJsonObject body;
    body[QStringLiteral("model")] = m_settings->summaryModel();
    body[QStringLiteral("max_tokens")] = 2048;

    QJsonArray messages;
    QJsonObject userMsg;
    userMsg[QStringLiteral("role")] = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = buildPrompt(projectRoot, transcriptContent);
    messages.append(userMsg);
    body[QStringLiteral("messages")] = messages;

    QNetworkReply *reply = m_networkManager->post(request, QJsonDocument(body).toJson());

    PendingRequest pending;
    pending.sessionId = sessionId;
    pending.projectRoot = projectRoot;
    m_pendingRequests[reply] = pending;
}

void SummaryGenerator::onNetworkReply(QNetworkReply *reply)
{
    qDebug() << "[SummaryGenerator] Network reply received";
    reply->deleteLater();

    if (!m_pendingRequests.contains(reply)) {
        qDebug() << "[SummaryGenerator] Unknown reply, ignoring";
        return;
    }

    PendingRequest pending = m_pendingRequests.take(reply);

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "[SummaryGenerator] Network error:" << reply->errorString();
        Q_EMIT summaryError(pending.sessionId,
                           QStringLiteral("Network error: %1").arg(reply->errorString()));
        return;
    }
    qDebug() << "[SummaryGenerator] HTTP status:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isNull() || !doc.isObject()) {
        Q_EMIT summaryError(pending.sessionId, QStringLiteral("Invalid JSON response from API"));
        return;
    }

    QJsonObject root = doc.object();

    // Check for API error
    if (root.contains(QStringLiteral("error"))) {
        QJsonObject error = root[QStringLiteral("error")].toObject();
        QString message = error[QStringLiteral("message")].toString();
        Q_EMIT summaryError(pending.sessionId, QStringLiteral("API error: %1").arg(message));
        return;
    }

    // Extract content from response
    QJsonArray content = root[QStringLiteral("content")].toArray();
    if (content.isEmpty()) {
        Q_EMIT summaryError(pending.sessionId, QStringLiteral("Empty response from API"));
        return;
    }

    QString summary;
    for (const QJsonValue &block : content) {
        if (block.toObject()[QStringLiteral("type")].toString() == QStringLiteral("text")) {
            summary += block.toObject()[QStringLiteral("text")].toString();
        }
    }

    if (summary.isEmpty()) {
        Q_EMIT summaryError(pending.sessionId, QStringLiteral("No text content in API response"));
        return;
    }

    Q_EMIT summaryReady(pending.sessionId, pending.projectRoot, summary);
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
        "- If the transcript was truncated, focus on the final state and outcomes over intermediate attempts\n"
        "- Prioritize information that would help someone continue this work\n"
        "- Omit sections that have no relevant content rather than writing \"None\"\n\n"
        "---\n\n"
        "Transcript:\n%3"
    ).arg(projectName, projectRoot, truncated);
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
