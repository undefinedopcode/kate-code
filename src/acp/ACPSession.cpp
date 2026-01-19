#include "ACPSession.h"
#include "ACPService.h"
#include "TerminalManager.h"
#include "../util/TranscriptWriter.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QUrl>
#include <QUuid>

ACPSession::ACPSession(QObject *parent)
    : QObject(parent)
    , m_service(new ACPService(this))
    , m_terminalManager(new TerminalManager(this))
    , m_transcript(new TranscriptWriter(this))
    , m_status(ConnectionStatus::Disconnected)
    , m_initializeRequestId(-1)
    , m_sessionNewRequestId(-1)
    , m_sessionLoadRequestId(-1)
    , m_promptRequestId(-1)
    , m_messageCounter(0)
{
    connect(m_service, &ACPService::connected, this, &ACPSession::onConnected);
    connect(m_service, &ACPService::disconnected, this, &ACPSession::onDisconnected);
    connect(m_service, &ACPService::notificationReceived, this, &ACPSession::onNotification);
    connect(m_service, &ACPService::responseReceived, this, &ACPSession::onResponse);
    connect(m_service, &ACPService::errorOccurred, this, &ACPSession::onError);

    // Forward terminal output to UI
    connect(m_terminalManager, &TerminalManager::outputAvailable,
            this, &ACPSession::terminalOutputUpdated);
}

ACPSession::~ACPSession()
{
    // Disconnect from service signals before cleanup to prevent signal emission during destruction
    if (m_service) {
        disconnect(m_service, nullptr, this, nullptr);
    }
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
    m_transcript->finishSession();
    m_terminalManager->releaseAll();
    m_service->stop();
    m_status = ConnectionStatus::Disconnected;
    m_sessionId.clear();
    m_promptRequestId = -1;
    Q_EMIT statusChanged(m_status);
}

void ACPSession::cancelPrompt()
{
    if (m_promptRequestId < 0) {
        qDebug() << "[ACPSession] cancelPrompt called but no prompt running";
        return;
    }

    qDebug() << "[ACPSession] Cancelling prompt request:" << m_promptRequestId;

    // Send $/cancel_request notification per ACP protocol
    QJsonObject params;
    params[QStringLiteral("id")] = m_promptRequestId;
    m_service->sendNotification(QStringLiteral("$/cancel_request"), params);

    // Finish any streaming message
    if (!m_currentMessageId.isEmpty()) {
        Q_EMIT messageFinished(m_currentMessageId);
        m_currentMessageId.clear();
        m_currentMessageContent.clear();
    }

    m_promptRequestId = -1;
    Q_EMIT promptCancelled();
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

void ACPSession::createNewSession()
{
    if (m_status != ConnectionStatus::Connecting) {
        qWarning() << "[ACPSession] createNewSession called but not in Connecting state";
        return;
    }

    qDebug() << "[ACPSession] Creating new session";

    QJsonObject params;
    params[QStringLiteral("cwd")] = m_workingDir;
    params[QStringLiteral("mcpServers")] = QJsonArray();

    m_sessionNewRequestId = m_service->sendRequest(QStringLiteral("session/new"), params);
    qDebug() << "[ACPSession] Sent session/new request, id:" << m_sessionNewRequestId;
}

void ACPSession::loadSession(const QString &sessionId)
{
    if (m_status != ConnectionStatus::Connecting) {
        qWarning() << "[ACPSession] loadSession called but not in Connecting state";
        return;
    }

    if (sessionId.isEmpty()) {
        qWarning() << "[ACPSession] loadSession called with empty session ID";
        Q_EMIT sessionLoadFailed(QStringLiteral("Empty session ID"));
        return;
    }

    qDebug() << "[ACPSession] Loading existing session:" << sessionId;

    QJsonObject params;
    params[QStringLiteral("sessionId")] = sessionId;
    params[QStringLiteral("cwd")] = m_workingDir;

    m_sessionLoadRequestId = m_service->sendRequest(QStringLiteral("session/load"), params);
    qDebug() << "[ACPSession] Sent session/load request, id:" << m_sessionLoadRequestId;
}

void ACPSession::sendMessage(const QString &content, const QString &filePath, const QString &selection, const QList<ContextChunk> &contextChunks, const QList<ImageAttachment> &images)
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
    userMsg.images = images;  // Include attached images for display
    Q_EMIT messageAdded(userMsg);
    m_transcript->recordMessage(userMsg);

    // Create assistant placeholder for streaming
    Message assistantMsg;
    assistantMsg.id = QStringLiteral("msg_%1").arg(++m_messageCounter);
    assistantMsg.role = QStringLiteral("assistant");
    assistantMsg.timestamp = QDateTime::currentDateTime();
    assistantMsg.isStreaming = true;
    m_currentMessageId = assistantMsg.id;
    m_currentMessageContent.clear();
    m_currentMessageTimestamp = assistantMsg.timestamp;
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

    // Add context chunks as embedded resources
    for (const ContextChunk &chunk : contextChunks) {
        QJsonObject resourceBlock;
        resourceBlock[QStringLiteral("type")] = QStringLiteral("resource");

        QJsonObject resource;
        resource[QStringLiteral("uri")] = QUrl::fromLocalFile(chunk.filePath).toString();
        resource[QStringLiteral("text")] = chunk.content;

        // Guess MIME type from file extension
        QString mimeType = QStringLiteral("text/plain");
        if (chunk.filePath.endsWith(QStringLiteral(".cpp")) || chunk.filePath.endsWith(QStringLiteral(".h")) ||
            chunk.filePath.endsWith(QStringLiteral(".cc")) || chunk.filePath.endsWith(QStringLiteral(".cxx"))) {
            mimeType = QStringLiteral("text/x-c++");
        } else if (chunk.filePath.endsWith(QStringLiteral(".py"))) {
            mimeType = QStringLiteral("text/x-python");
        } else if (chunk.filePath.endsWith(QStringLiteral(".js"))) {
            mimeType = QStringLiteral("text/javascript");
        } else if (chunk.filePath.endsWith(QStringLiteral(".rs"))) {
            mimeType = QStringLiteral("text/x-rust");
        }
        resource[QStringLiteral("mimeType")] = mimeType;

        resourceBlock[QStringLiteral("resource")] = resource;
        promptBlocks.append(resourceBlock);
    }

    // Add image attachments
    for (const ImageAttachment &img : images) {
        QJsonObject imageBlock;
        imageBlock[QStringLiteral("type")] = QStringLiteral("image");
        imageBlock[QStringLiteral("mimeType")] = img.mimeType;
        imageBlock[QStringLiteral("data")] = QString::fromLatin1(img.data.toBase64());
        promptBlocks.append(imageBlock);

        qDebug() << "[ACPSession] Added image block - mimeType:" << img.mimeType
                 << "data size:" << img.data.size() << "bytes"
                 << "base64 length:" << img.data.toBase64().size();
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

    // Advertise terminal support so agent uses terminal/* methods
    QJsonObject capabilities;
    capabilities[QStringLiteral("terminal")] = true;
    params[QStringLiteral("clientCapabilities")] = capabilities;

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
    } else if (method == QStringLiteral("terminal/create")) {
        handleTerminalCreate(params, requestId);
    } else if (method == QStringLiteral("terminal/output")) {
        handleTerminalOutput(params, requestId);
    } else if (method == QStringLiteral("terminal/wait_for_exit")) {
        handleTerminalWaitForExit(params, requestId);
    } else if (method == QStringLiteral("terminal/kill")) {
        handleTerminalKill(params, requestId);
    } else if (method == QStringLiteral("terminal/release")) {
        handleTerminalRelease(params, requestId);
    }
}

void ACPSession::onResponse(int id, const QJsonObject &result, const QJsonObject &error)
{
    // Handle session/load errors specially - we want to fall back to new session
    if (id == m_sessionLoadRequestId) {
        handleSessionLoadResponse(id, result, error);
        return;
    }

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

    // Don't automatically create session - let ChatWidget decide
    // whether to load an existing session or create a new one
    Q_EMIT initializeComplete();
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
        // Start transcript for new session
        m_transcript->startSession(m_sessionId, m_workingDir);
        // Emit modes available signal
        Q_EMIT modesAvailable(m_availableModes);
        if (!m_currentMode.isEmpty()) {
            Q_EMIT modeChanged(m_currentMode);
        }
    }

    Q_EMIT statusChanged(m_status);
}

void ACPSession::handleSessionLoadResponse(int id, const QJsonObject &result, const QJsonObject &error)
{
    Q_UNUSED(id);
    m_sessionLoadRequestId = -1;

    if (!error.isEmpty()) {
        QString errorMsg = error[QStringLiteral("message")].toString();
        qWarning() << "[ACPSession] Session load failed:" << errorMsg;
        Q_EMIT sessionLoadFailed(errorMsg);
        return;
    }

    m_sessionId = result[QStringLiteral("sessionId")].toString();
    qDebug() << "[ACPSession] Session loaded with ID:" << m_sessionId;

    // Parse available modes
    m_availableModes = result[QStringLiteral("availableModes")].toArray();
    m_currentMode = result[QStringLiteral("currentModeId")].toString();

    qDebug() << "[ACPSession] Available modes:" << m_availableModes.size();
    qDebug() << "[ACPSession] Current mode:" << m_currentMode;

    if (m_sessionId.isEmpty()) {
        qWarning() << "[ACPSession] ERROR: Session ID is empty after load!";
        Q_EMIT sessionLoadFailed(QStringLiteral("Empty session ID in response"));
        return;
    }

    m_status = ConnectionStatus::Connected;

    // Start transcript for loaded session (will append if file exists)
    m_transcript->startSession(m_sessionId, m_workingDir);

    // Emit modes available signal
    Q_EMIT modesAvailable(m_availableModes);
    if (!m_currentMode.isEmpty()) {
        Q_EMIT modeChanged(m_currentMode);
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
            m_currentMessageContent += text;  // Accumulate for transcript
            Q_EMIT messageUpdated(m_currentMessageId, text);
        }
    }
    else if (updateType == QStringLiteral("agent_message_end")) {
        // Finish streaming
        qDebug() << "[ACPSession] Agent message ended - messageId:" << m_currentMessageId;
        if (!m_currentMessageId.isEmpty()) {
            // Record complete assistant message to transcript
            if (!m_currentMessageContent.isEmpty()) {
                Message assistantMsg;
                assistantMsg.id = m_currentMessageId;
                assistantMsg.role = QStringLiteral("assistant");
                assistantMsg.content = m_currentMessageContent;
                assistantMsg.timestamp = m_currentMessageTimestamp;
                m_transcript->recordMessage(assistantMsg);
            }
            Q_EMIT messageFinished(m_currentMessageId);
            m_currentMessageId.clear();
            m_currentMessageContent.clear();
        } else {
            qWarning() << "[ACPSession] agent_message_end but no current message ID!";
        }
    }
    else if (updateType == QStringLiteral("tool_call")) {
        // Tool call started - data is at root level, not nested

        // DEBUG: Log full tool_call JSON to see format (especially for edits)
        qDebug() << "[ACPSession] tool_call raw JSON:"
                 << QJsonDocument(update).toJson(QJsonDocument::Compact);

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

        // Extract Edit/Write specific fields from content array
        QJsonArray contentArray = update[QStringLiteral("content")].toArray();
        for (int i = 0; i < contentArray.size(); ++i) {
            QJsonObject contentItem = contentArray[i].toObject();
            QString type = contentItem[QStringLiteral("type")].toString();

            if (type == QStringLiteral("diff")) {
                // This is an Edit operation
                toolCall.operationType = QStringLiteral("edit");

                EditDiff edit;
                edit.oldText = contentItem[QStringLiteral("oldText")].toString();
                edit.newText = contentItem[QStringLiteral("newText")].toString();
                edit.filePath = contentItem[QStringLiteral("filePath")].toString();

                toolCall.edits.append(edit);

                // Keep backward compatibility with single-edit fields
                if (i == 0) {
                    toolCall.oldText = edit.oldText;
                    toolCall.newText = edit.newText;
                }

                qDebug() << "[ACPSession] Edit" << i + 1 << "detected - old:" << edit.oldText.length()
                         << "chars, new:" << edit.newText.length() << "chars";
            } else if (type == QStringLiteral("terminal")) {
                // This tool call has embedded terminal output
                toolCall.terminalId = contentItem[QStringLiteral("terminalId")].toString();
                qDebug() << "[ACPSession] Terminal embedded - id:" << toolCall.terminalId;
            }
        }

        if (!toolCall.edits.isEmpty()) {
            qDebug() << "[ACPSession] Total edits in tool call:" << toolCall.edits.size();
        }

        qDebug() << "[ACPSession] Tool call - id:" << toolCall.id
                 << "name:" << toolCall.name << "status:" << toolCall.status
                 << "file:" << toolCall.filePath << "operation:" << toolCall.operationType;

        // Store tool input for later lookup (e.g., to check ExitPlanMode parameters)
        m_toolCallInputs[toolCall.id] = toolCall.input;

        if (!m_currentMessageId.isEmpty()) {
            Q_EMIT toolCallAdded(m_currentMessageId, toolCall);
            m_transcript->recordToolCall(toolCall);
        }
    }
    else if (updateType == QStringLiteral("tool_call_update")) {
        // Tool call updated - data is at root level
        QString toolCallId = update[QStringLiteral("toolCallId")].toString();
        QString status = update[QStringLiteral("status")].toString();

        // DEBUG: Log full tool_call_update JSON to see format (especially for edits)
        qDebug() << "[ACPSession] tool_call_update raw JSON:"
                 << QJsonDocument(update).toJson(QJsonDocument::Compact);

        // Extract result text from content array
        QString result;
        QString operationType;
        QString newText;

        QJsonArray contentArray = update[QStringLiteral("content")].toArray();
        if (!contentArray.isEmpty()) {
            QJsonObject contentItem = contentArray[0].toObject();
            QJsonObject content = contentItem[QStringLiteral("content")].toObject();
            result = content[QStringLiteral("text")].toString();
        }

        // Check for tool response in _meta.claudeCode.toolResponse
        // toolResponse can be either an object (Write tool) or an array (Bash/other tools)
        QJsonObject meta = update[QStringLiteral("_meta")].toObject();
        QJsonObject claudeCode = meta[QStringLiteral("claudeCode")].toObject();
        QString toolName = claudeCode[QStringLiteral("toolName")].toString();
        QJsonValue toolResponseValue = claudeCode[QStringLiteral("toolResponse")];

        qDebug() << "[ACPSession] DEBUG - toolName:" << toolName
                 << "toolResponse isArray:" << toolResponseValue.isArray()
                 << "toolResponse isObject:" << toolResponseValue.isObject()
                 << "has _meta:" << !meta.isEmpty();

        if (toolResponseValue.isArray()) {
            // Bash and other tools return an array of content items
            QJsonArray toolResponseArray = toolResponseValue.toArray();
            for (const QJsonValue &item : toolResponseArray) {
                QJsonObject itemObj = item.toObject();
                QString text = itemObj[QStringLiteral("text")].toString();
                if (!text.isEmpty()) {
                    result = text;
                    qDebug() << "[ACPSession] Tool response (array) - text length:" << text.length();
                    break;
                }
            }
        } else if (toolResponseValue.isObject()) {
            // Write tool returns an object with type, content, filePath
            QJsonObject toolResponse = toolResponseValue.toObject();
            operationType = toolResponse[QStringLiteral("type")].toString();
            newText = toolResponse[QStringLiteral("content")].toString();
            QString filePath = toolResponse[QStringLiteral("filePath")].toString();

            qDebug() << "[ACPSession] DEBUG - operationType:" << operationType
                     << "filePath:" << filePath
                     << "content length:" << newText.length();

            if (operationType == QStringLiteral("create") && toolName == QStringLiteral("Write")) {
                // Write tool result - show the actual file content
                result = newText;
                qDebug() << "[ACPSession] Write tool - created file" << filePath << "with" << newText.length() << "bytes";
            }
        }

        qDebug() << "[ACPSession] Tool call update - id:" << toolCallId
                 << "status:" << status << "operation:" << operationType
                 << "result length:" << result.length();

        if (!m_currentMessageId.isEmpty()) {
            // Only emit update if we have a result OR status changed
            // (Don't overwrite good results with empty ones from status-only updates)
            if (!result.isEmpty() || !status.isEmpty()) {
                Q_EMIT toolCallUpdated(m_currentMessageId, toolCallId, status, result);
                m_transcript->recordToolUpdate(toolCallId, status, result);
            }
        }

        // Detect ExitPlanMode completion and switch to appropriate mode
        if (toolName == QStringLiteral("ExitPlanMode") && status == QStringLiteral("completed")) {
            // Check if launchSwarm was requested (means "Accept Edits" mode)
            QJsonObject toolInput = m_toolCallInputs.value(toolCallId);
            bool launchSwarm = toolInput[QStringLiteral("launchSwarm")].toBool(false);

            QString newMode = launchSwarm ? QStringLiteral("acceptEdits") : QStringLiteral("default");
            qDebug() << "[ACPSession] ExitPlanMode completed, launchSwarm:" << launchSwarm
                     << "switching to mode:" << newMode;

            m_currentMode = newMode;
            Q_EMIT modeChanged(newMode);

            // Clean up stored input
            m_toolCallInputs.remove(toolCallId);
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
    else if (updateType == QStringLiteral("available_commands_update")) {
        // Available slash commands updated
        qDebug() << "[ACPSession] available_commands_update raw payload:" << QJsonDocument(update).toJson(QJsonDocument::Compact);

        QJsonArray commandsArray = update[QStringLiteral("availableCommands")].toArray();
        QList<SlashCommand> commands;

        for (const QJsonValue &value : commandsArray) {
            QJsonObject cmdObj = value.toObject();
            SlashCommand cmd;
            cmd.name = cmdObj[QStringLiteral("name")].toString();
            cmd.description = cmdObj[QStringLiteral("description")].toString();
            commands.append(cmd);
        }

        qDebug() << "[ACPSession] Available commands updated:" << commands.size() << "commands";
        m_availableCommands = commands;
        Q_EMIT commandsAvailable(commands);
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

void ACPSession::handleTerminalCreate(const QJsonObject &params, int requestId)
{
    QString command = params[QStringLiteral("command")].toString();
    QJsonArray argsArray = params[QStringLiteral("args")].toArray();
    QJsonArray envArray = params[QStringLiteral("env")].toArray();
    QString cwd = params[QStringLiteral("cwd")].toString();
    qint64 outputByteLimit = params[QStringLiteral("outputByteLimit")].toVariant().toLongLong();

    qDebug() << "[ACPSession] terminal/create - command:" << command << "cwd:" << cwd;

    // Build the full command string including any args
    QString fullCommand = command;
    for (const QJsonValue &v : argsArray) {
        fullCommand += QLatin1Char(' ') + v.toString();
    }

    // Build environment from base system environment plus any overrides
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QJsonValue &v : envArray) {
        QJsonObject e = v.toObject();
        env.insert(e[QStringLiteral("name")].toString(), e[QStringLiteral("value")].toString());
    }

    // Use working dir from params, or fall back to session working dir
    if (cwd.isEmpty()) {
        cwd = m_workingDir;
    }

    // Run command through shell - QProcess needs executable separate from args,
    // but ACP sends full command strings like "git status"
    QString terminalId = m_terminalManager->createTerminal(
        QStringLiteral("/bin/bash"),
        QStringList{QStringLiteral("-c"), fullCommand},
        env, cwd, outputByteLimit);

    if (terminalId.isEmpty()) {
        // Failed to create terminal
        QJsonObject error;
        error[QStringLiteral("code")] = -32000;
        error[QStringLiteral("message")] = QStringLiteral("Failed to create terminal");
        m_service->sendResponse(requestId, QJsonObject(), error);
        return;
    }

    QJsonObject result;
    result[QStringLiteral("terminalId")] = terminalId;
    m_service->sendResponse(requestId, result);
}

void ACPSession::handleTerminalOutput(const QJsonObject &params, int requestId)
{
    QString terminalId = params[QStringLiteral("terminalId")].toString();

    qDebug() << "[ACPSession] terminal/output - terminalId:" << terminalId;

    if (!m_terminalManager->isValid(terminalId)) {
        QJsonObject error;
        error[QStringLiteral("code")] = -32001;
        error[QStringLiteral("message")] = QStringLiteral("Terminal not found");
        m_service->sendResponse(requestId, QJsonObject(), error);
        return;
    }

    auto outputResult = m_terminalManager->getOutput(terminalId);

    QJsonObject result;
    result[QStringLiteral("output")] = outputResult.output;
    result[QStringLiteral("truncated")] = outputResult.truncated;

    if (outputResult.exitStatus.has_value()) {
        QJsonObject exitStatus;
        exitStatus[QStringLiteral("exitCode")] = outputResult.exitStatus.value();
        result[QStringLiteral("exitStatus")] = exitStatus;
    }

    m_service->sendResponse(requestId, result);
}

void ACPSession::handleTerminalWaitForExit(const QJsonObject &params, int requestId)
{
    QString terminalId = params[QStringLiteral("terminalId")].toString();
    int timeoutMs = params[QStringLiteral("timeout")].toInt(-1);

    qDebug() << "[ACPSession] terminal/wait_for_exit - terminalId:" << terminalId << "timeout:" << timeoutMs;

    if (!m_terminalManager->isValid(terminalId)) {
        QJsonObject error;
        error[QStringLiteral("code")] = -32001;
        error[QStringLiteral("message")] = QStringLiteral("Terminal not found");
        m_service->sendResponse(requestId, QJsonObject(), error);
        return;
    }

    auto waitResult = m_terminalManager->waitForExit(terminalId, timeoutMs);

    QJsonObject result;
    result[QStringLiteral("output")] = waitResult.output;
    result[QStringLiteral("truncated")] = waitResult.truncated;

    if (waitResult.success) {
        QJsonObject exitStatus;
        exitStatus[QStringLiteral("exitCode")] = waitResult.exitStatus;
        result[QStringLiteral("exitStatus")] = exitStatus;
    }

    m_service->sendResponse(requestId, result);
}

void ACPSession::handleTerminalKill(const QJsonObject &params, int requestId)
{
    QString terminalId = params[QStringLiteral("terminalId")].toString();

    qDebug() << "[ACPSession] terminal/kill - terminalId:" << terminalId;

    if (!m_terminalManager->isValid(terminalId)) {
        QJsonObject error;
        error[QStringLiteral("code")] = -32001;
        error[QStringLiteral("message")] = QStringLiteral("Terminal not found");
        m_service->sendResponse(requestId, QJsonObject(), error);
        return;
    }

    m_terminalManager->killTerminal(terminalId);

    // Get final output after kill
    auto outputResult = m_terminalManager->getOutput(terminalId);

    QJsonObject result;
    result[QStringLiteral("output")] = outputResult.output;
    result[QStringLiteral("truncated")] = outputResult.truncated;

    if (outputResult.exitStatus.has_value()) {
        QJsonObject exitStatus;
        exitStatus[QStringLiteral("exitCode")] = outputResult.exitStatus.value();
        result[QStringLiteral("exitStatus")] = exitStatus;
    }

    m_service->sendResponse(requestId, result);
}

void ACPSession::handleTerminalRelease(const QJsonObject &params, int requestId)
{
    QString terminalId = params[QStringLiteral("terminalId")].toString();

    qDebug() << "[ACPSession] terminal/release - terminalId:" << terminalId;

    if (!m_terminalManager->isValid(terminalId)) {
        QJsonObject error;
        error[QStringLiteral("code")] = -32001;
        error[QStringLiteral("message")] = QStringLiteral("Terminal not found");
        m_service->sendResponse(requestId, QJsonObject(), error);
        return;
    }

    // Get output before releasing
    auto outputResult = m_terminalManager->getOutput(terminalId);

    // Release the terminal (this kills and cleans up)
    m_terminalManager->releaseTerminal(terminalId);

    QJsonObject result;
    result[QStringLiteral("output")] = outputResult.output;
    result[QStringLiteral("truncated")] = outputResult.truncated;

    if (outputResult.exitStatus.has_value()) {
        QJsonObject exitStatus;
        exitStatus[QStringLiteral("exitCode")] = outputResult.exitStatus.value();
        result[QStringLiteral("exitStatus")] = exitStatus;
    }

    m_service->sendResponse(requestId, result);
}
