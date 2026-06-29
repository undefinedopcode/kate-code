#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class SummaryStore : public QObject
{
    Q_OBJECT

public:
    explicit SummaryStore(QObject *parent = nullptr);
    ~SummaryStore() override = default;

    // Save summary for a session
    void saveSummary(const QString &projectRoot,
                     const QString &sessionId,
                     const QString &summary);

    // Load summary for session resumption context
    QString loadSummary(const QString &projectRoot,
                        const QString &sessionId) const;

    // Check if summary exists
    bool hasSummary(const QString &projectRoot,
                    const QString &sessionId) const;

    // Get path to summary file
    QString summaryPath(const QString &projectRoot,
                        const QString &sessionId) const;

    // List all sessions with summaries for a project
    QStringList listSessionSummaries(const QString &projectRoot) const;

    // Transcript helpers (transcripts are always written, even without a summary)
    // List all sessions that have a transcript on disk, most recent first.
    QStringList listTranscriptSessions(const QString &projectRoot) const;
    // Path to a session's transcript file.
    QString transcriptPath(const QString &projectRoot, const QString &sessionId) const;
    // Load a session's transcript content (empty if none).
    QString loadTranscript(const QString &projectRoot, const QString &sessionId) const;

private:
    QString projectPathToFolderName(const QString &projectRoot) const;
    QString summaryDir(const QString &projectRoot) const;
    QString transcriptDir(const QString &projectRoot) const;
    QString baseDir() const;
};
