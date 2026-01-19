#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QSize>
#include <QString>

enum class ConnectionStatus {
    Disconnected,
    Connecting,
    Connected,
    Error
};

enum class TerminalStatus {
    Running,
    Exited,
    Killed,
    Released
};

struct EditDiff {
    QString oldText;  // Original text
    QString newText;  // New text
    QString filePath; // Optional file path for this specific edit
};

struct ToolCall {
    QString id;
    QString name;
    QJsonObject input;
    QString status;  // "pending", "running", "completed", "failed"
    QString result;
    QString filePath;  // File path if tool operates on a file
    int contentPosition = 0;

    // Edit/Write specific fields
    QString oldText;  // For Edit tool - original text (deprecated, use edits)
    QString newText;  // For Edit tool - new text (deprecated, use edits)
    QString operationType;  // "create", "edit", etc.
    QList<EditDiff> edits;  // Multiple edits for Edit tool

    // Terminal specific fields
    QString terminalId;  // For embedded terminal output
};

struct ImageAttachment {
    QString id;           // Unique identifier for removal
    QString mimeType;     // "image/png", "image/jpeg", "image/gif", "image/webp"
    QByteArray data;      // Raw image bytes
    QSize dimensions;     // Original dimensions for preview scaling
};

struct Message {
    QString id;
    QString role;  // "user", "assistant", "system"
    QString content;
    QDateTime timestamp;
    bool isStreaming = false;
    QList<ToolCall> toolCalls;
    QList<ImageAttachment> images;  // For user messages with image attachments
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

struct SlashCommand {
    QString name;
    QString description;
};

struct ContextChunk {
    QString filePath;
    int startLine;
    int endLine;
    QString content;
    QString id;  // Unique identifier for removal
};
