#pragma once

#include "../acp/ACPModels.h"
#include <QObject>

class EditTracker : public QObject
{
    Q_OBJECT

public:
    explicit EditTracker(QObject *parent = nullptr);
    ~EditTracker() override = default;

    // Record an edit operation
    void recordEdit(const QString &toolCallId, const QString &filePath,
                    int startLine, int oldLineCount, int newLineCount);

    // Record a new file creation
    void recordNewFile(const QString &toolCallId, const QString &filePath, int lineCount);

    // Get all tracked edits
    QList<TrackedEdit> getEdits() const { return m_edits; }

    // Get edits for a specific file
    QList<TrackedEdit> getEditsForFile(const QString &filePath) const;

    // Clear all tracked edits
    void clear();

Q_SIGNALS:
    // Emitted when a new edit is recorded
    void editRecorded(const TrackedEdit &edit);

    // Emitted when edits are cleared
    void editsCleared();

private:
    QList<TrackedEdit> m_edits;
};
