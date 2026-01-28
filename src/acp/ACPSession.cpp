#include "ACPSession.h"
#include "ACPService.h"
#include "TerminalManager.h"
#include "../util/EditTracker.h"
#include "../util/TranscriptWriter.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QUrl>
#include <QUuid>

// Helper functions to check tool types (mirrors logic in chat.js)
static bool isReadTool(const QString &name)
{
    return name == QStringLiteral("Read") || name == QStringLiteral("mcp__acp__Read");
}

static bool isWriteTool(const QString &name)
{
    return name == QStringLiteral("Write") || name == QStringLiteral("mcp__acp__Write");
}

static bool isEditTool(const QString &name)
{
    return name == QStringLiteral("Edit") || name == QStringLiteral("mcp__acp__Edit");
}

static bool isBashTool(const QString &name)
{
    return name == QStringLiteral("Bash") || name == QStringLiteral("mcp__acp__Bash");
}

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
    , m_editTracker(new EditTracker(this))
{
    connect(m_service, &ACPService::connected, this, &ACPSession::onConnected);
    connect(m_service, &ACPService::disconnected, this, &ACPSession::onDisconnected);
    connect(m_service, &ACPService::notificationReceived, this, &ACPSession::onNotification);
    connect(m_service, &ACPService::responseReceived, this, &ACPSession::onResponse);
    connect(m_service, &ACPService::jsonPayload, this, &ACPSession::jsonPayload);
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

void ACPSession::setExecutable(const QString &executable, const QStringList &args)
{
    m_service->setExecutable(executable, args);
}

void ACPSession::start(const QString &workingDir, const QString &permissionMode)
{
    Q_UNUSED(permissionMode);  // Modes are now discovered from agent

    if (m_status != ConnectionStatus::Disconnected) {
        return;
    }

    m_workingDir = workingDir;
    m_status = ConnectionStatus::Connecting;
    m_editTracker->clear();
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

    // Set status BEFORE stopping the service, because m_service->stop()
    // may synchronously trigger onDisconnected() via QProcess signals.
    // If we set Disconnected first, onDisconnected() sees the state is
    // already Disconnected and skips emitting a duplicate statusChanged.
    m_status = ConnectionStatus::Disconnected;
    m_sessionId.clear();
    m_promptRequestId = -1;

    m_service->stop();

    Q_EMIT statusChanged(m_status);
}

void ACPSession::setTerminalSize(int columns, int rows)
{
    m_terminalManager->setDefaultTerminalSize(columns, rows);
}

void ACPSession::setDocumentProvider(DocumentProvider provider)
{
    m_documentProvider = provider;
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
    if (m_status != ConnectionStatus::Connecting) {
        m_status = ConnectionStatus::Connecting;
        Q_EMIT statusChanged(m_status);
    }

    // Send initialize request
    QJsonObject params;
    params[QStringLiteral("protocolVersion")] = 1;

    // Advertise terminal support so agent uses terminal/* methods
    QJsonObject capabilities;
    capabilities[QStringLiteral("terminal")] = true;

    // Advertise filesystem support
    QJsonObject fsCapabilities;
    fsCapabilities[QStringLiteral("readTextFile")] = true;
    fsCapabilities[QStringLiteral("writeTextFile")] = true;
    capabilities[QStringLiteral("fs")] = fsCapabilities;

    params[QStringLiteral("clientCapabilities")] = capabilities;

    m_initializeRequestId = m_service->sendRequest(QStringLiteral("initialize"), params);
    qDebug() << "[ACPSession] Sent initialize request, id:" << m_initializeRequestId;
}

void ACPSession::onDisconnected(int exitCode)
{
    qDebug() << "[ACPSession] Disconnected with exit code:" << exitCode;
    bool wasAlreadyDisconnected = (m_status == ConnectionStatus::Disconnected);
    m_status = ConnectionStatus::Disconnected;
    m_sessionId.clear();
    if (!wasAlreadyDisconnected) {
        Q_EMIT statusChanged(m_status);
    }
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
    } else if (method == QStringLiteral("fs/read_text_file")) {
        handleFsReadTextFile(params, requestId);
    } else if (method == QStringLiteral("fs/write_text_file")) {
        handleFsWriteTextFile(params, requestId);
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
        // rawInput may be a JSON object or a JSON string that needs parsing
        QJsonValue rawInputVal = update[QStringLiteral("rawInput")];
        if (rawInputVal.isObject()) {
            toolCall.input = rawInputVal.toObject();
        } else if (rawInputVal.isString()) {
            QJsonDocument rawDoc = QJsonDocument::fromJson(rawInputVal.toString().toUtf8());
            if (rawDoc.isObject()) {
                toolCall.input = rawDoc.object();
            }
        }

        // Track current tool call ID for edit tracking
        m_currentToolCallId = toolCall.id;

        // Get tool name from _meta.claudeCode.toolName or fall back to title
        QJsonObject meta = update[QStringLiteral("_meta")].toObject();
        QJsonObject claudeCode = meta[QStringLiteral("claudeCode")].toObject();
        toolCall.name = claudeCode[QStringLiteral("toolName")].toString();
        if (toolCall.name.isEmpty()) {
            toolCall.name = update[QStringLiteral("title")].toString();
        }

        // vibe-acp uses "kind" field to indicate tool type (e.g., "execute" for Bash)
        QString kind = update[QStringLiteral("kind")].toString();

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

        // Infer tool type from vibe-acp kind or title if toolName is not a known tool
        // vibe-acp uses "kind" field: "execute" for Bash, or titles like "Reading ...", "Editing ..."
        if (!isReadTool(toolCall.name) && !isWriteTool(toolCall.name) &&
            !isEditTool(toolCall.name) && !isBashTool(toolCall.name)) {
            // Check kind field first - more reliable than title matching
            if (kind == QStringLiteral("execute")) {
                toolCall.name = QStringLiteral("Bash");
                // Extract command from rawInput for display
                QString command = toolCall.input[QStringLiteral("command")].toString();
                if (!command.isEmpty()) {
                    toolCall.operationType = QStringLiteral("bash");
                }
            }
        }
        if (!isReadTool(toolCall.name) && !isWriteTool(toolCall.name) &&
            !isEditTool(toolCall.name) && !isBashTool(toolCall.name)) {
            QString title = update[QStringLiteral("title")].toString();
            if (title.startsWith(QStringLiteral("Reading "))) {
                toolCall.name = QStringLiteral("Read");
                if (toolCall.filePath.isEmpty()) {
                    // Extract path from title - it may be relative
                    QString titlePath = title.mid(8);  // len("Reading ")
                    if (!titlePath.isEmpty()) {
                        toolCall.filePath = QDir(m_workingDir).absoluteFilePath(titlePath);
                    }
                }
            } else if (title.startsWith(QStringLiteral("Editing "))) {
                toolCall.name = QStringLiteral("Edit");
                if (toolCall.filePath.isEmpty()) {
                    QString titlePath = title.mid(8);
                    if (!titlePath.isEmpty()) {
                        toolCall.filePath = QDir(m_workingDir).absoluteFilePath(titlePath);
                    }
                }
            } else if (title.startsWith(QStringLiteral("Writing "))) {
                toolCall.name = QStringLiteral("Write");
                if (toolCall.filePath.isEmpty()) {
                    QString titlePath = title.mid(8);
                    if (!titlePath.isEmpty()) {
                        toolCall.filePath = QDir(m_workingDir).absoluteFilePath(titlePath);
                    }
                }
            } else if (title.startsWith(QStringLiteral("Patching "))) {
                // vibe-acp Edit uses "Patching file.txt (N blocks)" format
                toolCall.name = QStringLiteral("Edit");
                if (toolCall.filePath.isEmpty()) {
                    QString titlePath = title.mid(9);  // len("Patching ")
                    // Remove trailing " (N blocks)" if present
                    int parenIdx = titlePath.lastIndexOf(QStringLiteral(" ("));
                    if (parenIdx > 0) {
                        titlePath = titlePath.left(parenIdx);
                    }
                    if (!titlePath.isEmpty()) {
                        toolCall.filePath = QDir(m_workingDir).absoluteFilePath(titlePath);
                    }
                }
            } else if (title.contains(QStringLiteral("bash")) || title.contains(QStringLiteral("Bash")) ||
                       title.startsWith(QStringLiteral("Running "))) {
                toolCall.name = QStringLiteral("Bash");
            }
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
        QString updateFilePath;  // File path extracted from this update

        QString terminalId;  // Terminal ID extracted from content (vibe-acp sends this in tool_call_update)

        QJsonArray contentArray = update[QStringLiteral("content")].toArray();
        for (int i = 0; i < contentArray.size(); ++i) {
            QJsonObject contentItem = contentArray[i].toObject();
            QString contentType = contentItem[QStringLiteral("type")].toString();

            if (contentType == QStringLiteral("terminal")) {
                // vibe-acp sends terminal info in tool_call_update (not in initial tool_call)
                terminalId = contentItem[QStringLiteral("terminalId")].toString();
                if (terminalId.isEmpty()) {
                    terminalId = contentItem[QStringLiteral("terminal_id")].toString();
                }
                qDebug() << "[ACPSession] tool_call_update has terminal content - id:" << terminalId;
            } else if (contentType == QStringLiteral("content")) {
                QJsonObject content = contentItem[QStringLiteral("content")].toObject();
                QString text = content[QStringLiteral("text")].toString();
                if (!text.isEmpty()) {
                    result = text;
                }
            }
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

        // If result is still empty or just a summary, check rawOutput (vibe-acp format)
        // vibe-acp tools like Read return actual content in rawOutput as a JSON string
        if (result.isEmpty() || (!update[QStringLiteral("rawOutput")].isUndefined() && result.length() < 200)) {
            QString rawOutput = update[QStringLiteral("rawOutput")].toString();
            if (!rawOutput.isEmpty()) {
                // rawOutput may be a JSON string (e.g., Read tool returns {"path":...,"content":...})
                QJsonDocument rawDoc = QJsonDocument::fromJson(rawOutput.toUtf8());
                if (!rawDoc.isNull() && rawDoc.isObject()) {
                    QJsonObject rawObj = rawDoc.object();

                    // Check if this is an Edit/Patch result (has blocks_applied field)
                    if (rawObj.contains(QStringLiteral("blocks_applied"))) {
                        int blocksApplied = rawObj[QStringLiteral("blocks_applied")].toInt();
                        int linesChanged = rawObj[QStringLiteral("lines_changed")].toInt();
                        QString file = rawObj[QStringLiteral("file")].toString();
                        if (!file.isEmpty()) {
                            updateFilePath = file;
                        }
                        // The "content" field contains SEARCH/REPLACE diff text - use it as result
                        QString diffContent = rawObj[QStringLiteral("content")].toString();
                        if (!diffContent.isEmpty()) {
                            result = diffContent;
                        } else {
                            result = QStringLiteral("%1 block(s) applied, %2 line(s) changed")
                                .arg(blocksApplied).arg(linesChanged);
                        }
                        qDebug() << "[ACPSession] Edit rawOutput - file:" << file
                                 << "blocks:" << blocksApplied << "lines:" << linesChanged;
                    } else {
                        QString fileContent = rawObj[QStringLiteral("content")].toString();
                        if (!fileContent.isEmpty()) {
                            result = fileContent;
                            qDebug() << "[ACPSession] Extracted content from rawOutput - length:" << result.length();
                        }
                    }
                    // Extract file path from rawOutput (e.g., Read tool returns {"path":"/abs/path"})
                    // Also check "file" field (Edit tool uses this)
                    QString rawPath = rawObj[QStringLiteral("path")].toString();
                    if (rawPath.isEmpty()) {
                        rawPath = rawObj[QStringLiteral("file")].toString();
                    }
                    if (!rawPath.isEmpty() && updateFilePath.isEmpty()) {
                        updateFilePath = rawPath;
                        qDebug() << "[ACPSession] Extracted file path from rawOutput:" << updateFilePath;
                    }
                } else {
                    // rawOutput is plain text
                    result = rawOutput;
                    qDebug() << "[ACPSession] Using rawOutput as plain text - length:" << result.length();
                }
            }
        }

        qDebug() << "[ACPSession] Tool call update - id:" << toolCallId
                 << "status:" << status << "operation:" << operationType
                 << "result length:" << result.length();

        if (!m_currentMessageId.isEmpty()) {
            // Link terminal to tool call if we found one (vibe-acp sends terminal in tool_call_update)
            if (!terminalId.isEmpty()) {
                Q_EMIT toolCallTerminalIdSet(m_currentMessageId, toolCallId, terminalId);
            }

            // Only emit update if we have a result OR status changed
            // (Don't overwrite good results with empty ones from status-only updates)
            if (!result.isEmpty() || !status.isEmpty()) {
                Q_EMIT toolCallUpdated(m_currentMessageId, toolCallId, status, result, updateFilePath);
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
    env.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));  // Prevent git from using pager
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

void ACPSession::handleFsReadTextFile(const QJsonObject &params, int requestId)
{
    QString path = params[QStringLiteral("path")].toString();
    int line = params[QStringLiteral("line")].toInt(1);  // 1-based, default to 1
    int limit = params[QStringLiteral("limit")].toInt(-1);  // -1 means no limit

    qDebug() << "[ACPSession] fs/read_text_file - path:" << path << "line:" << line << "limit:" << limit;

    if (path.isEmpty()) {
        QJsonObject error;
        error[QStringLiteral("code")] = -32602;
        error[QStringLiteral("message")] = QStringLiteral("Missing required parameter: path");
        m_service->sendResponse(requestId, QJsonObject(), error);
        return;
    }

    QString content;
    bool fromKate = false;

    // Try to get content from Kate document first (may have unsaved changes)
    if (m_documentProvider) {
        KTextEditor::Document *doc = m_documentProvider(path);
        if (doc) {
            content = doc->text();
            fromKate = true;
            qDebug() << "[ACPSession] Reading from Kate document:" << path;
        }
    }

    // Fall back to filesystem if not open in Kate
    if (!fromKate) {
        QFile file(path);
        if (!file.exists()) {
            QJsonObject error;
            error[QStringLiteral("code")] = -32001;
            error[QStringLiteral("message")] = QString(QStringLiteral("File not found: ") + path);
            m_service->sendResponse(requestId, QJsonObject(), error);
            return;
        }

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonObject error;
            error[QStringLiteral("code")] = -32001;
            error[QStringLiteral("message")] = QString(QStringLiteral("Cannot open file: ") + file.errorString());
            m_service->sendResponse(requestId, QJsonObject(), error);
            return;
        }

        content = QString::fromUtf8(file.readAll());
        file.close();
    }

    // Apply line offset and limit
    QStringList lines = content.split(QLatin1Char('\n'));
    QStringList resultLines;

    for (int i = 0; i < lines.size(); ++i) {
        int currentLine = i + 1;  // 1-based line numbers

        // Skip lines before the requested start line
        if (currentLine < line) {
            continue;
        }

        resultLines.append(lines[i]);

        // Check limit
        if (limit > 0 && resultLines.size() >= limit) {
            break;
        }
    }

    QJsonObject result;
    result[QStringLiteral("content")] = resultLines.join(QLatin1Char('\n'));
    m_service->sendResponse(requestId, result);
}

// Helper struct for tracking line changes
struct LineChange {
    int startLine;      // 0-based start line in old document
    int oldLineCount;   // Number of lines to remove
    int newLineCount;   // Number of lines to insert
    QStringList newLines;  // The new lines to insert
};

// Compute minimal line-based changes between old and new content
static QList<LineChange> computeLineChanges(const QStringList &oldLines, const QStringList &newLines)
{
    QList<LineChange> changes;

    int oldSize = oldLines.size();
    int newSize = newLines.size();
    int i = 0, j = 0;

    while (i < oldSize || j < newSize) {
        // Find common prefix from current position
        int commonStart = 0;
        while (i + commonStart < oldSize && j + commonStart < newSize &&
               oldLines[i + commonStart] == newLines[j + commonStart]) {
            ++commonStart;
        }
        i += commonStart;
        j += commonStart;

        if (i >= oldSize && j >= newSize) {
            break;  // Done
        }

        // Find common suffix from the end of remaining content
        int oldRemaining = oldSize - i;
        int newRemaining = newSize - j;
        int commonEnd = 0;
        while (commonEnd < oldRemaining && commonEnd < newRemaining &&
               oldLines[oldSize - 1 - commonEnd] == newLines[newSize - 1 - commonEnd]) {
            ++commonEnd;
        }

        // The change spans from current position to just before the common suffix
        int oldChangeCount = oldRemaining - commonEnd;
        int newChangeCount = newRemaining - commonEnd;

        if (oldChangeCount > 0 || newChangeCount > 0) {
            LineChange change;
            change.startLine = i;
            change.oldLineCount = oldChangeCount;
            change.newLineCount = newChangeCount;
            for (int k = 0; k < newChangeCount; ++k) {
                change.newLines.append(newLines[j + k]);
            }
            changes.append(change);
        }

        // Move past the changed region
        i += oldChangeCount;
        j += newChangeCount;
    }

    return changes;
}

// Apply surgical edits to a Kate document, preserving cursor position where possible
// Returns the list of line changes applied (empty on failure or no changes)
static QList<LineChange> applySurgicalEdits(KTextEditor::Document *doc, const QString &newContent)
{
    QString oldContent = doc->text();

    // If content is identical, no changes needed
    if (oldContent == newContent) {
        return QList<LineChange>();
    }

    // Split into lines for comparison
    QStringList oldLines = oldContent.split(QLatin1Char('\n'));
    QStringList newLines = newContent.split(QLatin1Char('\n'));

    // Compute the changes
    QList<LineChange> changes = computeLineChanges(oldLines, newLines);

    if (changes.isEmpty()) {
        // Content differs only in ways not captured by line comparison (shouldn't happen)
        if (doc->setText(newContent)) {
            // Return a single change representing the whole document
            LineChange wholeDoc;
            wholeDoc.startLine = 0;
            wholeDoc.oldLineCount = oldLines.size();
            wholeDoc.newLineCount = newLines.size();
            return QList<LineChange>() << wholeDoc;
        }
        return QList<LineChange>();
    }

    // Save cursor positions from all views
    QList<KTextEditor::View *> views = doc->views();
    QList<KTextEditor::Cursor> savedCursors;
    for (KTextEditor::View *view : views) {
        savedCursors.append(view->cursorPosition());
    }

    // Start an editing transaction for undo grouping (RAII - finishes when scope exits)
    KTextEditor::Document::EditingTransaction transaction(doc);

    // Apply changes in reverse order to avoid line number shifting issues
    for (int changeIdx = changes.size() - 1; changeIdx >= 0; --changeIdx) {
        const LineChange &change = changes[changeIdx];

        // Calculate the range to replace
        int startLine = change.startLine;
        int endLine = change.startLine + change.oldLineCount;

        KTextEditor::Cursor startPos(startLine, 0);
        KTextEditor::Cursor endPos;

        if (endLine >= oldLines.size()) {
            // Replacing to end of document
            int lastLine = oldLines.size() - 1;
            endPos = KTextEditor::Cursor(lastLine, oldLines[lastLine].length());
        } else {
            // Replacing up to start of next unchanged line
            endPos = KTextEditor::Cursor(endLine, 0);
        }

        // Build replacement text
        QString replacement = change.newLines.join(QLatin1Char('\n'));

        // Add trailing newline if we're not at document end and replacing full lines
        if (endLine < oldLines.size() && !replacement.isEmpty()) {
            replacement += QLatin1Char('\n');
        } else if (change.oldLineCount > 0 && endLine < oldLines.size()) {
            // We're deleting lines that had a trailing newline
            // The replacement should not add one if empty
        }

        // Special case: inserting new lines at document end
        if (startLine >= oldLines.size()) {
            startPos = KTextEditor::Cursor(oldLines.size() - 1, oldLines.last().length());
            if (!replacement.isEmpty()) {
                replacement = QLatin1Char('\n') + replacement;
            }
        }

        KTextEditor::Range range(startPos, endPos);
        doc->replaceText(range, replacement);

        // Update oldLines to reflect the change for subsequent iterations
        // (since we're going in reverse, this updates line count for earlier changes)
        for (int r = 0; r < change.oldLineCount && startLine < oldLines.size(); ++r) {
            oldLines.removeAt(startLine);
        }
        for (int a = 0; a < change.newLines.size(); ++a) {
            oldLines.insert(startLine + a, change.newLines[a]);
        }
    }

    // Transaction finishes automatically when 'transaction' goes out of scope

    // Restore cursor positions, clamping to valid positions
    for (int v = 0; v < views.size(); ++v) {
        KTextEditor::Cursor savedCursor = savedCursors[v];
        int newLine = savedCursor.line();

        // Clamp to valid range
        if (newLine >= doc->lines()) {
            newLine = doc->lines() - 1;
        }
        if (newLine < 0) {
            newLine = 0;
        }

        int newCol = savedCursor.column();
        int lineLength = doc->lineLength(newLine);
        if (newCol > lineLength) {
            newCol = lineLength;
        }

        views[v]->setCursorPosition(KTextEditor::Cursor(newLine, newCol));
    }

    return changes;
}

void ACPSession::handleFsWriteTextFile(const QJsonObject &params, int requestId)
{
    QString path = params[QStringLiteral("path")].toString();
    QString content = params[QStringLiteral("content")].toString();

    qDebug() << "[ACPSession] fs/write_text_file - path:" << path << "content length:" << content.length();

    if (path.isEmpty()) {
        QJsonObject error;
        error[QStringLiteral("code")] = -32602;
        error[QStringLiteral("message")] = QStringLiteral("Missing required parameter: path");
        m_service->sendResponse(requestId, QJsonObject(), error);
        return;
    }

    bool writtenViaKate = false;

    // Check if this is a new file
    bool isNewFile = !QFile::exists(path);

    // Try to write through Kate document if open
    if (m_documentProvider) {
        KTextEditor::Document *doc = m_documentProvider(path);
        if (doc) {
            qDebug() << "[ACPSession] Writing through Kate document:" << path;

            // Use surgical edits to preserve cursor position and minimize gutter markers
            QList<LineChange> changes = applySurgicalEdits(doc, content);
            if (!changes.isEmpty()) {
                bool saved = doc->save();
                if (saved) {
                    writtenViaKate = true;
                    qDebug() << "[ACPSession] Kate document saved successfully (surgical edit)";

                    // Record edits for tracking
                    for (const LineChange &change : changes) {
                        m_editTracker->recordEdit(m_currentToolCallId, path,
                                                   change.startLine, change.oldLineCount, change.newLineCount);
                    }
                } else {
                    qWarning() << "[ACPSession] Failed to save Kate document, falling back to direct write";
                }
            } else {
                // Empty changes means content was identical - no edit to track
                writtenViaKate = true;
                qDebug() << "[ACPSession] Kate document unchanged (identical content)";
            }
        }
    }

    // Fall back to direct filesystem write
    if (!writtenViaKate) {
        // Ensure parent directory exists
        QFileInfo fileInfo(path);
        QDir parentDir = fileInfo.absoluteDir();
        if (!parentDir.exists()) {
            if (!parentDir.mkpath(QStringLiteral("."))) {
                QJsonObject error;
                error[QStringLiteral("code")] = -32001;
                error[QStringLiteral("message")] = QString(QStringLiteral("Cannot create parent directory: ") + parentDir.absolutePath());
                m_service->sendResponse(requestId, QJsonObject(), error);
                return;
            }
        }

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QJsonObject error;
            error[QStringLiteral("code")] = -32001;
            error[QStringLiteral("message")] = QString(QStringLiteral("Cannot open file for writing: ") + file.errorString());
            m_service->sendResponse(requestId, QJsonObject(), error);
            return;
        }

        QTextStream out(&file);
        out << content;
        file.close();

        // Record edit for tracking (count lines in content)
        int lineCount = content.count(QLatin1Char('\n')) + (content.isEmpty() ? 0 : 1);
        if (isNewFile) {
            m_editTracker->recordNewFile(m_currentToolCallId, path, lineCount);
        } else {
            // For direct writes to existing files, we don't know the exact changes
            // Record as a full-file replacement
            m_editTracker->recordEdit(m_currentToolCallId, path, 0, -1, lineCount);
        }
    }

    QJsonObject result;
    result[QStringLiteral("result")] = QJsonValue::Null;
    m_service->sendResponse(requestId, result);
}
