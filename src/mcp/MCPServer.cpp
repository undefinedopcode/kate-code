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

    // katecode_ask_user tool definition
    QJsonObject askUserTool;
    askUserTool[QStringLiteral("name")] = QStringLiteral("katecode_ask_user");
    askUserTool[QStringLiteral("description")] =
        QStringLiteral("Ask the user 1-4 structured questions with selectable options. "
                       "Use this to gather clarifications, preferences, or decisions from the user. "
                       "Each question has a header (≤12 chars, used as answer key), question text, "
                       "multiSelect flag (checkboxes vs radio buttons), and 2-4 options. "
                       "An 'Other' option is automatically added for custom text input. "
                       "Returns answers as JSON object keyed by question headers.\n\n"
                       "In sessions with mcp__kate__katecode_ask_user always use it instead of AskUserQuestion, "
                       "as it will integrate with the editor and allow easy user feedback.");

    // Option schema
    QJsonObject optionLabelProp;
    optionLabelProp[QStringLiteral("type")] = QStringLiteral("string");
    optionLabelProp[QStringLiteral("description")] = QStringLiteral("Display text for the option (1-5 words)");

    QJsonObject optionDescProp;
    optionDescProp[QStringLiteral("type")] = QStringLiteral("string");
    optionDescProp[QStringLiteral("description")] = QStringLiteral("Explanation of the choice");

    QJsonObject optionProps;
    optionProps[QStringLiteral("label")] = optionLabelProp;
    optionProps[QStringLiteral("description")] = optionDescProp;

    QJsonObject optionSchema;
    optionSchema[QStringLiteral("type")] = QStringLiteral("object");
    optionSchema[QStringLiteral("properties")] = optionProps;
    optionSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("label"), QStringLiteral("description")};

    // Question schema
    QJsonObject questionHeaderProp;
    questionHeaderProp[QStringLiteral("type")] = QStringLiteral("string");
    questionHeaderProp[QStringLiteral("description")] = QStringLiteral("Short label (≤12 chars), used as key in response");
    questionHeaderProp[QStringLiteral("maxLength")] = 12;

    QJsonObject questionTextProp;
    questionTextProp[QStringLiteral("type")] = QStringLiteral("string");
    questionTextProp[QStringLiteral("description")] = QStringLiteral("The question text (should end with '?')");

    QJsonObject multiSelectProp;
    multiSelectProp[QStringLiteral("type")] = QStringLiteral("boolean");
    multiSelectProp[QStringLiteral("description")] = QStringLiteral("Allow multiple selections (checkboxes) vs single selection (radio buttons)");

    QJsonObject optionsArrayProp;
    optionsArrayProp[QStringLiteral("type")] = QStringLiteral("array");
    optionsArrayProp[QStringLiteral("items")] = optionSchema;
    optionsArrayProp[QStringLiteral("minItems")] = 2;
    optionsArrayProp[QStringLiteral("maxItems")] = 4;
    optionsArrayProp[QStringLiteral("description")] = QStringLiteral("2-4 options for the user to choose from");

    QJsonObject questionProps;
    questionProps[QStringLiteral("header")] = questionHeaderProp;
    questionProps[QStringLiteral("question")] = questionTextProp;
    questionProps[QStringLiteral("multiSelect")] = multiSelectProp;
    questionProps[QStringLiteral("options")] = optionsArrayProp;

    QJsonObject questionSchema;
    questionSchema[QStringLiteral("type")] = QStringLiteral("object");
    questionSchema[QStringLiteral("properties")] = questionProps;
    questionSchema[QStringLiteral("required")] = QJsonArray{
        QStringLiteral("header"),
        QStringLiteral("question"),
        QStringLiteral("multiSelect"),
        QStringLiteral("options")
    };

    // Questions array
    QJsonObject questionsArrayProp;
    questionsArrayProp[QStringLiteral("type")] = QStringLiteral("array");
    questionsArrayProp[QStringLiteral("items")] = questionSchema;
    questionsArrayProp[QStringLiteral("minItems")] = 1;
    questionsArrayProp[QStringLiteral("maxItems")] = 4;
    questionsArrayProp[QStringLiteral("description")] = QStringLiteral("1-4 questions to ask the user");

    QJsonObject askUserProps;
    askUserProps[QStringLiteral("questions")] = questionsArrayProp;

    QJsonObject askUserSchema;
    askUserSchema[QStringLiteral("type")] = QStringLiteral("object");
    askUserSchema[QStringLiteral("properties")] = askUserProps;
    askUserSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("questions")};
    askUserTool[QStringLiteral("inputSchema")] = askUserSchema;

    // Read-only annotation (doesn't modify files, just asks user)
    QJsonObject askUserAnnotations;
    askUserAnnotations[QStringLiteral("readOnlyHint")] = true;
    askUserAnnotations[QStringLiteral("destructiveHint")] = false;
    askUserTool[QStringLiteral("annotations")] = askUserAnnotations;

    QJsonObject result;
    result[QStringLiteral("tools")] = QJsonArray{docsTool, readTool, editTool, writeTool, askUserTool};

    return makeResponse(id, result);
}

QJsonObject MCPServer::handleToolsCall(int id, const QJsonObject &params)
{
    const QString toolName = params[QStringLiteral("name")].toString();
    const QJsonObject arguments = params[QStringLiteral("arguments")].toObject();

    if (toolName == QStringLiteral("katecode_documents")) {
        return makeResponse(id, executeDocuments(arguments));
    } else if (toolName == QStringLiteral("katecode_read")) {
        return makeResponse(id, executeRead(arguments));
    } else if (toolName == QStringLiteral("katecode_edit")) {
        return makeResponse(id, executeEdit(arguments));
    } else if (toolName == QStringLiteral("katecode_write")) {
        return makeResponse(id, executeWrite(arguments));
    } else if (toolName == QStringLiteral("katecode_ask_user")) {
        return makeResponse(id, executeAskUserQuestion(arguments));
    }

    return makeErrorResponse(id, -32602, QStringLiteral("Unknown tool: %1").arg(toolName));
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

QJsonObject MCPServer::makeErrorResult(const QString &message)
{
    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = message;

    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    result[QStringLiteral("isError")] = true;
    return result;
}

QJsonObject MCPServer::executeAskUserQuestion(const QJsonObject &arguments)
{
    const QJsonArray questions = arguments[QStringLiteral("questions")].toArray();

    // Validate: 1-4 questions
    if (questions.isEmpty()) {
        return makeErrorResult(QStringLiteral("Error: questions array is required and cannot be empty"));
    }
    if (questions.size() > 4) {
        return makeErrorResult(QStringLiteral("Error: questions array must have at most 4 items"));
    }

    // Validate each question
    for (int i = 0; i < questions.size(); ++i) {
        const QJsonObject q = questions[i].toObject();
        const QString header = q[QStringLiteral("header")].toString();
        const QString questionText = q[QStringLiteral("question")].toString();
        const QJsonArray options = q[QStringLiteral("options")].toArray();

        if (header.isEmpty()) {
            return makeErrorResult(QStringLiteral("Error: question %1 is missing 'header'").arg(i + 1));
        }
        if (header.length() > 12) {
            return makeErrorResult(QStringLiteral("Error: question %1 header exceeds 12 characters").arg(i + 1));
        }
        if (questionText.isEmpty()) {
            return makeErrorResult(QStringLiteral("Error: question %1 is missing 'question' text").arg(i + 1));
        }
        if (options.size() < 2) {
            return makeErrorResult(QStringLiteral("Error: question %1 must have at least 2 options").arg(i + 1));
        }
        if (options.size() > 4) {
            return makeErrorResult(QStringLiteral("Error: question %1 must have at most 4 options").arg(i + 1));
        }
    }

    // Serialize questions to JSON string for DBus
    const QString questionsJson = QString::fromUtf8(
        QJsonDocument(questions).toJson(QJsonDocument::Compact));

    // Call DBus method - this will block until user responds
    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());

    if (!iface.isValid()) {
        return makeErrorResult(QStringLiteral("Error: Could not connect to Kate editor DBus service. "
                                              "Is Kate running with the Kate Code plugin enabled?"));
    }

    // Set timeout to 5 minutes (user interaction can take time)
    iface.setTimeout(300000);

    QDBusReply<QString> reply = iface.call(QStringLiteral("askUserQuestion"), questionsJson);

    if (!reply.isValid()) {
        return makeErrorResult(QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message()));
    }

    const QString responseJson = reply.value();

    // Check for error response
    if (responseJson.startsWith(QStringLiteral("ERROR:"))) {
        return makeErrorResult(responseJson);
    }

    // Parse the JSON response and format it nicely
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseJson.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // Fallback to raw JSON if parsing fails
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = responseJson;

        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        return result;
    }

    // Format the response as readable text (plain text, no markdown)
    QJsonObject answers = doc.object();
    QString formattedText;

    for (auto it = answers.begin(); it != answers.end(); ++it) {
        const QString &header = it.key();
        const QJsonValue &value = it.value();

        formattedText += QStringLiteral("%1: ").arg(header);

        if (value.isArray()) {
            // Multi-select: list all selected options
            QStringList selections;
            const QJsonArray arr = value.toArray();
            for (const QJsonValue &v : arr) {
                selections << v.toString();
            }
            formattedText += selections.join(QStringLiteral(", "));
        } else {
            // Single-select: just the value
            formattedText += value.toString();
        }
        formattedText += QStringLiteral("\n");
    }

    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = formattedText.trimmed();

    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    return result;
}
