#pragma once

#include "ACPModels.h"
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <optional>

class TerminalManager : public QObject
{
    Q_OBJECT

public:
    explicit TerminalManager(QObject *parent = nullptr);
    ~TerminalManager() override;

    // Create a new terminal and spawn the command
    // Returns empty string on failure
    QString createTerminal(const QString &command, const QStringList &args,
                          const QProcessEnvironment &env, const QString &cwd,
                          qint64 outputByteLimit = 0);

    // Query terminal output (non-blocking)
    struct OutputResult {
        QString output;
        bool truncated = false;
        std::optional<int> exitStatus;
    };
    OutputResult getOutput(const QString &terminalId) const;

    // Wait for terminal to exit (blocking with event processing)
    // Returns false if terminal not found or timeout
    struct WaitResult {
        QString output;
        bool truncated = false;
        int exitStatus = -1;
        bool success = false;
    };
    WaitResult waitForExit(const QString &terminalId, int timeoutMs = -1);

    // Kill the terminal process (keeps terminal valid for output queries)
    bool killTerminal(const QString &terminalId);

    // Release terminal (kill if running, invalidate)
    bool releaseTerminal(const QString &terminalId);

    // Check if terminal exists and is valid
    bool isValid(const QString &terminalId) const;

    // Release all terminals (called on session end)
    void releaseAll();

Q_SIGNALS:
    // Emitted when new output is available (for live UI updates)
    void outputAvailable(const QString &terminalId, const QString &output, bool finished);

    // Emitted when terminal exits
    void terminalExited(const QString &terminalId, int exitCode);

private Q_SLOTS:
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    QString generateTerminalId();
    void truncateOutputIfNeeded(const QString &terminalId);

    struct TerminalData {
        QProcess *process = nullptr;
        QByteArray outputBuffer;
        TerminalStatus status = TerminalStatus::Running;
        int exitCode = -1;
        qint64 outputByteLimit = 0;
        bool truncated = false;
        QString command;  // For debugging/display
    };

    QHash<QString, TerminalData> m_terminals;
    int m_idCounter = 0;
};
