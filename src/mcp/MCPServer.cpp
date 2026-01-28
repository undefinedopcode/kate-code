/*
    SPDX-License-Identifier: MIT
    SPDX-FileCopyrightText: 2025 Kate Code contributors
*/

#include "MCPServer.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QJsonDocument>

MCPServer::MCPServer() = default;

QJsonObject MCPServer::handleMessage(const QJsonObject &msg)
{
    const QString method = msg[QStringLiteral("method")].toString();
    const int id = msg[QStringLiteral("id")].toInt(-1);
    const QJsonObject params = msg[QStringLiteral("params")].toObject();

    // Notifications have no id — no response needed
    if (id < 0) {
        return {};
    }

    if (method == QStringLiteral("initialize")) {
        return handleInitialize(id, params);
    } else if (method == QStringLiteral("tools/list")) {
        return handleToolsList(id, params);
    } else if (method == QStringLiteral("tools/call")) {
        return handleToolsCall(id, params);
    }

    return makeErrorResponse(id, -32601, QStringLiteral("Method not found: %1").arg(method));
}

QJsonObject MCPServer::handleInitialize(int id, const QJsonObject &params)
{
    Q_UNUSED(params);
    m_initialized = true;

    QJsonObject serverInfo;
    serverInfo[QStringLiteral("name")] = QStringLiteral("kate-mcp-server");
    serverInfo[QStringLiteral("version")] = QStringLiteral("0.1.0");

    QJsonObject capabilities;
    capabilities[QStringLiteral("tools")] = QJsonObject();

    QJsonObject result;
    result[QStringLiteral("protocolVersion")] = QStringLiteral("2024-11-05");
    result[QStringLiteral("serverInfo")] = serverInfo;
    result[QStringLiteral("capabilities")] = capabilities;

    return makeResponse(id, result);
}

QJsonObject MCPServer::handleToolsList(int id, const QJsonObject &params)
{
    Q_UNUSED(params);

    // kate_test tool definition
    QJsonObject messageProp;
    messageProp[QStringLiteral("type")] = QStringLiteral("string");
    messageProp[QStringLiteral("description")] = QStringLiteral("The message to echo back");

    QJsonObject properties;
    properties[QStringLiteral("message")] = messageProp;

    QJsonObject inputSchema;
    inputSchema[QStringLiteral("type")] = QStringLiteral("object");
    inputSchema[QStringLiteral("properties")] = properties;
    inputSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("message")};

    QJsonObject kateTestTool;
    kateTestTool[QStringLiteral("name")] = QStringLiteral("kate_test");
    kateTestTool[QStringLiteral("description")] =
        QStringLiteral("A test tool that echoes input. Use this to verify the Kate MCP server is working.");
    kateTestTool[QStringLiteral("inputSchema")] = inputSchema;

    // Read-only annotation for test tool
    QJsonObject testAnnotations;
    testAnnotations[QStringLiteral("readOnlyHint")] = true;
    testAnnotations[QStringLiteral("destructiveHint")] = false;
    kateTestTool[QStringLiteral("annotations")] = testAnnotations;

    // katecode_documents tool definition
    QJsonObject docsTool;
    docsTool[QStringLiteral("name")] = QStringLiteral("katecode_documents");
    docsTool[QStringLiteral("description")] =
        QStringLiteral("Returns a list of all documents currently open in the Kate editor.");

    QJsonObject docsSchema;
    docsSchema[QStringLiteral("type")] = QStringLiteral("object");
    docsSchema[QStringLiteral("properties")] = QJsonObject();
    docsTool[QStringLiteral("inputSchema")] = docsSchema;

    // Read-only annotation for documents tool
    QJsonObject docsAnnotations;
    docsAnnotations[QStringLiteral("readOnlyHint")] = true;
    docsAnnotations[QStringLiteral("destructiveHint")] = false;
    docsTool[QStringLiteral("annotations")] = docsAnnotations;

    // katecode_read tool definition
    QJsonObject readTool;
    readTool[QStringLiteral("name")] = QStringLiteral("katecode_read");
    readTool[QStringLiteral("description")] =
        QStringLiteral("Reads the content of a file. If the file is open in Kate, returns the current buffer content (which may have unsaved changes). Otherwise reads from disk.\n\nIn sessions with mcp__kate__katecode_read always use it instead of Read or mcp__acp__Read, as it contains the most up-to-date contents provided by the editor.");

    QJsonObject readPathProp;
    readPathProp[QStringLiteral("type")] = QStringLiteral("string");
    readPathProp[QStringLiteral("description")] = QStringLiteral("The absolute path to the file to read");

    QJsonObject readProps;
    readProps[QStringLiteral("file_path")] = readPathProp;

    QJsonObject readSchema;
    readSchema[QStringLiteral("type")] = QStringLiteral("object");
    readSchema[QStringLiteral("properties")] = readProps;
    readSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("file_path")};
    readTool[QStringLiteral("inputSchema")] = readSchema;

    // Read-only annotation for read tool
    QJsonObject readAnnotations;
    readAnnotations[QStringLiteral("readOnlyHint")] = true;
    readAnnotations[QStringLiteral("destructiveHint")] = false;
    readTool[QStringLiteral("annotations")] = readAnnotations;

    // katecode_edit tool definition
    QJsonObject editTool;
    editTool[QStringLiteral("name")] = QStringLiteral("katecode_edit");
    editTool[QStringLiteral("description")] =
        QStringLiteral("Edits a file by replacing old_string with new_string. The old_string must be unique in the file. Opens the file in Kate if not already open.\n\nIn sessions with mcp__kate__katecode_edit always use it instead of Edit or mcp__acp__Edit, as it will update the editor buffer directly.");

    QJsonObject editPathProp;
    editPathProp[QStringLiteral("type")] = QStringLiteral("string");
    editPathProp[QStringLiteral("description")] = QStringLiteral("The absolute path to the file to edit");

    QJsonObject editOldProp;
    editOldProp[QStringLiteral("type")] = QStringLiteral("string");
    editOldProp[QStringLiteral("description")] = QStringLiteral("The text to replace (must be unique in the file)");

    QJsonObject editNewProp;
    editNewProp[QStringLiteral("type")] = QStringLiteral("string");
    editNewProp[QStringLiteral("description")] = QStringLiteral("The replacement text");

    QJsonObject editProps;
    editProps[QStringLiteral("file_path")] = editPathProp;
    editProps[QStringLiteral("old_string")] = editOldProp;
    editProps[QStringLiteral("new_string")] = editNewProp;

    QJsonObject editSchema;
    editSchema[QStringLiteral("type")] = QStringLiteral("object");
    editSchema[QStringLiteral("properties")] = editProps;
    editSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("file_path"), QStringLiteral("old_string"), QStringLiteral("new_string")};
    editTool[QStringLiteral("inputSchema")] = editSchema;

    // Destructive annotation for edit tool (modifies files)
    QJsonObject editAnnotations;
    editAnnotations[QStringLiteral("readOnlyHint")] = false;
    editAnnotations[QStringLiteral("destructiveHint")] = true;
    editAnnotations[QStringLiteral("idempotentHint")] = false;
    editTool[QStringLiteral("annotations")] = editAnnotations;

    // katecode_write tool definition
    QJsonObject writeTool;
    writeTool[QStringLiteral("name")] = QStringLiteral("katecode_write");
    writeTool[QStringLiteral("description")] =
        QStringLiteral("Writes content to a file. If the file is open in Kate, updates the buffer. Otherwise creates or overwrites the file.\n\nIn sessions with mcp__kate__katecode_write always use it instead of Write or mcp__acp__Write, as it will update the editor buffer directly.");

    QJsonObject writePathProp;
    writePathProp[QStringLiteral("type")] = QStringLiteral("string");
    writePathProp[QStringLiteral("description")] = QStringLiteral("The absolute path to the file to write");

    QJsonObject writeContentProp;
    writeContentProp[QStringLiteral("type")] = QStringLiteral("string");
    writeContentProp[QStringLiteral("description")] = QStringLiteral("The content to write to the file");

    QJsonObject writeProps;
    writeProps[QStringLiteral("file_path")] = writePathProp;
    writeProps[QStringLiteral("content")] = writeContentProp;

    QJsonObject writeSchema;
    writeSchema[QStringLiteral("type")] = QStringLiteral("object");
    writeSchema[QStringLiteral("properties")] = writeProps;
    writeSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("file_path"), QStringLiteral("content")};
    writeTool[QStringLiteral("inputSchema")] = writeSchema;

    // Destructive annotation for write tool (modifies/creates files)
    QJsonObject writeAnnotations;
    writeAnnotations[QStringLiteral("readOnlyHint")] = false;
    writeAnnotations[QStringLiteral("destructiveHint")] = true;
    writeAnnotations[QStringLiteral("idempotentHint")] = true;  // Writing same content twice = same result
    writeTool[QStringLiteral("annotations")] = writeAnnotations;

    QJsonObject result;
    result[QStringLiteral("tools")] = QJsonArray{kateTestTool, docsTool, readTool, editTool, writeTool};

    return makeResponse(id, result);
}

QJsonObject MCPServer::handleToolsCall(int id, const QJsonObject &params)
{
    const QString toolName = params[QStringLiteral("name")].toString();
    const QJsonObject arguments = params[QStringLiteral("arguments")].toObject();

    if (toolName == QStringLiteral("kate_test")) {
        return makeResponse(id, executeKateTest(arguments));
    } else if (toolName == QStringLiteral("katecode_documents")) {
        return makeResponse(id, executeDocuments(arguments));
    } else if (toolName == QStringLiteral("katecode_read")) {
        return makeResponse(id, executeRead(arguments));
    } else if (toolName == QStringLiteral("katecode_edit")) {
        return makeResponse(id, executeEdit(arguments));
    } else if (toolName == QStringLiteral("katecode_write")) {
        return makeResponse(id, executeWrite(arguments));
    }

    return makeErrorResponse(id, -32602, QStringLiteral("Unknown tool: %1").arg(toolName));
}

QJsonObject MCPServer::executeKateTest(const QJsonObject &arguments)
{
    const QString message = arguments[QStringLiteral("message")].toString();

    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = QStringLiteral("Kate MCP echo: %1").arg(message);

    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    return result;
}

QJsonObject MCPServer::executeDocuments(const QJsonObject &arguments)
{
    Q_UNUSED(arguments);

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());

    if (!iface.isValid()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = QStringLiteral("Error: Could not connect to Kate editor DBus service.");
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    QDBusReply<QStringList> reply = iface.call(QStringLiteral("listDocuments"));
    if (!reply.isValid()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message());
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    const QStringList docs = reply.value();
    QString text;
    if (docs.isEmpty()) {
        text = QStringLiteral("No documents currently open in Kate.");
    } else {
        text = QStringLiteral("Open documents (%1):\n").arg(docs.size());
        for (const QString &doc : docs) {
            text += QStringLiteral("  %1\n").arg(doc);
        }
    }

    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = text;

    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    return result;
}

QJsonObject MCPServer::makeResponse(int id, const QJsonObject &result)
{
    QJsonObject response;
    response[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    response[QStringLiteral("id")] = id;
    response[QStringLiteral("result")] = result;
    return response;
}

QJsonObject MCPServer::makeErrorResponse(int id, int code, const QString &message)
{
    QJsonObject error;
    error[QStringLiteral("code")] = code;
    error[QStringLiteral("message")] = message;

    QJsonObject response;
    response[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    response[QStringLiteral("id")] = id;
    response[QStringLiteral("error")] = error;
    return response;
}

QJsonObject MCPServer::executeRead(const QJsonObject &arguments)
{
    const QString filePath = arguments[QStringLiteral("file_path")].toString();

    if (filePath.isEmpty()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = QStringLiteral("Error: file_path is required");
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());

    if (!iface.isValid()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = QStringLiteral("Error: Could not connect to Kate editor DBus service.");
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    QDBusReply<QString> reply = iface.call(QStringLiteral("readDocument"), filePath);
    if (!reply.isValid()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message());
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    const QString content = reply.value();
    if (content.startsWith(QStringLiteral("ERROR:"))) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = content;
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = content;

    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    return result;
}

QJsonObject MCPServer::executeEdit(const QJsonObject &arguments)
{
    const QString filePath = arguments[QStringLiteral("file_path")].toString();
    const QString oldString = arguments[QStringLiteral("old_string")].toString();
    const QString newString = arguments[QStringLiteral("new_string")].toString();

    if (filePath.isEmpty() || oldString.isEmpty()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = QStringLiteral("Error: file_path and old_string are required");
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());

    if (!iface.isValid()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = QStringLiteral("Error: Could not connect to Kate editor DBus service.");
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    QDBusReply<QString> reply = iface.call(QStringLiteral("editDocument"), filePath, oldString, newString);
    if (!reply.isValid()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message());
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    const QString response = reply.value();
    bool isError = response.startsWith(QStringLiteral("ERROR:"));

    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = response;

    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    if (isError) {
        result[QStringLiteral("isError")] = true;
    }
    return result;
}

QJsonObject MCPServer::executeWrite(const QJsonObject &arguments)
{
    const QString filePath = arguments[QStringLiteral("file_path")].toString();
    const QString content = arguments[QStringLiteral("content")].toString();

    if (filePath.isEmpty()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = QStringLiteral("Error: file_path is required");
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());

    if (!iface.isValid()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = QStringLiteral("Error: Could not connect to Kate editor DBus service.");
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    QDBusReply<QString> reply = iface.call(QStringLiteral("writeDocument"), filePath, content);
    if (!reply.isValid()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message());
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        result[QStringLiteral("isError")] = true;
        return result;
    }

    const QString response = reply.value();
    bool isError = response.startsWith(QStringLiteral("ERROR:"));

    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = response;

    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    if (isError) {
        result[QStringLiteral("isError")] = true;
    }
    return result;
}
