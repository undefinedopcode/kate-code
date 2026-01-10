#pragma once

#include "ACPModels.h"
#include <QJsonArray>
#include <QObject>
#include <QString>

class ACPService;

class ACPSession : public QObject
{
    Q_OBJECT

public:
    explicit ACPSession(QObject *parent = nullptr);
    ~ACPSession() override;

    void start(const QString &workingDir, const QString &permissionMode = QStringLiteral("default"));
    void stop();

    void sendMessage(const QString &content, const QString &filePath = QString(), const QString &selection = QString());
    void sendPermissionResponse(int requestId, const QJsonObject &outcome);
    void setMode(const QString &modeId);

    bool isConnected() const { return m_status == ConnectionStatus::Connected; }
    ConnectionStatus status() const { return m_status; }
    QString sessionId() const { return m_sessionId; }
    QJsonArray availableModes() const { return m_availableModes; }
    QString currentMode() const { return m_currentMode; }
    QList<SlashCommand> availableCommands() const { return m_availableCommands; }

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

private Q_SLOTS:
    void onConnected();
    void onDisconnected(int exitCode);
    void onNotification(const QString &method, const QJsonObject &params, int requestId);
    void onResponse(int id, const QJsonObject &result, const QJsonObject &error);
    void onError(const QString &message);

private:
    void handleInitializeResponse(int id, const QJsonObject &result);
    void handleSessionNewResponse(int id, const QJsonObject &result);
    void handleSessionUpdate(const QJsonObject &params);
    void handlePermissionRequest(const QJsonObject &params, int requestId);

    ACPService *m_service;
    ConnectionStatus m_status;
    QString m_sessionId;
    QString m_workingDir;
    QString m_currentMode;
    QJsonArray m_availableModes;
    QList<SlashCommand> m_availableCommands;
    QString m_currentMessageId;

    // Request tracking
    int m_initializeRequestId;
    int m_sessionNewRequestId;
    int m_promptRequestId;

    // Message counter
    int m_messageCounter;
};
