#include "TranscriptWriter.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>

TranscriptWriter::TranscriptWriter(QObject *parent)
    : QObject(parent)
{
}

TranscriptWriter::~TranscriptWriter()
{
    finishSession();
}

void TranscriptWriter::startSession(const QString &sessionId, const QString &projectRoot)
{
    if (m_file.isOpen()) {
        finishSession();
    }

    m_sessionId = sessionId;
    m_projectRoot = projectRoot;

    // Create transcripts directory with project subfolder
    QString transcriptDir = QDir::homePath() + QStringLiteral("/.kate-code/transcripts");
    QString projectFolder = projectPathToFolderName(projectRoot);
    QString projectDir = transcriptDir + QStringLiteral("/") + projectFolder;
    QDir dir(projectDir);
    if (!dir.exists()) {
        dir.mkpath(projectDir);
    }

    m_filePath = projectDir + QStringLiteral("/") + sessionId + QStringLiteral(".md");
    m_file.setFileName(m_filePath);

    // Check if we're resuming an existing session
    bool isResume = m_file.exists();

    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "[TranscriptWriter] Failed to open transcript file:" << m_filePath;
        return;
    }

    m_stream.setDevice(&m_file);

    if (!isResume) {
        // Write header for new session
        QString header = QStringLiteral("# Session Transcript\n\n");
        header += QStringLiteral("- **Session ID:** %1\n").arg(sessionId);
        header += QStringLiteral("- **Project:** %1\n").arg(projectRoot);
        header += QStringLiteral("- **Started:** %1\n\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
        header += QStringLiteral("---\n\n");
        appendToFile(header);
    } else {
        // Add resume marker
        QString resumeMarker = QStringLiteral("\n---\n\n");
        resumeMarker += QStringLiteral("*Session resumed at %1*\n\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
        resumeMarker += QStringLiteral("---\n\n");
        appendToFile(resumeMarker);
    }

    qDebug() << "[TranscriptWriter] Started transcript:" << m_filePath << (isResume ? "(resumed)" : "(new)");
}

void TranscriptWriter::finishSession()
{
    if (m_file.isOpen()) {
        QString footer = QStringLiteral("\n---\n\n");
        footer += QStringLiteral("*Session ended at %1*\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
        appendToFile(footer);

        m_stream.flush();
        m_file.close();
        qDebug() << "[TranscriptWriter] Finished transcript:" << m_filePath;
    }
    m_messageContent.clear();
}

void TranscriptWriter::recordMessage(const Message &msg)
{
    if (!m_file.isOpen()) {
        return;
    }

    if (msg.role == QStringLiteral("user")) {
        // User messages are written immediately (not streamed)
        appendToFile(formatMessage(msg));
    } else if (msg.role == QStringLiteral("assistant")) {
        if (msg.isStreaming) {
            // Start accumulating content
            m_messageContent[msg.id] = msg.content;
        } else {
            // Message is complete, write it
            appendToFile(formatMessage(msg));
        }
    }
}

void TranscriptWriter::recordToolCall(const ToolCall &toolCall)
{
    if (!m_file.isOpen()) {
        return;
    }

    appendToFile(formatToolCall(toolCall));
}

void TranscriptWriter::recordToolUpdate(const QString &toolId, const QString &status, const QString &result)
{
    Q_UNUSED(toolId);

    if (!m_file.isOpen()) {
        return;
    }

    // Only record completion with results
    if (status == QStringLiteral("completed") && !result.isEmpty()) {
        QString markdown;
        markdown += QStringLiteral("**Result:**\n```\n");
        markdown += result;
        if (!result.endsWith(QLatin1Char('\n'))) {
            markdown += QLatin1Char('\n');
        }
        markdown += QStringLiteral("```\n\n");
        appendToFile(markdown);
    } else if (status == QStringLiteral("failed")) {
        QString markdown = QStringLiteral("**Status:** Failed\n");
        if (!result.isEmpty()) {
            markdown += QStringLiteral("**Error:**\n```\n%1\n```\n").arg(result);
        }
        markdown += QLatin1Char('\n');
        appendToFile(markdown);
    }
}

void TranscriptWriter::appendToFile(const QString &markdown)
{
    if (m_file.isOpen()) {
        m_stream << markdown;
        m_stream.flush();
    }
}

QString TranscriptWriter::formatMessage(const Message &msg)
{
    QString markdown;
    QString timestamp = msg.timestamp.toString(QStringLiteral("hh:mm:ss"));

    if (msg.role == QStringLiteral("user")) {
        markdown += QStringLiteral("## User (%1)\n\n").arg(timestamp);
    } else if (msg.role == QStringLiteral("assistant")) {
        markdown += QStringLiteral("## Assistant (%1)\n\n").arg(timestamp);
    } else {
        markdown += QStringLiteral("## %1 (%2)\n\n").arg(msg.role, timestamp);
    }

    markdown += msg.content;
    if (!msg.content.endsWith(QLatin1Char('\n'))) {
        markdown += QLatin1Char('\n');
    }
    markdown += QLatin1Char('\n');

    return markdown;
}

QString TranscriptWriter::formatToolCall(const ToolCall &toolCall)
{
    QString markdown;
    markdown += QStringLiteral("### Tool: %1\n\n").arg(toolCall.name);

    if (toolCall.name == QStringLiteral("Edit")) {
        // File edits with diffs
        if (!toolCall.filePath.isEmpty()) {
            markdown += QStringLiteral("**File:** `%1`\n\n").arg(toolCall.filePath);
        }

        // Handle multiple edits
        if (!toolCall.edits.isEmpty()) {
            for (const EditDiff &edit : toolCall.edits) {
                if (!edit.filePath.isEmpty() && edit.filePath != toolCall.filePath) {
                    markdown += QStringLiteral("**File:** `%1`\n\n").arg(edit.filePath);
                }
                markdown += QStringLiteral("```diff\n");
                markdown += generateUnifiedDiff(edit.oldText, edit.newText);
                markdown += QStringLiteral("```\n\n");
            }
        } else if (!toolCall.oldText.isEmpty() || !toolCall.newText.isEmpty()) {
            // Legacy single edit
            markdown += QStringLiteral("```diff\n");
            markdown += generateUnifiedDiff(toolCall.oldText, toolCall.newText);
            markdown += QStringLiteral("```\n\n");
        }

    } else if (toolCall.name == QStringLiteral("Write")) {
        // File writes
        if (!toolCall.filePath.isEmpty()) {
            markdown += QStringLiteral("**File:** `%1`\n").arg(toolCall.filePath);
            markdown += QStringLiteral("**Operation:** %1\n\n").arg(
                toolCall.operationType.isEmpty() ? QStringLiteral("create") : toolCall.operationType);
        }
        if (!toolCall.newText.isEmpty()) {
            markdown += QStringLiteral("```\n%1\n```\n\n").arg(toolCall.newText);
        }

    } else if (toolCall.name == QStringLiteral("Bash")) {
        // Bash commands
        QString command = toolCall.input.value(QStringLiteral("command")).toString();
        if (!command.isEmpty()) {
            markdown += QStringLiteral("**Command:**\n```bash\n%1\n```\n\n").arg(command);
        }

    } else if (toolCall.name == QStringLiteral("Read")) {
        // File reads
        QString filePath = toolCall.input.value(QStringLiteral("file_path")).toString();
        if (!filePath.isEmpty()) {
            markdown += QStringLiteral("**File:** `%1`\n\n").arg(filePath);
        }

    } else {
        // Generic tool - dump input as JSON if present
        if (!toolCall.input.isEmpty()) {
            QJsonDocument doc(toolCall.input);
            markdown += QStringLiteral("**Input:**\n```json\n%1\n```\n\n")
                            .arg(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
        }
    }

    return markdown;
}

QString TranscriptWriter::generateUnifiedDiff(const QString &oldText, const QString &newText)
{
    // Simple unified diff generation
    QStringList oldLines = oldText.split(QLatin1Char('\n'));
    QStringList newLines = newText.split(QLatin1Char('\n'));

    QString diff;

    // Find common prefix
    int commonPrefix = 0;
    while (commonPrefix < oldLines.size() && commonPrefix < newLines.size() &&
           oldLines[commonPrefix] == newLines[commonPrefix]) {
        commonPrefix++;
    }

    // Find common suffix
    int commonSuffix = 0;
    while (commonSuffix < (oldLines.size() - commonPrefix) &&
           commonSuffix < (newLines.size() - commonPrefix) &&
           oldLines[oldLines.size() - 1 - commonSuffix] == newLines[newLines.size() - 1 - commonSuffix]) {
        commonSuffix++;
    }

    // Context lines before
    int contextStart = qMax(0, commonPrefix - 3);
    for (int i = contextStart; i < commonPrefix; i++) {
        diff += QStringLiteral(" %1\n").arg(oldLines[i]);
    }

    // Removed lines
    for (int i = commonPrefix; i < oldLines.size() - commonSuffix; i++) {
        diff += QStringLiteral("-%1\n").arg(oldLines[i]);
    }

    // Added lines
    for (int i = commonPrefix; i < newLines.size() - commonSuffix; i++) {
        diff += QStringLiteral("+%1\n").arg(newLines[i]);
    }

    // Context lines after
    int contextEnd = qMin(oldLines.size(), oldLines.size() - commonSuffix + 3);
    for (int i = oldLines.size() - commonSuffix; i < contextEnd; i++) {
        diff += QStringLiteral(" %1\n").arg(oldLines[i]);
    }

    return diff;
}

QString TranscriptWriter::escapeMarkdown(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    escaped.replace(QLatin1Char('*'), QStringLiteral("\\*"));
    escaped.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return escaped;
}

QString TranscriptWriter::projectPathToFolderName(const QString &projectRoot)
{
    if (projectRoot.isEmpty()) {
        return QStringLiteral("_unknown_");
    }
    QString folder = projectRoot;
    if (folder.startsWith(QLatin1Char('/'))) {
        folder = folder.mid(1);
    }
    folder.replace(QLatin1Char('/'), QLatin1Char('_'));
    return folder;
}
