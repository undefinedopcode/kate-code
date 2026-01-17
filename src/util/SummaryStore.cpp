#include "SummaryStore.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

SummaryStore::SummaryStore(QObject *parent)
    : QObject(parent)
{
}

void SummaryStore::saveSummary(const QString &projectRoot,
                                const QString &sessionId,
                                const QString &summary)
{
    QString dirPath = summaryDir(projectRoot);
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    QString filePath = summaryPath(projectRoot, sessionId);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << summary;
        file.close();
    }
}

QString SummaryStore::loadSummary(const QString &projectRoot,
                                   const QString &sessionId) const
{
    QString filePath = summaryPath(projectRoot, sessionId);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();
    return content;
}

bool SummaryStore::hasSummary(const QString &projectRoot,
                               const QString &sessionId) const
{
    return QFile::exists(summaryPath(projectRoot, sessionId));
}

QString SummaryStore::summaryPath(const QString &projectRoot,
                                   const QString &sessionId) const
{
    return summaryDir(projectRoot) + QStringLiteral("/") + sessionId + QStringLiteral(".md");
}

QStringList SummaryStore::listSessionSummaries(const QString &projectRoot) const
{
    QDir dir(summaryDir(projectRoot));
    if (!dir.exists()) {
        return QStringList();
    }

    QStringList filters;
    filters << QStringLiteral("*.md");

    QStringList files = dir.entryList(filters, QDir::Files, QDir::Time);

    // Remove .md extension to get session IDs
    QStringList sessionIds;
    for (const QString &file : files) {
        sessionIds << file.chopped(3); // Remove ".md"
    }
    return sessionIds;
}

QString SummaryStore::projectPathToFolderName(const QString &projectRoot) const
{
    // Convert path like /home/april/projects/kate-code
    // to folder name like home_april_projects_kate-code
    QString normalized = projectRoot;

    // Remove leading slash
    if (normalized.startsWith(QLatin1Char('/'))) {
        normalized = normalized.mid(1);
    }

    // Remove trailing slash
    if (normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }

    // Replace slashes with underscores
    normalized.replace(QLatin1Char('/'), QLatin1Char('_'));

    return normalized;
}

QString SummaryStore::summaryDir(const QString &projectRoot) const
{
    return baseDir() + QStringLiteral("/") + projectPathToFolderName(projectRoot);
}

QString SummaryStore::baseDir() const
{
    return QDir::homePath() + QStringLiteral("/.kate-code/summaries");
}
