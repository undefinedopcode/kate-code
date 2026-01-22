#include "EditTracker.h"

#include <QDebug>

EditTracker::EditTracker(QObject *parent)
    : QObject(parent)
{
}

void EditTracker::recordEdit(const QString &toolCallId, const QString &filePath,
                              int startLine, int oldLineCount, int newLineCount)
{
    TrackedEdit edit;
    edit.toolCallId = toolCallId;
    edit.filePath = filePath;
    edit.startLine = startLine;
    edit.oldLineCount = oldLineCount;
    edit.newLineCount = newLineCount;
    edit.isNewFile = false;
    edit.timestamp = QDateTime::currentDateTime();

    m_edits.append(edit);

    qDebug() << "[EditTracker] Recorded edit:" << filePath
             << "L" << startLine + 1 << "+" << newLineCount << "/-" << oldLineCount;

    Q_EMIT editRecorded(edit);
}

void EditTracker::recordNewFile(const QString &toolCallId, const QString &filePath, int lineCount)
{
    TrackedEdit edit;
    edit.toolCallId = toolCallId;
    edit.filePath = filePath;
    edit.startLine = 0;
    edit.oldLineCount = 0;
    edit.newLineCount = lineCount;
    edit.isNewFile = true;
    edit.timestamp = QDateTime::currentDateTime();

    m_edits.append(edit);

    qDebug() << "[EditTracker] Recorded new file:" << filePath << "with" << lineCount << "lines";

    Q_EMIT editRecorded(edit);
}

QList<TrackedEdit> EditTracker::getEditsForFile(const QString &filePath) const
{
    QList<TrackedEdit> result;
    for (const TrackedEdit &edit : m_edits) {
        if (edit.filePath == filePath) {
            result.append(edit);
        }
    }
    return result;
}

void EditTracker::clear()
{
    m_edits.clear();
    qDebug() << "[EditTracker] Cleared all edits";
    Q_EMIT editsCleared();
}
