#pragma once

#include "../acp/ACPModels.h"
#include <QFile>
#include <QObject>
#include <QString>
#include <QTextStream>

class TranscriptWriter : public QObject
{
    Q_OBJECT

public:
    explicit TranscriptWriter(QObject *parent = nullptr);
    ~TranscriptWriter() override;

    void startSession(const QString &sessionId, const QString &projectRoot);
    void finishSession();

    void recordMessage(const Message &msg);
    void recordToolCall(const ToolCall &toolCall);
    void recordToolUpdate(const QString &toolId, const QString &status, const QString &result);

    QString transcriptPath() const { return m_filePath; }
    bool isActive() const { return m_file.isOpen(); }

private:
    void appendToFile(const QString &markdown);
    QString formatMessage(const Message &msg);
    QString formatToolCall(const ToolCall &toolCall);
    QString generateUnifiedDiff(const QString &oldText, const QString &newText);
    QString escapeMarkdown(const QString &text);
    QString projectPathToFolderName(const QString &projectRoot);

    QString m_sessionId;
    QString m_projectRoot;
    QString m_filePath;
    QFile m_file;
    QTextStream m_stream;

    // Track accumulated message content for streaming
    QMap<QString, QString> m_messageContent;
};
