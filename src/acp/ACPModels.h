#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

enum class ConnectionStatus {
    Disconnected,
    Connecting,
    Connected,
    Error
};

struct ToolCall {
    QString id;
    QString name;
    QJsonObject input;
    QString status;  // "pending", "running", "completed", "failed"
    QString result;
    QString filePath;  // File path if tool operates on a file
    int contentPosition = 0;
};

struct Message {
    QString id;
    QString role;  // "user", "assistant", "system"
    QString content;
    QDateTime timestamp;
    bool isStreaming = false;
    QList<ToolCall> toolCalls;
};

struct TodoItem {
    QString content;
    QString status;  // "pending", "in_progress", "completed"
    QString activeForm;
};

struct PermissionRequest {
    QString id;
    int requestId;
    QString toolName;
    QJsonObject input;
    QList<QJsonObject> options;
    QString sessionId;
};
