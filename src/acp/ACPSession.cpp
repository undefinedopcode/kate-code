#include "ACPSession.h"
#include "ACPService.h"
#include "TerminalManager.h"
#include "../util/EditTracker.h"
#include "../util/TranscriptWriter.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

// Helper functions to check tool types (mirrors logic in chat.js)
// Uses suffix matching for katecode tools to handle different MCP host prefixes
static bool isReadTool(const QString &name)
{
    return name == QStringLiteral("Read") || name == QStringLiteral("mcp__acp__Read") ||
           name.endsWith(QStringLiteral("_katecode_read"));
}

static bool isWriteTool(const QString &name)
{
    return name == QStringLiteral("Write") || name == QStringLiteral("mcp__acp__Write") ||
           name.endsWith(QStringLiteral("_katecode_write"));
}

static bool isEditTool(const QString &name)
{
    return name == QStringLiteral("Edit") || name == QStringLiteral("mcp__acp__Edit") ||
           name.endsWith(QStringLiteral("_katecode_edit"));
}

static bool isBashTool(const QString &name)
{
    return name == QStringLiteral("Bash") || name == QStringLiteral("mcp__acp__Bash");
}

// Infer tool name from toolCallId prefix (e.g., Gemini uses "run_shell_command-<timestamp>")
static QString inferToolNameFromId(const QString &toolCallId)
{
    // Extract prefix before the last dash-digits segment
    int dashIdx = toolCallId.lastIndexOf(QLatin1Char('-'));
    if (dashIdx <= 0) return {};

    QString prefix = toolCallId.left(dashIdx);

    if (prefix == QStringLiteral("run_shell_command") || prefix == QStringLiteral("bash") || prefix == QStringLiteral("execute")) {
        return QStringLiteral("Bash");
    } else if (prefix == QStringLiteral("read_file") || prefix == QStringLiteral("read")) {
        return QStringLiteral("Read");
    } else if (prefix == QStringLiteral("write_file") || prefix == QStringLiteral("write") || prefix == QStringLiteral("create_file")) {
        return QStringLiteral("Write");
    } else if (prefix == QStringLiteral("edit_file") || prefix == QStringLiteral("edit") || prefix == QStringLiteral("patch_file")) {
        return QStringLiteral("Edit");
    }
    return {};
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
    , m_sessionConfigRequestId(-1)
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

QString ACPSession::transcriptFilePath() const
{
    return m_transcript ? m_transcript->transcriptPath() : QString();
}

void ACPSession::setExecutable(const QString &executable, const QStringList &args)
{
    m_service->setExecutable(executable, args);
}

void ACPSession::setMcpConfigPath(const QString &path)
{
    m_mcpConfigPath = path;
    qDebug() << "[ACPSession] MCP config path set to:" << path;
}

void ACPSession::setSessionConfig(const QJsonObject &config)
{
    m_sessionConfig = config;
    qDebug() << "[ACPSession] Session configuration keys set to:" << config.keys();
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
    m_sessionLoadId.clear();
    m_initializeRequestId = -1;
    m_sessionNewRequestId = -1;
    m_sessionLoadRequestId = -1;
    m_sessionConfigRequestId = -1;
    m_pendingSessionConfigKeys.clear();
    m_currentSessionConfigKey.clear();
    m_availableConfigOptions = {};
    m_promptRequestId = -1;
    m_promptQueue.clear();
    m_messageCounter = 0;
    // Discard any in-flight or queued interactive mode-change state.
    m_interactiveModeRequestId = -1;
    m_pendingModeValue.clear();
    m_queuedModeValue.clear();
    // Reset agent capability flags so they don't leak across reconnections.
    m_supportsImage = false;
    m_supportsEmbeddedContext = false;
    m_supportsPromptQueueing = false;

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
    // Discard queued prompts: a cancellation signals the user wants to stop, so
    // silently auto-sending stale follow-ups would be surprising.
    if (!m_promptQueue.isEmpty()) {
        qDebug() << "[ACPSession] Discarding" << m_promptQueue.size() << "queued prompt(s) on cancel";
        m_promptQueue.clear();
    }
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
        qWarning() << "[ACPSession] setMode: no active session, ignoring request for" << modeId;
        return;
    }

    // Nothing to do if the mode is already confirmed and no other request is pending.
    if (modeId == m_currentMode && m_interactiveModeRequestId < 0) {
        qDebug() << "[ACPSession] setMode: mode" << modeId << "already active, skipping";
        return;
    }

    // Coalesce rapid selections: queue the latest value and let the in-flight
    // response handler send it once the current round-trip completes.
    if (m_interactiveModeRequestId >= 0) {
        qDebug() << "[ACPSession] setMode: request in flight, queuing" << modeId;
        m_queuedModeValue = modeId;
        return;
    }

    // Decide transport: prefer session/set_config_option when the agent has
    // advertised a config option with id=="mode"; fall back to session/set_mode
    // for older agents that only advertise legacy modes.
    bool useModern = false;
    for (const QJsonValue &value : m_availableConfigOptions) {
        if (value.toObject()[QStringLiteral("id")].toString() == QStringLiteral("mode")) {
            useModern = true;
            break;
        }
    }

    if (useModern) {
        // Validate that modeId is one of the advertised values (use m_availableModes
        // which updateSessionConfigOptions() already built from the option's choices).
        bool valid = false;
        for (const QJsonValue &v : m_availableModes) {
            if (v.toObject()[QStringLiteral("id")].toString() == modeId) {
                valid = true;
                break;
            }
        }
        if (!valid) {
            qWarning() << "[ACPSession] setMode: mode" << modeId
                       << "not in advertised options, ignoring";
            return;
        }

        QJsonObject params;
        params[QStringLiteral("sessionId")] = m_sessionId;
        params[QStringLiteral("configId")]  = QStringLiteral("mode");
        params[QStringLiteral("value")]     = modeId;
        int reqId = m_service->sendRequest(
            QStringLiteral("session/set_config_option"), params);
        if (reqId < 0) {
            qWarning() << "[ACPSession] setMode: failed to send session/set_config_option";
            return;
        }
        m_interactiveModeRequestId = reqId;
        m_pendingModeValue = modeId;
        qDebug() << "[ACPSession] setMode: sent session/set_config_option mode=" << modeId
                 << "reqId=" << reqId;
    } else {
        // Legacy transport: session/set_mode
        QJsonObject params;
        params[QStringLiteral("sessionId")] = m_sessionId;
        params[QStringLiteral("modeId")]    = modeId;
        int reqId = m_service->sendRequest(
            QStringLiteral("session/set_mode"), params);
        if (reqId < 0) {
            qWarning() << "[ACPSession] setMode: failed to send session/set_mode";
            return;
        }
        m_interactiveModeRequestId = reqId;
        m_pendingModeValue = modeId;
        qDebug() << "[ACPSession] setMode: sent session/set_mode modeId=" << modeId
                 << "reqId=" << reqId;
    }
}

static QString expandConfigPath(const QString &path)
{
    QString expanded = path.trimmed();
    if (expanded == QStringLiteral("~")) {
        expanded = QDir::homePath();
    } else if (expanded.startsWith(QStringLiteral("~/"))) {
        expanded = QDir::homePath() + expanded.mid(1);
    }

    static const QRegularExpression envVarPattern(QStringLiteral("\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}"));
    QRegularExpressionMatchIterator it = envVarPattern.globalMatch(expanded);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        expanded.replace(match.captured(0), env.value(match.captured(1)));
    }

    return QDir::cleanPath(expanded);
}

static QJsonArray loadExternalMcpServers(const QString &configPath)
{
    QJsonArray servers;

    // Skip if no config path is set
    if (configPath.isEmpty()) {
        qDebug() << "[ACPSession] No MCP config path configured, skipping external servers";
        return servers;
    }

    const QString expandedConfigPath = expandConfigPath(configPath);
    QFile configFile(expandedConfigPath);

    if (!configFile.exists()) {
        qDebug() << "[ACPSession] No external MCP config found at:" << expandedConfigPath;
        return servers;
    }

    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[ACPSession] Failed to open MCP config:" << expandedConfigPath;
        return servers;
    }

    QByteArray data = configFile.readAll();
    configFile.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "[ACPSession] Invalid JSON in MCP config:" << expandedConfigPath;
        return servers;
    }

    QJsonObject root = doc.object();
    QJsonObject mcpServersObj = root[QStringLiteral("mcpServers")].toObject();

    if (mcpServersObj.isEmpty()) {
        qDebug() << "[ACPSession] No mcpServers found in config";
        return servers;
    }

    for (auto it = mcpServersObj.begin(); it != mcpServersObj.end(); ++it) {
        QString serverName = it.key();
        QJsonObject serverConfig = it.value().toObject();

        if (serverConfig.isEmpty()) {
            qWarning() << "[ACPSession] Empty config for MCP server:" << serverName;
            continue;
        }

        // Build server object in ACP format
        QJsonObject server;
        server[QStringLiteral("name")] = serverName;
        server[QStringLiteral("type")] = serverConfig.value(QStringLiteral("type")).toString(QStringLiteral("stdio"));
        server[QStringLiteral("command")] = serverConfig[QStringLiteral("command")];
        server[QStringLiteral("args")] = serverConfig[QStringLiteral("args")].toArray();

        // Convert env object to array of {name, value} objects for ACP protocol
        QJsonObject envObj = serverConfig[QStringLiteral("env")].toObject();
        QJsonArray envArray;
        for (auto envIt = envObj.begin(); envIt != envObj.end(); ++envIt) {
            QJsonObject envEntry;
            envEntry[QStringLiteral("name")] = envIt.key();
            envEntry[QStringLiteral("value")] = envIt.value().toString();
            envArray.append(envEntry);
        }
        server[QStringLiteral("env")] = envArray;

        servers.append(server);
        qDebug() << "[ACPSession] Loaded external MCP server:" << serverName
                 << "command:" << serverConfig[QStringLiteral("command")].toString();
    }

    qDebug() << "[ACPSession] Loaded" << servers.size() << "external MCP server(s)";
    return servers;
}

static void addExecutableCandidate(QStringList &candidates, const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    const QString cleanPath = QDir::cleanPath(path);
    if (!candidates.contains(cleanPath)) {
        candidates.append(cleanPath);
    }
}

static QString findKateMcpServerPath()
{
    QStringList candidates;

#ifdef KATE_MCP_SERVER_PATH
    const QString compiledPath = QStringLiteral(KATE_MCP_SERVER_PATH);
    addExecutableCandidate(candidates, compiledPath);

    static const QStringList installPrefixes = {
        QDir::homePath() + QStringLiteral("/.local"),
        QStringLiteral("/usr/local"),
        QStringLiteral("/usr"),
    };
    static const QStringList compiledPrefixes = {
        QStringLiteral("/usr/local"),
        QStringLiteral("/usr"),
    };
    for (const QString &compiledPrefix : compiledPrefixes) {
        if (!compiledPath.startsWith(compiledPrefix + QLatin1Char('/'))) {
            continue;
        }

        const QString relativePath = compiledPath.mid(compiledPrefix.size());
        for (const QString &installPrefix : installPrefixes) {
            addExecutableCandidate(candidates, installPrefix + relativePath);
        }
    }
#endif

    addExecutableCandidate(candidates, QCoreApplication::applicationDirPath() + QStringLiteral("/kate-mcp-server"));
    addExecutableCandidate(candidates, QDir::homePath() + QStringLiteral("/.local/libexec/kate-mcp-server"));
    addExecutableCandidate(candidates, QDir::homePath() + QStringLiteral("/.local/lib64/libexec/kate-mcp-server"));
    addExecutableCandidate(candidates, QStringLiteral("/usr/local/libexec/kate-mcp-server"));
    addExecutableCandidate(candidates, QStringLiteral("/usr/local/lib64/libexec/kate-mcp-server"));
    addExecutableCandidate(candidates, QStringLiteral("/usr/libexec/kate-mcp-server"));
    addExecutableCandidate(candidates, QStringLiteral("/usr/lib64/libexec/kate-mcp-server"));

    addExecutableCandidate(candidates, QStandardPaths::findExecutable(QStringLiteral("kate-mcp-server")));

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile() && info.isExecutable()) {
            return candidate;
        }
    }

    qWarning() << "[ACPSession] Kate MCP server not found. Checked:" << candidates;
    return {};
}

QJsonArray ACPSession::buildMcpServers() const
{
    QJsonArray mcpServers;
    const QString mcpServerPath = findKateMcpServerPath();

    if (!mcpServerPath.isEmpty()) {
        QJsonObject kateMcp;
        kateMcp[QStringLiteral("type")] = QStringLiteral("stdio");
        kateMcp[QStringLiteral("name")] = QStringLiteral("kate");
        kateMcp[QStringLiteral("command")] = mcpServerPath;
        kateMcp[QStringLiteral("args")] = QJsonArray();

        // The child MCP server must inherit the desktop session bus so it can
        // reach the editor service even when the ACP agent sanitizes its env.
        QJsonArray envArray;
        static const QStringList sessionEnvVars = {
            QStringLiteral("DBUS_SESSION_BUS_ADDRESS"),
            QStringLiteral("XDG_RUNTIME_DIR"),
            QStringLiteral("DISPLAY"),
            QStringLiteral("WAYLAND_DISPLAY"),
            QStringLiteral("HOME"),
        };
        const QProcessEnvironment sysEnv = QProcessEnvironment::systemEnvironment();
        for (const QString &varName : sessionEnvVars) {
            const QString value = sysEnv.value(varName);
            if (!value.isEmpty()) {
                QJsonObject entry;
                entry[QStringLiteral("name")] = varName;
                entry[QStringLiteral("value")] = value;
                envArray.append(entry);
            }
        }

        // Tell the child MCP server which DBus service name to target so it
        // reaches THIS process's editor instance, not a different Kate process.
        {
            QJsonObject entry;
            entry[QStringLiteral("name")]  = QStringLiteral("KATECODE_DBUS_SERVICE");
            entry[QStringLiteral("value")] = QString(QStringLiteral("org.kde.katecode.editor.")
                                             + QString::number(QCoreApplication::applicationPid()));
            envArray.append(entry);
        }

        kateMcp[QStringLiteral("env")] = envArray;
        mcpServers.append(kateMcp);
        qDebug() << "[ACPSession] Added Kate MCP server:" << mcpServerPath;
    } else {
        qWarning() << "[ACPSession] Kate MCP server not found";
    }

    if (!m_mcpConfigPath.isEmpty()) {
        const QJsonArray externalServers = loadExternalMcpServers(m_mcpConfigPath);
        for (const QJsonValue &server : externalServers) {
            mcpServers.append(server);
        }
    } else {
        qDebug() << "[ACPSession] No MCP config path configured for this provider";
    }

    return mcpServers;
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
    params[QStringLiteral("mcpServers")] = buildMcpServers();

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
    // A resumed Codex/app-server thread still needs the client-provided MCP
    // definitions for this process, including Kate and provider JSON servers.
    params[QStringLiteral("mcpServers")] = buildMcpServers();

    // ACP session/load responses do not normally repeat the session id, so
    // retain the requested id until the response arrives.
    m_sessionLoadId = sessionId;
    m_sessionLoadRequestId = m_service->sendRequest(QStringLiteral("session/load"), params);
    qDebug() << "[ACPSession] Sent session/load request, id:" << m_sessionLoadRequestId;
}

void ACPSession::sendMessage(const QString &content, const QString &filePath, const QString &selection, const QList<ContextChunk> &contextChunks, const QList<ImageAttachment> &images)
{
    if (m_status != ConnectionStatus::Connected) {
        qWarning() << "[ACPSession] Cannot send message: not connected";
        return;
    }

    const bool isFirstMessage = (m_messageCounter == 0);

    // Emit the user message immediately so the UI updates regardless of
    // whether the prompt can be dispatched now or must be queued.
    Message userMsg;
    userMsg.id = QStringLiteral("msg_%1").arg(++m_messageCounter);
    userMsg.role = QStringLiteral("user");
    userMsg.timestamp = QDateTime::currentDateTime();
    userMsg.content = content;
    userMsg.images = images;
    Q_EMIT messageAdded(userMsg);
    m_transcript->recordMessage(userMsg);

    // If a session/prompt round-trip is already in flight, queue this prompt
    // instead of sending a concurrent request (which agents may mishandle).
    // The assistant placeholder is created only when the prompt is dispatched.
    if (isPromptRunning()) {
        m_promptQueue.append(QueuedPrompt{content, filePath, selection, contextChunks, images});
        qDebug() << "[ACPSession] Prompt busy; queued follow-up (" << m_promptQueue.size() << " queued)";
        return;
    }

    dispatchPrompt(content, filePath, selection, contextChunks, images, isFirstMessage);
}

void ACPSession::dispatchPrompt(const QString &content, const QString &filePath, const QString &selection, const QList<ContextChunk> &contextChunks, const QList<ImageAttachment> &images, bool isFirstMessage)
{
    // Create assistant placeholder for streaming — exactly one per dispatched turn.
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

    // Add file context as embedded resource if available.
    // Embedded-context (resource) blocks are only sent when the agent advertised
    // embeddedContext support in its promptCapabilities.
    if (m_supportsEmbeddedContext) {
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
    } else if (!filePath.isEmpty() || !contextChunks.isEmpty()) {
        qDebug() << "[ACPSession] Skipping embedded-context blocks: agent did not advertise embeddedContext capability";
    }

    // Image blocks are only sent when the agent advertised image support.
    if (m_supportsImage) {
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
    } else if (!images.isEmpty()) {
        qWarning() << "[ACPSession] Skipping" << images.size()
                   << "image(s): agent did not advertise image capability";
    }

    // Add user's actual message
    QJsonObject textBlock;
    textBlock[QStringLiteral("type")] = QStringLiteral("text");

    // On the first message, inject editor context and tool preferences.
    QString messageText = content;
    if (isFirstMessage) {
        messageText += QStringLiteral("\n\n<system-reminder>"
            "You are running inside the KDE Kate text editor via the Kate Code plugin. "
            "The files the user has open in Kate are your working context. "
            "File reads, edits, and writes should go through the kate MCP tools so the user "
            "can review changes directly in the editor. "
            "The workspace root is the current project directory.\n\n"
            "Use mcp__kate__katecode_documents when you need to see which files are open in Kate.\n\n"
            "In sessions with mcp__kate__katecode_read always use it instead of Read as it contains the most up-to-date contents.\n\n"
            "In sessions with mcp__kate__katecode_write always use it instead of Write as it will\n"
            "allow the user to conveniently review changes.\n\n"
            "In sessions with mcp__kate__katecode_edit always use it instead of Edit as it will\n"
            "allow the user to conveniently review changes."
            "</system-reminder>");
    }
    textBlock[QStringLiteral("text")] = messageText;
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

    // Select options are part of the base protocol. Explicitly advertise the
    // optional boolean form because provider session configuration can retain
    // and send native JSON booleans.
    QJsonObject configOptionCapabilities;
    configOptionCapabilities[QStringLiteral("boolean")] = QJsonObject();
    QJsonObject sessionCapabilities;
    sessionCapabilities[QStringLiteral("configOptions")] = configOptionCapabilities;
    capabilities[QStringLiteral("session")] = sessionCapabilities;

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

    // Configuration errors are non-fatal: report the failed option and keep
    // applying any remaining per-provider session settings.
    if (id == m_sessionConfigRequestId) {
        handleSessionConfigResponse(result, error);
        return;
    }

    // Interactive mode-change response (distinct from the startup config flow).
    if (id == m_interactiveModeRequestId) {
        handleInteractiveModeResponse(result, error);
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
        // Prompt completed — finish the streaming message.
        qDebug() << "[ACPSession] Prompt response received, finishing message:" << m_currentMessageId;
        if (!m_currentMessageId.isEmpty()) {
            // Record transcript here for agents that end the turn via the
            // session/prompt RESPONSE rather than agent_message_end.  If
            // agent_message_end already fired it cleared both fields, so this
            // path sees them empty and is a safe no-op (no double-record).
            if (!m_currentMessageContent.isEmpty() && m_transcript) {
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
        }
        m_promptRequestId = -1;

        // Flush the next queued prompt if one arrived while this turn was running.
        // isPromptRunning() is now false, so dispatchPrompt sends immediately
        // and creates exactly one new assistant placeholder.
        if (!m_promptQueue.isEmpty()) {
            const QueuedPrompt next = m_promptQueue.takeFirst();
            // Not the first message; the session is already established.
            dispatchPrompt(next.content, next.filePath, next.selection, next.contextChunks, next.images, false);
        }
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

    // Parse agent prompt capabilities so we can degrade gracefully on servers
    // that don't support all block types (e.g. Codex omits image/embeddedContext).
    const QJsonObject agentCaps = result[QStringLiteral("agentCapabilities")].toObject();
    const QJsonObject promptCaps = agentCaps[QStringLiteral("promptCapabilities")].toObject();
    m_supportsImage = promptCaps[QStringLiteral("image")].toBool(false);
    m_supportsEmbeddedContext = promptCaps[QStringLiteral("embeddedContext")].toBool(false);
    m_supportsPromptQueueing = agentCaps[QStringLiteral("_meta")].toObject()
                                    [QStringLiteral("claudeCode")].toObject()
                                    [QStringLiteral("promptQueueing")].toBool(false);

    qDebug() << "[ACPSession] Agent capabilities:"
             << "image=" << m_supportsImage
             << "embeddedContext=" << m_supportsEmbeddedContext
             << "promptQueueing=" << m_supportsPromptQueueing;

    // Don't automatically create session - let ChatWidget decide
    // whether to load an existing session or create a new one
    Q_EMIT initializeComplete();
}

void ACPSession::handleSessionNewResponse(int id, const QJsonObject &result)
{
    Q_UNUSED(id);
    m_sessionNewRequestId = -1;
    m_sessionId = result[QStringLiteral("sessionId")].toString();
    qDebug() << "[ACPSession] Session created with ID:" << m_sessionId;

    if (m_sessionId.isEmpty()) {
        qWarning() << "[ACPSession] ERROR: Session ID is empty! Full result:" << result;
        m_status = ConnectionStatus::Error;
        Q_EMIT errorOccurred(QStringLiteral("Failed to get session ID from ACP"));
        Q_EMIT statusChanged(m_status);
        return;
    }

    parseSessionSetupResult(result);
    beginSessionConfiguration(false);
}

void ACPSession::handleSessionLoadResponse(int id, const QJsonObject &result, const QJsonObject &error)
{
    Q_UNUSED(id);
    const QString requestedSessionId = m_sessionLoadId;
    m_sessionLoadId.clear();
    m_sessionLoadRequestId = -1;

    if (!error.isEmpty()) {
        m_sessionId.clear();
        QString errorMsg = error[QStringLiteral("message")].toString();
        qWarning() << "[ACPSession] Session load failed:" << errorMsg;
        Q_EMIT sessionLoadFailed(errorMsg);
        return;
    }

    // session/load returns mode/config state; unlike session/new, the standard
    // response does not include sessionId. Accept it when supplied by a legacy
    // agent, otherwise use the id from the request.
    m_sessionId = result[QStringLiteral("sessionId")].toString(requestedSessionId);
    qDebug() << "[ACPSession] Session loaded with ID:" << m_sessionId;

    if (m_sessionId.isEmpty()) {
        qWarning() << "[ACPSession] ERROR: Session ID is empty after load!";
        Q_EMIT sessionLoadFailed(QStringLiteral("Empty session ID in response"));
        return;
    }

    parseSessionSetupResult(result);
    beginSessionConfiguration(true);
}

void ACPSession::parseSessionSetupResult(const QJsonObject &result)
{
    const QJsonObject modes = result[QStringLiteral("modes")].toObject();
    if (!modes.isEmpty()) {
        m_availableModes = modes[QStringLiteral("availableModes")].toArray();
        m_currentMode = modes[QStringLiteral("currentModeId")].toString();
    } else {
        // Compatibility with older agents that returned mode fields at the top level.
        m_availableModes = result[QStringLiteral("availableModes")].toArray();
        m_currentMode = result[QStringLiteral("currentModeId")].toString();
    }

    updateSessionConfigOptions(result[QStringLiteral("configOptions")].toArray(), false);

    qDebug() << "[ACPSession] Available modes:" << m_availableModes.size();
    qDebug() << "[ACPSession] Current mode:" << m_currentMode;
    qDebug() << "[ACPSession] Available config options:" << m_availableConfigOptions.size();
}

void ACPSession::updateSessionConfigOptions(const QJsonArray &configOptions, bool emitChanges)
{
    m_availableConfigOptions = configOptions;

    for (const QJsonValue &value : configOptions) {
        const QJsonObject option = value.toObject();
        if (option[QStringLiteral("id")].toString() != QStringLiteral("mode")) {
            continue;
        }

        QJsonArray modes;
        auto appendMode = [&modes](const QJsonObject &choice) {
            const QString id = choice[QStringLiteral("value")].toString();
            if (id.isEmpty()) {
                return;
            }
            QJsonObject mode;
            mode[QStringLiteral("id")] = id;
            mode[QStringLiteral("name")] = choice[QStringLiteral("name")];
            mode[QStringLiteral("description")] = choice[QStringLiteral("description")];
            modes.append(mode);
        };

        // ACP select choices may be a flat array or grouped one level deep.
        for (const QJsonValue &choiceValue : option[QStringLiteral("options")].toArray()) {
            const QJsonObject choice = choiceValue.toObject();
            if (choice.contains(QStringLiteral("options"))) {
                for (const QJsonValue &nestedValue : choice[QStringLiteral("options")].toArray()) {
                    appendMode(nestedValue.toObject());
                }
            } else {
                appendMode(choice);
            }
        }

        if (!modes.isEmpty()) {
            m_availableModes = modes;
        }
        m_currentMode = option[QStringLiteral("currentValue")].toString(m_currentMode);

        if (emitChanges) {
            Q_EMIT modesAvailable(m_availableModes);
            if (!m_currentMode.isEmpty()) {
                Q_EMIT modeChanged(m_currentMode);
            }
        }
        break;
    }
}

void ACPSession::beginSessionConfiguration(bool loadedSession)
{
    m_configuringLoadedSession = loadedSession;
    m_pendingSessionConfigKeys.clear();
    m_currentSessionConfigKey.clear();

    QSet<QString> advertised;
    for (const QJsonValue &value : m_availableConfigOptions) {
        const QString id = value.toObject()[QStringLiteral("id")].toString();
        if (!id.isEmpty()) {
            advertised.insert(id);
        }
    }

    QStringList remainingKeys = m_sessionConfig.keys();
    QStringList configuredKeys;
    // Model must be selected before reasoning effort because changing model can
    // replace the advertised effort choices.
    static const QStringList preferredOrder = {
        QStringLiteral("model"),
        QStringLiteral("reasoning_effort"),
        QStringLiteral("mode"),
    };
    for (const QString &key : preferredOrder) {
        if (remainingKeys.removeOne(key)) {
            configuredKeys.append(key);
        }
    }
    configuredKeys.append(remainingKeys);

    QStringList skipped;
    for (const QString &key : configuredKeys) {
        if (advertised.contains(key)) {
            m_pendingSessionConfigKeys.append(key);
        } else {
            skipped.append(key);
        }
    }

    if (!skipped.isEmpty()) {
        const QString message = QStringLiteral("ACP agent did not advertise session configuration option(s): %1")
                                    .arg(skipped.join(QStringLiteral(", ")));
        qWarning() << "[ACPSession]" << message;
        Q_EMIT errorOccurred(message);
    }

    sendNextSessionConfigOption();
}

void ACPSession::sendNextSessionConfigOption()
{
    if (m_pendingSessionConfigKeys.isEmpty()) {
        completeSessionSetup(m_configuringLoadedSession);
        return;
    }

    m_currentSessionConfigKey = m_pendingSessionConfigKeys.takeFirst();
    QJsonObject params;
    params[QStringLiteral("sessionId")] = m_sessionId;
    params[QStringLiteral("configId")] = m_currentSessionConfigKey;
    const QJsonValue value = m_sessionConfig.value(m_currentSessionConfigKey);
    params[QStringLiteral("value")] = value;
    if (value.isBool()) {
        // Required by the ACP boolean config-option request variant.
        params[QStringLiteral("type")] = QStringLiteral("boolean");
    }
    m_sessionConfigRequestId = m_service->sendRequest(QStringLiteral("session/set_config_option"), params);
    if (m_sessionConfigRequestId < 0) {
        qWarning() << "[ACPSession] Failed to send session config option" << m_currentSessionConfigKey;
        sendNextSessionConfigOption();
    }
}

void ACPSession::handleSessionConfigResponse(const QJsonObject &result, const QJsonObject &error)
{
    if (!error.isEmpty()) {
        const QString message = QStringLiteral("Failed to apply ACP session configuration %1: %2")
                                    .arg(m_currentSessionConfigKey,
                                         error[QStringLiteral("message")].toString(QStringLiteral("unknown error")));
        qWarning() << "[ACPSession]" << message;
        Q_EMIT errorOccurred(message);
    } else if (result.contains(QStringLiteral("configOptions"))) {
        updateSessionConfigOptions(result[QStringLiteral("configOptions")].toArray(), false);
    }

    m_sessionConfigRequestId = -1;
    m_currentSessionConfigKey.clear();
    sendNextSessionConfigOption();
}

void ACPSession::handleInteractiveModeResponse(const QJsonObject &result, const QJsonObject &error)
{
    // Always clear the in-flight state first; we capture what we need locally.
    const QString attempted = m_pendingModeValue;
    m_interactiveModeRequestId = -1;
    m_pendingModeValue.clear();

    if (!error.isEmpty()) {
        const QString errMsg = error[QStringLiteral("message")].toString(
            QStringLiteral("unknown error"));
        qWarning() << "[ACPSession] Interactive mode change to" << attempted
                   << "rejected by agent:" << errMsg;
        // Roll the dropdown back to the last confirmed mode so the UI is not
        // left showing a mode the agent refused.
        Q_EMIT modeChanged(m_currentMode);
    } else {
        if (result.contains(QStringLiteral("configOptions"))) {
            // Modern response: let updateSessionConfigOptions() drive m_currentMode
            // and emit modesAvailable/modeChanged so the dropdown reflects the
            // confirmed state returned by the agent.
            updateSessionConfigOptions(
                result[QStringLiteral("configOptions")].toArray(), true);
        } else {
            // Non-conforming success (e.g. legacy session/set_mode with no body):
            // accept the attempted value as confirmed and notify the UI.
            qDebug() << "[ACPSession] Interactive mode change to" << attempted
                     << "succeeded (no configOptions in response)";
            m_currentMode = attempted;
            Q_EMIT modeChanged(m_currentMode);
        }
    }

    // If the user changed the dropdown again while this request was in flight,
    // send the coalesced latest selection now.
    if (!m_queuedModeValue.isEmpty()) {
        const QString queued = m_queuedModeValue;
        m_queuedModeValue.clear();
        if (queued != m_currentMode) {
            setMode(queued);
        }
    }
}

void ACPSession::completeSessionSetup(bool loadedSession)
{
    m_status = ConnectionStatus::Connected;

    // TranscriptWriter appends when a real ACP session is resumed.
    m_transcript->startSession(m_sessionId, m_workingDir);

    Q_EMIT modesAvailable(m_availableModes);
    if (!m_currentMode.isEmpty()) {
        Q_EMIT modeChanged(m_currentMode);
    }

    qDebug() << "[ACPSession]" << (loadedSession ? "Loaded" : "New")
             << "session ready with configured ACP options";
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

        // Final fallback: infer from toolCallId prefix (e.g., Gemini "run_shell_command-<ts>")
        if (!isReadTool(toolCall.name) && !isWriteTool(toolCall.name) &&
            !isEditTool(toolCall.name) && !isBashTool(toolCall.name)) {
            QString inferred = inferToolNameFromId(toolCall.id);
            if (!inferred.isEmpty()) {
                toolCall.name = inferred;
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

        // Fallback: Extract edit data from rawInput for MCP tools (e.g., mcp__kate__katecode_edit)
        // MCP tools use old_string/new_string in rawInput, not diff objects in content array
        if (toolCall.edits.isEmpty() && isEditTool(toolCall.name)) {
            QString oldStr = toolCall.input[QStringLiteral("old_string")].toString();
            QString newStr = toolCall.input[QStringLiteral("new_string")].toString();

            if (!oldStr.isEmpty() || !newStr.isEmpty()) {
                EditDiff edit;
                edit.oldText = oldStr;
                edit.newText = newStr;
                edit.filePath = toolCall.filePath;
                toolCall.edits.append(edit);
                toolCall.oldText = oldStr;
                toolCall.newText = newStr;

                qDebug() << "[ACPSession] Edit from rawInput - old:" << oldStr.length()
                         << "chars, new:" << newStr.length() << "chars";
            }
        }

        // Fallback: Extract write content from rawInput for MCP tools (e.g., mcp__kate__katecode_write)
        if (isWriteTool(toolCall.name) && toolCall.newText.isEmpty()) {
            QString content = toolCall.input[QStringLiteral("content")].toString();
            if (!content.isEmpty()) {
                toolCall.newText = content;
                qDebug() << "[ACPSession] Write content from rawInput:" << content.length() << "chars";
            }
        }

        qDebug() << "[ACPSession] Tool call - id:" << toolCall.id
                 << "name:" << toolCall.name << "status:" << toolCall.status
                 << "file:" << toolCall.filePath << "operation:" << toolCall.operationType;

        // Store tool input for later lookup (e.g., to check ExitPlanMode parameters)
        m_toolCallInputs[toolCall.id] = toolCall.input;

        if (!m_currentMessageId.isEmpty()) {
            Q_EMIT toolCallAdded(m_currentMessageId, toolCall);
            m_transcript->recordToolCall(toolCall);

            // Handle already-completed tool calls (some agents send tool_call with status=completed
            // and rawOutput in a single event, without a separate tool_call_update)
            if (toolCall.status == QStringLiteral("completed")) {
                QString result;
                QJsonValue rawOutputValue = update[QStringLiteral("rawOutput")];

                // Check if rawOutput is an object (bash tool format with exitCode, stdout, stderr)
                if (rawOutputValue.isObject()) {
                    QJsonObject rawObj = rawOutputValue.toObject();
                    if (rawObj.contains(QStringLiteral("stdout")) || rawObj.contains(QStringLiteral("stderr"))) {
                        QString stdoutText = rawObj[QStringLiteral("stdout")].toString();
                        QString stderrText = rawObj[QStringLiteral("stderr")].toString();
                        int exitCode = rawObj[QStringLiteral("exitCode")].toInt();

                        QStringList parts;
                        if (!stdoutText.isEmpty()) {
                            parts.append(stdoutText);
                        }
                        if (!stderrText.isEmpty()) {
                            if (!stdoutText.isEmpty()) {
                                parts.append(QStringLiteral("stderr:\n") + stderrText);
                            } else {
                                parts.append(stderrText);
                            }
                        }
                        if (!parts.isEmpty()) {
                            result = parts.join(QStringLiteral("\n"));
                        } else if (exitCode != 0) {
                            result = QStringLiteral("Exit code: %1").arg(exitCode);
                        }
                        qDebug() << "[ACPSession] tool_call completed inline with bash rawOutput - exitCode:"
                                 << exitCode << "stdout len:" << stdoutText.length();
                    }
                }

                // Fall back to rawOutput as string
                if (result.isEmpty()) {
                    QString rawOutput = rawOutputValue.toString();
                    if (!rawOutput.isEmpty()) {
                        // Try parsing as JSON
                        QJsonDocument rawDoc = QJsonDocument::fromJson(rawOutput.toUtf8());
                        if (!rawDoc.isNull() && rawDoc.isObject()) {
                            QJsonObject rawObj = rawDoc.object();
                            QJsonValue contentValue = rawObj[QStringLiteral("content")];
                            if (contentValue.isString()) {
                                result = contentValue.toString();
                            } else if (contentValue.isArray()) {
                                QJsonArray contentArray = contentValue.toArray();
                                QStringList texts;
                                for (const QJsonValue &item : contentArray) {
                                    if (item.isObject()) {
                                        QString text = item.toObject()[QStringLiteral("text")].toString();
                                        if (!text.isEmpty()) {
                                            texts.append(text);
                                        }
                                    }
                                }
                                result = texts.join(QString());
                            }
                        }
                        if (result.isEmpty()) {
                            result = rawOutput;  // Use as-is
                        }
                        qDebug() << "[ACPSession] tool_call completed inline - result length:" << result.length();
                    }
                }

                // Emit update if we extracted a result
                if (!result.isEmpty()) {
                    Q_EMIT toolCallUpdated(m_currentMessageId, toolCall.id, toolCall.status, result,
                                          toolCall.filePath, toolCall.name);
                    m_transcript->recordToolUpdate(toolCall.id, toolCall.status, result);
                }
            }
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
        // Bash tools may return rawOutput as an object with exitCode, stdout, stderr
        if (result.isEmpty() || (!update[QStringLiteral("rawOutput")].isUndefined() && result.length() < 200)) {
            QJsonValue rawOutputValue = update[QStringLiteral("rawOutput")];

            // Check if rawOutput is an object (bash tool format with exitCode, stdout, stderr)
            if (rawOutputValue.isObject()) {
                QJsonObject rawObj = rawOutputValue.toObject();
                // Check for bash-style output (has stdout or stderr fields)
                if (rawObj.contains(QStringLiteral("stdout")) || rawObj.contains(QStringLiteral("stderr"))) {
                    QString stdoutText = rawObj[QStringLiteral("stdout")].toString();
                    QString stderrText = rawObj[QStringLiteral("stderr")].toString();
                    int exitCode = rawObj[QStringLiteral("exitCode")].toInt();

                    // Combine stdout and stderr for display
                    QStringList parts;
                    if (!stdoutText.isEmpty()) {
                        parts.append(stdoutText);
                    }
                    if (!stderrText.isEmpty()) {
                        // Prefix stderr if there's also stdout
                        if (!stdoutText.isEmpty()) {
                            parts.append(QStringLiteral("stderr:\n") + stderrText);
                        } else {
                            parts.append(stderrText);
                        }
                    }
                    if (!parts.isEmpty()) {
                        result = parts.join(QStringLiteral("\n"));
                    } else if (exitCode != 0) {
                        // No output but non-zero exit - report the exit code
                        result = QStringLiteral("Exit code: %1").arg(exitCode);
                    }
                    qDebug() << "[ACPSession] Bash rawOutput - exitCode:" << exitCode
                             << "stdout len:" << stdoutText.length()
                             << "stderr len:" << stderrText.length();
                }
            }

            // Fall back to rawOutput as string (original format)
            QString rawOutput = rawOutputValue.toString();
            if (!rawOutput.isEmpty() && result.isEmpty()) {
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
                        // Handle both string and array formats for content
                        QJsonValue contentValue = rawObj[QStringLiteral("content")];
                        QString fileContent;
                        if (contentValue.isString()) {
                            fileContent = contentValue.toString();
                        } else if (contentValue.isArray()) {
                            // Anthropic tool result format: [{"type":"text","text":"..."}]
                            QJsonArray contentArray = contentValue.toArray();
                            QStringList texts;
                            for (const QJsonValue &item : contentArray) {
                                if (item.isObject()) {
                                    QJsonObject block = item.toObject();
                                    QString text = block[QStringLiteral("text")].toString();
                                    if (!text.isEmpty()) {
                                        texts.append(text);
                                    }
                                }
                            }
                            fileContent = texts.join(QString());
                        }
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

        // Infer tool name if not provided by _meta (needed when tool_call event was skipped)
        if (toolName.isEmpty() || (!isReadTool(toolName) && !isWriteTool(toolName) &&
            !isEditTool(toolName) && !isBashTool(toolName))) {
            QString kind = update[QStringLiteral("kind")].toString();
            if (kind == QStringLiteral("execute")) {
                toolName = QStringLiteral("Bash");
            }
        }
        if (toolName.isEmpty() || (!isReadTool(toolName) && !isWriteTool(toolName) &&
            !isEditTool(toolName) && !isBashTool(toolName))) {
            QString inferred = inferToolNameFromId(toolCallId);
            if (!inferred.isEmpty()) {
                toolName = inferred;
            }
        }

        qDebug() << "[ACPSession] Tool call update - id:" << toolCallId
                 << "status:" << status << "operation:" << operationType
                 << "toolName:" << toolName << "result length:" << result.length();

        if (!m_currentMessageId.isEmpty()) {
            // Link terminal to tool call if we found one (vibe-acp sends terminal in tool_call_update)
            if (!terminalId.isEmpty()) {
                Q_EMIT toolCallTerminalIdSet(m_currentMessageId, toolCallId, terminalId);
            }

            // Only emit update if we have a result OR status changed
            // (Don't overwrite good results with empty ones from status-only updates)
            if (!result.isEmpty() || !status.isEmpty()) {
                Q_EMIT toolCallUpdated(m_currentMessageId, toolCallId, status, result, updateFilePath, toolName);
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
    else if (updateType == QStringLiteral("config_option_update")) {
        updateSessionConfigOptions(update[QStringLiteral("configOptions")].toArray(),
                                   m_status == ConnectionStatus::Connected);
    }
    else if (updateType == QStringLiteral("current_mode_update")) {
        // Agent changed the mode
        const QString newMode = update[QStringLiteral("currentModeId")].toString(
            update[QStringLiteral("modeId")].toString());
        qDebug() << "[ACPSession] Mode changed to:" << newMode;
        m_currentMode = newMode;
        if (!newMode.isEmpty()) {
            Q_EMIT modeChanged(newMode);
        }
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
    else if (updateType == QStringLiteral("session_info_update")) {
        // Codex uses this to report thread-level status changes (e.g. upstream stream failures).
        const QString threadStatus =
            update[QStringLiteral("_meta")].toObject()
                  [QStringLiteral("codex")].toObject()
                  [QStringLiteral("threadStatus")].toObject()
                  [QStringLiteral("type")].toString();

        if (threadStatus == QStringLiteral("systemError") ||
            threadStatus == QStringLiteral("error")) {
            qWarning() << "[ACPSession] session_info_update: threadStatus =" << threadStatus
                       << "— signalling sessionError";
            // Do NOT clear m_currentMessageId or m_promptRequestId: the trailing
            // agent_message_chunk and prompt response still finalise the turn normally.
            Q_EMIT sessionError(QStringLiteral(
                "The agent reported a system error; this turn failed. "
                "Any details (including an OpenAI request ID) follow below. "
                "You can send another message to retry."));
        } else {
            qDebug() << "[ACPSession] session_info_update: threadStatus =" << threadStatus;
        }
    }
    else {
        // Log any unrecognised update type so they are not silently dropped.
        qDebug() << "[ACPSession] Unhandled session/update type:" << updateType;
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

    // Get tool name - try _meta.claudeCode.toolName first (most reliable)
    QJsonObject meta = toolCall[QStringLiteral("_meta")].toObject();
    QJsonObject claudeCode = meta[QStringLiteral("claudeCode")].toObject();
    request.toolName = claudeCode[QStringLiteral("toolName")].toString();

    // Fall back to name field
    if (request.toolName.isEmpty()) {
        request.toolName = toolCall[QStringLiteral("name")].toString();
    }
    if (request.toolName.isEmpty()) {
        request.toolName = toolCall[QStringLiteral("toolName")].toString();
    }

    // Infer tool type from kind field or title if not a known tool
    // (same logic as tool_call handler - needed for vibe-acp / Gemini)
    QString title = toolCall[QStringLiteral("title")].toString();
    QString kind = toolCall[QStringLiteral("kind")].toString();

    if (request.toolName.isEmpty() ||
        (!isReadTool(request.toolName) && !isWriteTool(request.toolName) &&
         !isEditTool(request.toolName) && !isBashTool(request.toolName))) {
        // Check kind field first - more reliable than title matching
        if (kind == QStringLiteral("execute")) {
            request.toolName = QStringLiteral("Bash");
        }
    }
    if (request.toolName.isEmpty() ||
        (!isReadTool(request.toolName) && !isWriteTool(request.toolName) &&
         !isEditTool(request.toolName) && !isBashTool(request.toolName))) {
        if (title.startsWith(QStringLiteral("Reading "))) {
            request.toolName = QStringLiteral("Read");
        } else if (title.startsWith(QStringLiteral("Editing "))) {
            request.toolName = QStringLiteral("Edit");
        } else if (title.startsWith(QStringLiteral("Writing "))) {
            request.toolName = QStringLiteral("Write");
        } else if (title.startsWith(QStringLiteral("Patching "))) {
            request.toolName = QStringLiteral("Edit");
        } else if (title.contains(QStringLiteral("bash")) || title.contains(QStringLiteral("Bash")) ||
                   title.startsWith(QStringLiteral("Running "))) {
            request.toolName = QStringLiteral("Bash");
        }
    }

    // Fallback: infer from toolCallId prefix (e.g., Gemini "run_shell_command-<ts>")
    if (request.toolName.isEmpty() ||
        (!isReadTool(request.toolName) && !isWriteTool(request.toolName) &&
         !isEditTool(request.toolName) && !isBashTool(request.toolName))) {
        QString inferred = inferToolNameFromId(request.id);
        if (!inferred.isEmpty()) {
            request.toolName = inferred;
        }
    }

    // If still no recognized tool name, use the title as-is
    if (request.toolName.isEmpty()) {
        request.toolName = title;
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
