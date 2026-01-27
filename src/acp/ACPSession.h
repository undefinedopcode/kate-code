#pragma once

#include "ACPModels.h"
#include <QJsonArray>
#include <QObject>
#include <QString>
#include <functional>

namespace KTextEditor {
class Document;
}

class ACPService;
class EditTracker;
class TerminalManager;
class TranscriptWriter;

class ACPSession : public QObject
{
    Q_OBJECT

public:
    explicit ACPSession(QObject *parent = nullptr);
    ~ACPSession() override;

    void setExecutable(const QString &executable, const QStringList &args = QStringList());
    void start(const QString &workingDir, const QString &permissionMode = QStringLiteral("default"));
    void stop();

    // Session management - called after initializeComplete signal
    void createNewSession();
    void loadSession(const QString &sessionId);

    void sendMessage(const QString &content, const QString &filePath = QString(), const QString &selection = QString(), const QList<ContextChunk> &contextChunks = QList<ContextChunk>(), const QList<ImageAttachment> &images = QList<ImageAttachment>());
    void sendPermissionResponse(int requestId, const QJsonObject &outcome);
    void setMode(const QString &modeId);

    bool isConnected() const { return m_status == ConnectionStatus::Connected; }
    bool isPromptRunning() const { return m_promptRequestId >= 0; }
    ConnectionStatus status() const { return m_status; }
    QString sessionId() const { return m_sessionId; }

    void cancelPrompt();
    QJsonArray availableModes() const { return m_availableModes; }
    QString currentMode() const { return m_currentMode; }
    QList<SlashCommand> availableCommands() const { return m_availableCommands; }

    // Set terminal size based on view width (columns calculated from pixel width)
    void setTerminalSize(int columns, int rows = 40);

    // Document provider for Kate integration (reads/writes use Kate documents when open)
    using DocumentProvider = std::function<KTextEditor::Document*(const QString &path)>;
    void setDocumentProvider(DocumentProvider provider);

    // Edit tracker for tracking file modifications
    EditTracker *editTracker() const { return m_editTracker; }

Q_SIGNALS:
    void statusChanged(ConnectionStatus status);
    void messageAdded(const Message &message);
    void messageUpdated(const QString &messageId, const QString &content);
    void messageFinished(const QString &messageId);
    void toolCallAdded(const QString &messageId, const ToolCall &toolCall);
    void toolCallUpdated(const QString &messageId, const QString &toolCallId, const QString &status, const QString &result);
    void todosUpdated(const QList<TodoItem> &todos);
    void permissionRequested(const PermissionRequest &request);
    void modesAvailable(const QJsonArray &modes);
    void modeChanged(const QString &modeId);
    void commandsAvailable(const QList<SlashCommand> &commands);
    void errorOccurred(const QString &message);
    void promptCancelled();

    // Emitted after initialize completes, before session creation
    void initializeComplete();

    // Emitted if session/load fails (caller should fall back to createNewSession)
    void sessionLoadFailed(const QString &error);

    // Terminal signals for UI updates
    void terminalOutputUpdated(const QString &terminalId, const QString &output, bool finished);

private Q_SLOTS:
    void onConnected();
    void onDisconnected(int exitCode);
    void onNotification(const QString &method, const QJsonObject &params, int requestId);
    void onResponse(int id, const QJsonObject &result, const QJsonObject &error);
    void onError(const QString &message);

private:
    void handleInitializeResponse(int id, const QJsonObject &result);
    void handleSessionNewResponse(int id, const QJsonObject &result);
    void handleSessionLoadResponse(int id, const QJsonObject &result, const QJsonObject &error);
    void handleSessionUpdate(const QJsonObject &params);
    void handlePermissionRequest(const QJsonObject &params, int requestId);

    // Terminal request handlers
    void handleTerminalCreate(const QJsonObject &params, int requestId);
    void handleTerminalOutput(const QJsonObject &params, int requestId);
    void handleTerminalWaitForExit(const QJsonObject &params, int requestId);
    void handleTerminalKill(const QJsonObject &params, int requestId);
    void handleTerminalRelease(const QJsonObject &params, int requestId);

    // Filesystem request handlers
    void handleFsReadTextFile(const QJsonObject &params, int requestId);
    void handleFsWriteTextFile(const QJsonObject &params, int requestId);

    ACPService *m_service;
    TerminalManager *m_terminalManager;
    TranscriptWriter *m_transcript;
    ConnectionStatus m_status;
    QString m_sessionId;
    QString m_workingDir;
    QString m_currentMode;
    QJsonArray m_availableModes;
    QList<SlashCommand> m_availableCommands;
    QString m_currentMessageId;
    QString m_currentMessageContent;  // Accumulated content for transcript
    QDateTime m_currentMessageTimestamp;
    QHash<QString, QJsonObject> m_toolCallInputs;  // Track tool inputs by toolCallId

    // Request tracking
    int m_initializeRequestId;
    int m_sessionNewRequestId;
    int m_sessionLoadRequestId;
    int m_promptRequestId;

    // Message counter
    int m_messageCounter;

    // Kate document provider
    DocumentProvider m_documentProvider;

    // Edit tracker
    EditTracker *m_editTracker;

    // Current tool call ID for edit tracking
    QString m_currentToolCallId;
};