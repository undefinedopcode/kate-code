#include "ACPSession.h"
#include "ACPService.h"

#include <QDebug>
#include <QJsonArray>
#include <QUrl>
#include <QUuid>

ACPSession::ACPSession(QObject *parent)
    : QObject(parent)
    , m_service(new ACPService(this))
    , m_status(ConnectionStatus::Disconnected)
    , m_initializeRequestId(-1)
    , m_sessionNewRequestId(-1)
    , m_promptRequestId(-1)
    , m_messageCounter(0)
{
    connect(m_service, &ACPService::connected, this, &ACPSession::onConnected);
    connect(m_service, &ACPService::disconnected, this, &ACPSession::onDisconnected);
    connect(m_service, &ACPService::notificationReceived, this, &ACPSession::onNotification);
    connect(m_service, &ACPService::responseReceived, this, &ACPSession::onResponse);
    connect(m_service, &ACPService::errorOccurred, this, &ACPSession::onError);
}

ACPSession::~ACPSession()
{
    stop();
}

void ACPSession::start(const QString &workingDir, const QString &permissionMode)
{
    Q_UNUSED(permissionMode);  // Modes are now discovered from agent

    if (m_status != ConnectionStatus::Disconnected) {
        return;
    }

    m_workingDir = workingDir;
    m_status = ConnectionStatus::Connecting;
    Q_EMIT statusChanged(m_status);

    if (!m_service->start(workingDir)) {
        m_status = ConnectionStatus::Error;
        Q_EMIT statusChanged(m_status);
        Q_EMIT errorOccurred(QStringLiteral("Failed to start ACP service"));
    }
}

void ACPSession::stop()
{
    m_service->stop();
    m_status = ConnectionStatus::Disconnected;
    m_sessionId.clear();
    Q_EMIT statusChanged(m_status);
}

void ACPSession::sendPermissionResponse(int requestId, const QJsonObject &outcome)
{
    QJsonObject result;
    result[QStringLiteral("outcome")] = outcome;

    m_service->sendResponse(requestId, result);
    qDebug() << "[ACPSession] Sent permission response for request:" << requestId;
}

void ACPSession::setMode(const QString &modeId)
{
    if (m_sessionId.isEmpty()) {
        qWarning() << "[ACPSession] Cannot set mode: no active session";
        return;
    }

    qDebug() << "[ACPSession] Setting mode to:" << modeId;

    QJsonObject params;
    params[QStringLiteral("sessionId")] = m_sessionId;
    params[QStringLiteral("modeId")] = modeId;

    m_service->sendRequest(QStringLiteral("session/set_mode"), params);
}

void ACPSession::sendMessage(const QString &content, const QString &filePath, const QString &selection)
{
    if (m_status != ConnectionStatus::Connected) {
        qWarning() << "[ACPSession] Cannot send message: not connected";
        return;
    }

    // Create user message (for display)
    Message userMsg;
    userMsg.id = QStringLiteral("msg_%1").arg(++m_messageCounter);
    userMsg.role = QStringLiteral("user");
    userMsg.timestamp = QDateTime::currentDateTime();
    userMsg.content = content;
    Q_EMIT messageAdded(userMsg);

    // Create assistant placeholder for streaming
    Message assistantMsg;
    assistantMsg.id = QStringLiteral("msg_%1").arg(++m_messageCounter);
    assistantMsg.role = QStringLiteral("assistant");
    assistantMsg.timestamp = QDateTime::currentDateTime();
    assistantMsg.isStreaming = true;
    m_currentMessageId = assistantMsg.id;
    Q_EMIT messageAdded(assistantMsg);

    // Build prompt blocks for ACP using proper resource blocks
    QJsonArray promptBlocks;

    // Add file context as embedded resource if available
    if (!filePath.isEmpty() && !selection.isEmpty()) {
        // Add resource block with selection
        QJsonObject resourceBlock;
        resourceBlock[QStringLiteral("type")] = QStringLiteral("resource");

        QJsonObject resource;
        resource[QStringLiteral("uri")] = QUrl::fromLocalFile(filePath).toString();
        resource[QStringLiteral("text")] = selection;

        // Try to guess MIME type from file extension
        QString mimeType = QStringLiteral("text/plain");
        if (filePath.endsWith(QStringLiteral(".cpp")) || filePath.endsWith(QStringLiteral(".h")) ||
            filePath.endsWith(QStringLiteral(".cc")) || filePath.endsWith(QStringLiteral(".cxx"))) {
            mimeType = QStringLiteral("text/x-c++");
        } else if (filePath.endsWith(QStringLiteral(".py"))) {
            mimeType = QStringLiteral("text/x-python");
        } else if (filePath.endsWith(QStringLiteral(".js"))) {
            mimeType = QStringLiteral("text/javascript");
        } else if (filePath.endsWith(QStringLiteral(".rs"))) {
            mimeType = QStringLiteral("text/x-rust");
        }
        resource[QStringLiteral("mimeType")] = mimeType;

        resourceBlock[QStringLiteral("resource")] = resource;
        promptBlocks.append(resourceBlock);
    } else if (!filePath.isEmpty()) {
        // Add just a file reference (no content)
        QJsonObject resourceBlock;
        resourceBlock[QStringLiteral("type")] = QStringLiteral("resource");

        QJsonObject resource;
        resource[QStringLiteral("uri")] = QUrl::fromLocalFile(filePath).toString();
        resource[QStringLiteral("text")] = QStringLiteral("(current file)");
        resource[QStringLiteral("mimeType")] = QStringLiteral("text/plain");

        resourceBlock[QStringLiteral("resource")] = resource;
        promptBlocks.append(resourceBlock);
    }

    // Add user's actual message
    QJsonObject textBlock;
    textBlock[QStringLiteral("type")] = QStringLiteral("text");
    textBlock[QStringLiteral("text")] = content;
    promptBlocks.append(textBlock);

    QJsonObject params;
    params[QStringLiteral("sessionId")] = m_sessionId;
    params[QStringLiteral("prompt")] = promptBlocks;

    m_promptRequestId = m_service->sendRequest(QStringLiteral("session/prompt"), params);
    qDebug() << "[ACPSession] Sent session/prompt request, id:" << m_promptRequestId;
}

void ACPSession::onConnected()
{
    qDebug() << "[ACPSession] ACP process started, sending initialize";
    m_status = ConnectionStatus::Connecting;
    Q_EMIT statusChanged(m_status);

    // Send initialize request
    QJsonObject params;
    params[QStringLiteral("protocolVersion")] = 1;

    m_initializeRequestId = m_service->sendRequest(QStringLiteral("initialize"), params);
    qDebug() << "[ACPSession] Sent initialize request, id:" << m_initializeRequestId;
}

void ACPSession::onDisconnected(int exitCode)
{
    qDebug() << "[ACPSession] Disconnected with exit code:" << exitCode;
    m_status = ConnectionStatus::Disconnected;
    m_sessionId.clear();
    Q_EMIT statusChanged(m_status);
}

void ACPSession::onNotification(const QString &method, const QJsonObject &params, int requestId)
{
    if (method == QStringLiteral("session/update")) {
        handleSessionUpdate(params);
    } else if (method == QStringLiteral("session/request_permission")) {
        handlePermissionRequest(params, requestId);
    }
}

void ACPSession::onResponse(int id, const QJsonObject &result, const QJsonObject &error)
{
    if (!error.isEmpty()) {
        qWarning() << "[ACPSession] Error response for id" << id << ":" << error;
        Q_EMIT errorOccurred(error[QStringLiteral("message")].toString());
        return;
    }

    if (id == m_initializeRequestId) {
        handleInitializeResponse(id, result);
    } else if (id == m_sessionNewRequestId) {
        handleSessionNewResponse(id, result);
    } else if (id == m_promptRequestId) {
        // Prompt completed - finish streaming message
        qDebug() << "[ACPSession] Prompt response received, finishing message:" << m_currentMessageId;
        if (!m_currentMessageId.isEmpty()) {
            Q_EMIT messageFinished(m_currentMessageId);
            m_currentMessageId.clear();
        }
        m_promptRequestId = -1;
    }
}

void ACPSession::onError(const QString &message)
{
    Q_EMIT errorOccurred(message);
}

void ACPSession::handleInitializeResponse(int id, const QJsonObject &result)
{
    Q_UNUSED(id);
    qDebug() << "[ACPSession] Initialize response received:" << result;

    // Send session/new request
    QJsonObject params;
    params[QStringLiteral("cwd")] = m_workingDir;
    params[QStringLiteral("mcpServers")] = QJsonArray();  // No MCP servers

    m_sessionNewRequestId = m_service->sendRequest(QStringLiteral("session/new"), params);
    qDebug() << "[ACPSession] Sent session/new request, id:" << m_sessionNewRequestId;
}

void ACPSession::handleSessionNewResponse(int id, const QJsonObject &result)
{
    Q_UNUSED(id);
    m_sessionId = result[QStringLiteral("sessionId")].toString();
    qDebug() << "[ACPSession] Session created with ID:" << m_sessionId;

    // Parse available modes
    m_availableModes = result[QStringLiteral("availableModes")].toArray();
    m_currentMode = result[QStringLiteral("currentModeId")].toString();

    qDebug() << "[ACPSession] Available modes:" << m_availableModes.size();
    qDebug() << "[ACPSession] Current mode:" << m_currentMode;

    if (m_sessionId.isEmpty()) {
        qWarning() << "[ACPSession] ERROR: Session ID is empty! Full result:" << result;
        m_status = ConnectionStatus::Error;
        Q_EMIT errorOccurred(QStringLiteral("Failed to get session ID from ACP"));
    } else {
        m_status = ConnectionStatus::Connected;
        // Emit modes available signal
        Q_EMIT modesAvailable(m_availableModes);
        if (!m_currentMode.isEmpty()) {
            Q_EMIT modeChanged(m_currentMode);
        }
    }

    Q_EMIT statusChanged(m_status);
}

void ACPSession::handleSessionUpdate(const QJsonObject &params)
{
    QJsonObject update = params[QStringLiteral("update")].toObject();
    QString updateType = update[QStringLiteral("sessionUpdate")].toString();

    if (updateType == QStringLiteral("agent_message_start")) {
        // Message already created as placeholder
        qDebug() << "[ACPSession] Agent message started";
    }
    else if (updateType == QStringLiteral("agent_message_chunk")) {
        // Extract text from content object (not chunk)
        QJsonObject content = update[QStringLiteral("content")].toObject();
        QString text = content[QStringLiteral("text")].toString();

        qDebug() << "[ACPSession] Chunk received - messageId:" << m_currentMessageId
                 << "text length:" << text.length() << "text:" << text.left(50);

        if (!text.isEmpty() && !m_currentMessageId.isEmpty()) {
            Q_EMIT messageUpdated(m_currentMessageId, text);
        }
    }
    else if (updateType == QStringLiteral("agent_message_end")) {
        // Finish streaming
        qDebug() << "[ACPSession] Agent message ended - messageId:" << m_currentMessageId;
        if (!m_currentMessageId.isEmpty()) {
            Q_EMIT messageFinished(m_currentMessageId);
            m_currentMessageId.clear();
        } else {
            qWarning() << "[ACPSession] agent_message_end but no current message ID!";
        }
    }
    else if (updateType == QStringLiteral("tool_call")) {
        // Tool call started - data is at root level, not nested
        ToolCall toolCall;
        toolCall.id = update[QStringLiteral("toolCallId")].toString();
        toolCall.status = update[QStringLiteral("status")].toString();
        toolCall.input = update[QStringLiteral("rawInput")].toObject();

        // Get tool name from _meta.claudeCode.toolName or fall back to title
        QJsonObject meta = update[QStringLiteral("_meta")].toObject();
        QJsonObject claudeCode = meta[QStringLiteral("claudeCode")].toObject();
        toolCall.name = claudeCode[QStringLiteral("toolName")].toString();
        if (toolCall.name.isEmpty()) {
            toolCall.name = update[QStringLiteral("title")].toString();
        }

        // Extract file path if present
        // Try locations array first
        QJsonArray locations = update[QStringLiteral("locations")].toArray();
        if (!locations.isEmpty()) {
            QJsonObject location = locations[0].toObject();
            toolCall.filePath = location[QStringLiteral("path")].toString();
        }
        // Fall back to rawInput.file_path
        if (toolCall.filePath.isEmpty()) {
            toolCall.filePath = toolCall.input[QStringLiteral("file_path")].toString();
        }

        qDebug() << "[ACPSession] Tool call - id:" << toolCall.id
                 << "name:" << toolCall.name << "status:" << toolCall.status
                 << "file:" << toolCall.filePath;

        if (!m_currentMessageId.isEmpty()) {
            Q_EMIT toolCallAdded(m_currentMessageId, toolCall);
        }
    }
    else if (updateType == QStringLiteral("tool_call_update")) {
        // Tool call updated - data is at root level
        QString toolCallId = update[QStringLiteral("toolCallId")].toString();
        QString status = update[QStringLiteral("status")].toString();

        // Extract result text from content array
        QString result;
        QJsonArray contentArray = update[QStringLiteral("content")].toArray();
        if (!contentArray.isEmpty()) {
            QJsonObject contentItem = contentArray[0].toObject();
            QJsonObject content = contentItem[QStringLiteral("content")].toObject();
            result = content[QStringLiteral("text")].toString();
        }

        qDebug() << "[ACPSession] Tool call update - id:" << toolCallId
                 << "status:" << status << "result length:" << result.length();

        if (!m_currentMessageId.isEmpty()) {
            Q_EMIT toolCallUpdated(m_currentMessageId, toolCallId, status, result);
        }
    }
    else if (updateType == QStringLiteral("plan")) {
        // Todo list update - uses "entries" field
        QJsonArray entriesArray = update[QStringLiteral("entries")].toArray();
        QList<TodoItem> todos;

        for (const QJsonValue &value : entriesArray) {
            QJsonObject entryObj = value.toObject();
            TodoItem todo;
            todo.content = entryObj[QStringLiteral("content")].toString();
            todo.status = entryObj[QStringLiteral("status")].toString();
            todo.activeForm = entryObj[QStringLiteral("activeForm")].toString();
            // If activeForm is empty, use content
            if (todo.activeForm.isEmpty()) {
                todo.activeForm = todo.content;
            }
            todos.append(todo);
        }

        qDebug() << "[ACPSession] Plan update with" << todos.size() << "entries";
        Q_EMIT todosUpdated(todos);
    }
    else if (updateType == QStringLiteral("current_mode_update")) {
        // Agent changed the mode
        QString newMode = update[QStringLiteral("modeId")].toString();
        qDebug() << "[ACPSession] Mode changed to:" << newMode;
        m_currentMode = newMode;
        Q_EMIT modeChanged(newMode);
    }
}

void ACPSession::handlePermissionRequest(const QJsonObject &params, int requestId)
{
    qDebug() << "[ACPSession] Permission request params:" << params;

    PermissionRequest request;
    request.requestId = requestId;
    request.sessionId = params[QStringLiteral("sessionId")].toString();

    QJsonObject toolCall = params[QStringLiteral("toolCall")].toObject();
    qDebug() << "[ACPSession] toolCall object:" << toolCall;

    request.id = toolCall[QStringLiteral("toolCallId")].toString();
    request.input = toolCall[QStringLiteral("rawInput")].toObject();

    // Get tool name from title field
    request.toolName = toolCall[QStringLiteral("title")].toString();

    // Try alternate field names if title is empty
    if (request.toolName.isEmpty()) {
        request.toolName = toolCall[QStringLiteral("name")].toString();
    }
    if (request.toolName.isEmpty()) {
        request.toolName = toolCall[QStringLiteral("toolName")].toString();
    }
    if (request.toolName.isEmpty()) {
        QJsonObject meta = toolCall[QStringLiteral("_meta")].toObject();
        QJsonObject claudeCode = meta[QStringLiteral("claudeCode")].toObject();
        request.toolName = claudeCode[QStringLiteral("toolName")].toString();
    }

    QJsonArray optionsArray = params[QStringLiteral("options")].toArray();
    for (const QJsonValue &value : optionsArray) {
        request.options.append(value.toObject());
    }

    qDebug() << "[ACPSession] Emitting permission request - toolName:" << request.toolName
             << "options count:" << request.options.size();

    Q_EMIT permissionRequested(request);
}
