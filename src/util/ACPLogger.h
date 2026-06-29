#pragma once

#include <QFile>
#include <QObject>
#include <QString>
#include <QTextStream>

/**
 * ACPLogger - Appends raw ACP JSON-RPC traffic to a per-session file.
 *
 * This is independent of the on-screen chat: it taps the same payload stream
 * that powers the optional Output-panel debug log, but writes it to disk so an
 * interrupted session leaves a complete record. Each line is a JSON object and
 * is flushed immediately, so the file stays current even after a crash.
 */
class ACPLogger : public QObject
{
    Q_OBJECT

public:
    explicit ACPLogger(QObject *parent = nullptr);
    ~ACPLogger() override;

    // Update logging configuration (typically from SettingsStore).
    void configure(bool enabled, const QString &baseDir, const QString &subDir);

    // Append a single ACP payload. Opens a new file lazily on first use.
    void logPayload(const QString &direction, const QString &json);

    // Close the current file so the next session starts a fresh one.
    void endSession();

private:
    void openNewFile();

    bool m_enabled = false;
    QString m_baseDir;
    QString m_subDir;
    QFile m_file;
    QTextStream m_stream;
};
