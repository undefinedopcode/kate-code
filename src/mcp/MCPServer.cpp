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
        QStringLiteral("Reads the content of a file. If the file is open in Kate, returns the current buffer content (which may have unsaved changes). Otherwise reads from disk. By default returns up to 2000 lines; use offset and limit for larger files.\n\nIn sessions with mcp__kate__katecode_read always use it instead of Read or mcp__acp__Read, as it contains the most up-to-date contents provided by the editor.");

    QJsonObject readPathProp;
    readPathProp[QStringLiteral("type")] = QStringLiteral("string");
    readPathProp[QStringLiteral("description")] = QStringLiteral("The absolute path to the file to read");

    QJsonObject readOffsetProp;
    readOffsetProp[QStringLiteral("type")] = QStringLiteral("integer");
    readOffsetProp[QStringLiteral("description")] = QStringLiteral("Line number to start reading from (1-based). Default: 1");

    QJsonObject readLimitProp;
    readLimitProp[QStringLiteral("type")] = QStringLiteral("integer");
    readLimitProp[QStringLiteral("description")] = QStringLiteral("Maximum number of lines to return. Default: 2000");

    QJsonObject readProps;
    readProps[QStringLiteral("file_path")] = readPathProp;
    readProps[QStringLiteral("offset")] = readOffsetProp;
    readProps[QStringLiteral("limit")] = readLimitProp;

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

    // katecode_active_document tool definition
    QJsonObject activeDocTool;
    activeDocTool[QStringLiteral("name")] = QStringLiteral("katecode_active_document");
    activeDocTool[QStringLiteral("description")] =
        QStringLiteral("Returns the file currently active in Kate, with cursor position and any selected text. "
                       "Use this to understand what the user is looking at without them having to tell you.");
    QJsonObject activeDocSchema;
    activeDocSchema[QStringLiteral("type")] = QStringLiteral("object");
    activeDocSchema[QStringLiteral("properties")] = QJsonObject();
    activeDocTool[QStringLiteral("inputSchema")] = activeDocSchema;
    QJsonObject activeDocAnnotations;
    activeDocAnnotations[QStringLiteral("readOnlyHint")] = true;
    activeDocAnnotations[QStringLiteral("destructiveHint")] = false;
    activeDocTool[QStringLiteral("annotations")] = activeDocAnnotations;

    // katecode_open tool definition
    QJsonObject openTool;
    openTool[QStringLiteral("name")] = QStringLiteral("katecode_open");
    openTool[QStringLiteral("description")] =
        QStringLiteral("Opens a file in Kate. Optionally jumps to a specific line and column "
                       "so the user can see the relevant code.");
    QJsonObject openFilePathProp;
    openFilePathProp[QStringLiteral("type")] = QStringLiteral("string");
    openFilePathProp[QStringLiteral("description")] = QStringLiteral("Absolute path to the file to open");
    QJsonObject openLineProp;
    openLineProp[QStringLiteral("type")] = QStringLiteral("integer");
    openLineProp[QStringLiteral("description")] = QStringLiteral("Line to jump to (1-based). Omit to open at current position.");
    QJsonObject openColProp;
    openColProp[QStringLiteral("type")] = QStringLiteral("integer");
    openColProp[QStringLiteral("description")] = QStringLiteral("Column to jump to (1-based). Only used if line is provided.");
    QJsonObject openProps;
    openProps[QStringLiteral("file_path")] = openFilePathProp;
    openProps[QStringLiteral("line")] = openLineProp;
    openProps[QStringLiteral("column")] = openColProp;
    QJsonObject openSchema;
    openSchema[QStringLiteral("type")] = QStringLiteral("object");
    openSchema[QStringLiteral("properties")] = openProps;
    openSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("file_path")};
    openTool[QStringLiteral("inputSchema")] = openSchema;
    QJsonObject openAnnotations;
    openAnnotations[QStringLiteral("readOnlyHint")] = false;
    openAnnotations[QStringLiteral("destructiveHint")] = false;
    openTool[QStringLiteral("annotations")] = openAnnotations;

    // katecode_close tool definition
    QJsonObject closeTool;
    closeTool[QStringLiteral("name")] = QStringLiteral("katecode_close");
    closeTool[QStringLiteral("description")] = QStringLiteral("Closes an open document in Kate.");
    QJsonObject closeFilePathProp;
    closeFilePathProp[QStringLiteral("type")] = QStringLiteral("string");
    closeFilePathProp[QStringLiteral("description")] = QStringLiteral("Absolute path of the open document to close");
    QJsonObject closeProps;
    closeProps[QStringLiteral("file_path")] = closeFilePathProp;
    QJsonObject closeSchema;
    closeSchema[QStringLiteral("type")] = QStringLiteral("object");
    closeSchema[QStringLiteral("properties")] = closeProps;
    closeSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("file_path")};
    closeTool[QStringLiteral("inputSchema")] = closeSchema;
    QJsonObject closeAnnotations;
    closeAnnotations[QStringLiteral("readOnlyHint")] = false;
    closeAnnotations[QStringLiteral("destructiveHint")] = false;
    closeTool[QStringLiteral("annotations")] = closeAnnotations;

    // katecode_save tool definition
    QJsonObject saveTool;
    saveTool[QStringLiteral("name")] = QStringLiteral("katecode_save");
    saveTool[QStringLiteral("description")] =
        QStringLiteral("Saves a document. If file_path is omitted, saves all modified documents.");
    QJsonObject saveFilePathProp;
    saveFilePathProp[QStringLiteral("type")] = QStringLiteral("string");
    saveFilePathProp[QStringLiteral("description")] =
        QStringLiteral("Absolute path of document to save. Omit to save all modified documents.");
    QJsonObject saveProps;
    saveProps[QStringLiteral("file_path")] = saveFilePathProp;
    QJsonObject saveSchema;
    saveSchema[QStringLiteral("type")] = QStringLiteral("object");
    saveSchema[QStringLiteral("properties")] = saveProps;
    saveTool[QStringLiteral("inputSchema")] = saveSchema;
    QJsonObject saveAnnotations;
    saveAnnotations[QStringLiteral("readOnlyHint")] = false;
    saveAnnotations[QStringLiteral("destructiveHint")] = false;
    saveTool[QStringLiteral("annotations")] = saveAnnotations;

    // katecode_status tool definition
    QJsonObject statusTool;
    statusTool[QStringLiteral("name")] = QStringLiteral("katecode_status");
    statusTool[QStringLiteral("description")] =
        QStringLiteral("Returns the status of an open document: whether it has unsaved changes and whether it is read-only.");
    QJsonObject statusFilePathProp;
    statusFilePathProp[QStringLiteral("type")] = QStringLiteral("string");
    statusFilePathProp[QStringLiteral("description")] = QStringLiteral("Absolute path of the open document");
    QJsonObject statusProps;
    statusProps[QStringLiteral("file_path")] = statusFilePathProp;
    QJsonObject statusSchema;
    statusSchema[QStringLiteral("type")] = QStringLiteral("object");
    statusSchema[QStringLiteral("properties")] = statusProps;
    statusSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("file_path")};
    statusTool[QStringLiteral("inputSchema")] = statusSchema;
    QJsonObject statusAnnotations;
    statusAnnotations[QStringLiteral("readOnlyHint")] = true;
    statusAnnotations[QStringLiteral("destructiveHint")] = false;
    statusTool[QStringLiteral("annotations")] = statusAnnotations;

    // katecode_revert tool definition
    QJsonObject revertTool;
    revertTool[QStringLiteral("name")] = QStringLiteral("katecode_revert");
    revertTool[QStringLiteral("description")] =
        QStringLiteral("Reverts a document to its on-disk state, discarding any unsaved changes.");
    QJsonObject revertFilePathProp;
    revertFilePathProp[QStringLiteral("type")] = QStringLiteral("string");
    revertFilePathProp[QStringLiteral("description")] = QStringLiteral("Absolute path of the open document to revert");
    QJsonObject revertProps;
    revertProps[QStringLiteral("file_path")] = revertFilePathProp;
    QJsonObject revertSchema;
    revertSchema[QStringLiteral("type")] = QStringLiteral("object");
    revertSchema[QStringLiteral("properties")] = revertProps;
    revertSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("file_path")};
    revertTool[QStringLiteral("inputSchema")] = revertSchema;
    QJsonObject revertAnnotations;
    revertAnnotations[QStringLiteral("readOnlyHint")] = false;
    revertAnnotations[QStringLiteral("destructiveHint")] = true;
    revertTool[QStringLiteral("annotations")] = revertAnnotations;

    // katecode_set_session_note tool definition
    QJsonObject setNoteTool;
    setNoteTool[QStringLiteral("name")] = QStringLiteral("katecode_set_session_note");
    setNoteTool[QStringLiteral("description")] =
        QStringLiteral("Updates the note stored against the current kate-code session. "
                       "Use this at the end of a session (or whenever useful) to record a brief "
                       "summary of what was accomplished, so it is visible in the session picker "
                       "when resuming later.");

    QJsonObject noteSessionIdProp;
    noteSessionIdProp[QStringLiteral("type")] = QStringLiteral("string");
    noteSessionIdProp[QStringLiteral("description")] = QStringLiteral("The session ID to update (visible in the 'Connected! Session ID:' message)");

    QJsonObject noteNoteProp;
    noteNoteProp[QStringLiteral("type")] = QStringLiteral("string");
    noteNoteProp[QStringLiteral("description")] = QStringLiteral("Brief note summarising what was done this session (a few sentences at most)");

    QJsonObject noteProps;
    noteProps[QStringLiteral("session_id")] = noteSessionIdProp;
    noteProps[QStringLiteral("note")] = noteNoteProp;

    QJsonObject noteSchema;
    noteSchema[QStringLiteral("type")] = QStringLiteral("object");
    noteSchema[QStringLiteral("properties")] = noteProps;
    noteSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("session_id"), QStringLiteral("note")};
    setNoteTool[QStringLiteral("inputSchema")] = noteSchema;

    QJsonObject noteAnnotations;
    noteAnnotations[QStringLiteral("readOnlyHint")] = false;
    noteAnnotations[QStringLiteral("destructiveHint")] = false;
    noteAnnotations[QStringLiteral("idempotentHint")] = true;
    setNoteTool[QStringLiteral("annotations")] = noteAnnotations;

    // katecode_get_session_id tool definition
    QJsonObject getSessionIdTool;
    getSessionIdTool[QStringLiteral("name")] = QStringLiteral("katecode_get_session_id");
    getSessionIdTool[QStringLiteral("description")] =
        QStringLiteral("Returns the ID of the currently active kate-code session. "
                       "Use this to obtain the session_id required by katecode_set_session_note.");
    QJsonObject getSessionIdSchema;
    getSessionIdSchema[QStringLiteral("type")] = QStringLiteral("object");
    getSessionIdSchema[QStringLiteral("properties")] = QJsonObject{};
    getSessionIdTool[QStringLiteral("inputSchema")] = getSessionIdSchema;
    QJsonObject getSessionIdAnnotations;
    getSessionIdAnnotations[QStringLiteral("readOnlyHint")] = true;
    getSessionIdAnnotations[QStringLiteral("destructiveHint")] = false;
    getSessionIdTool[QStringLiteral("annotations")] = getSessionIdAnnotations;

    // katecode_read_clipboard tool definition
    QJsonObject readClipboardTool;
    readClipboardTool[QStringLiteral("name")] = QStringLiteral("katecode_read_clipboard");
    readClipboardTool[QStringLiteral("description")] =
        QStringLiteral("Returns the current clipboard text content. "
                       "Useful for reading text the user has copied or selected (e.g. from the terminal).");
    QJsonObject readClipboardSchema;
    readClipboardSchema[QStringLiteral("type")] = QStringLiteral("object");
    readClipboardSchema[QStringLiteral("properties")] = QJsonObject();
    readClipboardTool[QStringLiteral("inputSchema")] = readClipboardSchema;
    QJsonObject readClipboardAnnotations;
    readClipboardAnnotations[QStringLiteral("readOnlyHint")] = true;
    readClipboardAnnotations[QStringLiteral("destructiveHint")] = false;
    readClipboardTool[QStringLiteral("annotations")] = readClipboardAnnotations;

    // katecode_paste_to_terminal tool definition
    QJsonObject pasteTerminalTool;
    pasteTerminalTool[QStringLiteral("name")] = QStringLiteral("katecode_paste_to_terminal");
    pasteTerminalTool[QStringLiteral("description")] =
        QStringLiteral("Sends text to Kate's embedded terminal without executing it (no Enter key). "
                       "The user can review the text and press Enter themselves. "
                       "Requires Kate's terminal plugin to be enabled.");

    QJsonObject pasteTextProp;
    pasteTextProp[QStringLiteral("type")] = QStringLiteral("string");
    pasteTextProp[QStringLiteral("description")] = QStringLiteral("The text to paste into the terminal");

    QJsonObject pasteTerminalProps;
    pasteTerminalProps[QStringLiteral("text")] = pasteTextProp;

    QJsonObject pasteTerminalSchema;
    pasteTerminalSchema[QStringLiteral("type")] = QStringLiteral("object");
    pasteTerminalSchema[QStringLiteral("properties")] = pasteTerminalProps;
    pasteTerminalSchema[QStringLiteral("required")] = QJsonArray{QStringLiteral("text")};
    pasteTerminalTool[QStringLiteral("inputSchema")] = pasteTerminalSchema;

    QJsonObject pasteTerminalAnnotations;
    pasteTerminalAnnotations[QStringLiteral("readOnlyHint")] = false;
    pasteTerminalAnnotations[QStringLiteral("destructiveHint")] = false;
    pasteTerminalTool[QStringLiteral("annotations")] = pasteTerminalAnnotations;

    QJsonObject result;
    result[QStringLiteral("tools")] = QJsonArray{
        docsTool, readTool, editTool, writeTool, askUserTool,
        activeDocTool, openTool, closeTool, saveTool, statusTool, revertTool,
        setNoteTool, getSessionIdTool, readClipboardTool, pasteTerminalTool
    };

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
    } else if (toolName == QStringLiteral("katecode_active_document")) {
        return makeResponse(id, executeActiveDocument(arguments));
    } else if (toolName == QStringLiteral("katecode_open")) {
        return makeResponse(id, executeOpen(arguments));
    } else if (toolName == QStringLiteral("katecode_close")) {
        return makeResponse(id, executeClose(arguments));
    } else if (toolName == QStringLiteral("katecode_save")) {
        return makeResponse(id, executeSave(arguments));
    } else if (toolName == QStringLiteral("katecode_status")) {
        return makeResponse(id, executeStatus(arguments));
    } else if (toolName == QStringLiteral("katecode_revert")) {
        return makeResponse(id, executeRevert(arguments));
    } else if (toolName == QStringLiteral("katecode_set_session_note")) {
        return makeResponse(id, executeSetSessionNote(arguments));
    } else if (toolName == QStringLiteral("katecode_get_session_id")) {
        return makeResponse(id, executeGetSessionId(arguments));
    } else if (toolName == QStringLiteral("katecode_read_clipboard")) {
        return makeResponse(id, executeReadClipboard(arguments));
    } else if (toolName == QStringLiteral("katecode_paste_to_terminal")) {
        return makeResponse(id, executePasteToTerminal(arguments));
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

    // Parse optional offset and limit (1-based line numbers)
    const int defaultLimit = 2000;
    int offset = 1;  // 1-based, default to first line
    int limit = defaultLimit;

    if (arguments.contains(QStringLiteral("offset"))) {
        offset = qMax(1, arguments[QStringLiteral("offset")].toInt(1));
    }
    if (arguments.contains(QStringLiteral("limit"))) {
        limit = qMax(1, arguments[QStringLiteral("limit")].toInt(defaultLimit));
    }

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

    // Split content into lines and apply offset/limit
    QStringList allLines = content.split(QLatin1Char('\n'));
    const int totalLines = allLines.size();

    // Convert 1-based offset to 0-based index
    int startIdx = offset - 1;
    if (startIdx >= totalLines) {
        startIdx = qMax(0, totalLines - 1);
    }

    int endIdx = qMin(startIdx + limit, totalLines);
    int linesOmittedBefore = startIdx;
    int linesOmittedAfter = totalLines - endIdx;

    // Build output with line numbers (like Claude Code's Read tool)
    // Format: "   42→content" with arrow separator
    QString outputText;
    int lineNumWidth = QString::number(endIdx).length();

    for (int i = startIdx; i < endIdx; ++i) {
        int lineNum = i + 1;  // 1-based line number
        outputText += QStringLiteral("%1→%2\n")
            .arg(lineNum, lineNumWidth)
            .arg(allLines[i]);
    }

    // Remove trailing newline if present
    if (outputText.endsWith(QLatin1Char('\n'))) {
        outputText.chop(1);
    }

    // Add truncation warnings
    QString header;
    if (linesOmittedBefore > 0 || linesOmittedAfter > 0) {
        header = QStringLiteral("(%1 total lines").arg(totalLines);
        if (linesOmittedBefore > 0) {
            header += QStringLiteral(", %1 omitted before").arg(linesOmittedBefore);
        }
        if (linesOmittedAfter > 0) {
            header += QStringLiteral(", %1 omitted after").arg(linesOmittedAfter);
        }
        header += QStringLiteral(")\n\n");
    }

    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = QString(header + outputText);

    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    return result;
}

QJsonObject MCPServer::executeEdit(const QJsonObject &arguments)
{
    const QString filePath = arguments[QStringLiteral("file_path")].toString();
    const QString oldString = arguments[QStringLiteral("old_string")].toString();
    const QString newString = arguments[QStringLiteral("new_string")].toString();

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

QJsonObject MCPServer::executeActiveDocument(const QJsonObject &arguments)
{
    Q_UNUSED(arguments);

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return makeErrorResult(QStringLiteral("Error: Could not connect to Kate editor DBus service."));

    QDBusReply<QString> reply = iface.call(QStringLiteral("getActiveDocument"));
    if (!reply.isValid())
        return makeErrorResult(QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message()));

    const QString response = reply.value();
    if (response.startsWith(QStringLiteral("ERROR:")))
        return makeErrorResult(response);

    // Parse JSON and format as human-readable text
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = response;
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        return result;
    }

    QJsonObject obj = doc.object();
    QString text = QStringLiteral("Active document: %1\nCursor: line %2, column %3\nModified: %4")
        .arg(obj[QStringLiteral("path")].toString())
        .arg(obj[QStringLiteral("line")].toInt())
        .arg(obj[QStringLiteral("column")].toInt())
        .arg(obj[QStringLiteral("isModified")].toBool() ? QStringLiteral("yes") : QStringLiteral("no"));
    if (obj.contains(QStringLiteral("selection")))
        text += QStringLiteral("\nSelection: \"%1\"").arg(obj[QStringLiteral("selection")].toString());

    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = text;
    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    return result;
}

QJsonObject MCPServer::executeOpen(const QJsonObject &arguments)
{
    const QString filePath = arguments[QStringLiteral("file_path")].toString();
    if (filePath.isEmpty())
        return makeErrorResult(QStringLiteral("Error: file_path is required"));

    const int line = arguments[QStringLiteral("line")].toInt(0);
    const int column = arguments[QStringLiteral("column")].toInt(0);

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return makeErrorResult(QStringLiteral("Error: Could not connect to Kate editor DBus service."));

    QDBusReply<QString> reply = iface.call(QStringLiteral("openDocument"), filePath, line, column);
    if (!reply.isValid())
        return makeErrorResult(QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message()));

    const QString response = reply.value();
    bool isError = response.startsWith(QStringLiteral("ERROR:"));
    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = response;
    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    if (isError)
        result[QStringLiteral("isError")] = true;
    return result;
}

QJsonObject MCPServer::executeClose(const QJsonObject &arguments)
{
    const QString filePath = arguments[QStringLiteral("file_path")].toString();
    if (filePath.isEmpty())
        return makeErrorResult(QStringLiteral("Error: file_path is required"));

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return makeErrorResult(QStringLiteral("Error: Could not connect to Kate editor DBus service."));

    QDBusReply<QString> reply = iface.call(QStringLiteral("closeDocument"), filePath);
    if (!reply.isValid())
        return makeErrorResult(QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message()));

    const QString response = reply.value();
    bool isError = response.startsWith(QStringLiteral("ERROR:"));
    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = response;
    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    if (isError)
        result[QStringLiteral("isError")] = true;
    return result;
}

QJsonObject MCPServer::executeSave(const QJsonObject &arguments)
{
    // file_path is optional — empty string means save all
    const QString filePath = arguments[QStringLiteral("file_path")].toString();

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return makeErrorResult(QStringLiteral("Error: Could not connect to Kate editor DBus service."));

    QDBusReply<QString> reply = iface.call(QStringLiteral("saveDocument"), filePath);
    if (!reply.isValid())
        return makeErrorResult(QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message()));

    const QString response = reply.value();
    bool isError = response.startsWith(QStringLiteral("ERROR:"));
    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = response;
    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    if (isError)
        result[QStringLiteral("isError")] = true;
    return result;
}

QJsonObject MCPServer::executeStatus(const QJsonObject &arguments)
{
    const QString filePath = arguments[QStringLiteral("file_path")].toString();
    if (filePath.isEmpty())
        return makeErrorResult(QStringLiteral("Error: file_path is required"));

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return makeErrorResult(QStringLiteral("Error: Could not connect to Kate editor DBus service."));

    QDBusReply<QString> reply = iface.call(QStringLiteral("getDocumentStatus"), filePath);
    if (!reply.isValid())
        return makeErrorResult(QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message()));

    const QString response = reply.value();
    if (response.startsWith(QStringLiteral("ERROR:")))
        return makeErrorResult(response);

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QJsonObject textContent;
        textContent[QStringLiteral("type")] = QStringLiteral("text");
        textContent[QStringLiteral("text")] = response;
        QJsonObject result;
        result[QStringLiteral("content")] = QJsonArray{textContent};
        return result;
    }

    QJsonObject obj = doc.object();
    QString text = QStringLiteral("Document: %1\nModified: %2\nRead-only: %3")
        .arg(obj[QStringLiteral("path")].toString())
        .arg(obj[QStringLiteral("isModified")].toBool() ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(obj[QStringLiteral("isReadOnly")].toBool() ? QStringLiteral("yes") : QStringLiteral("no"));

    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = text;
    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    return result;
}

QJsonObject MCPServer::executeRevert(const QJsonObject &arguments)
{
    const QString filePath = arguments[QStringLiteral("file_path")].toString();
    if (filePath.isEmpty())
        return makeErrorResult(QStringLiteral("Error: file_path is required"));

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return makeErrorResult(QStringLiteral("Error: Could not connect to Kate editor DBus service."));

    QDBusReply<QString> reply = iface.call(QStringLiteral("revertDocument"), filePath);
    if (!reply.isValid())
        return makeErrorResult(QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message()));

    const QString response = reply.value();
    bool isError = response.startsWith(QStringLiteral("ERROR:"));
    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = response;
    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    if (isError)
        result[QStringLiteral("isError")] = true;
    return result;
}

QJsonObject MCPServer::executeSetSessionNote(const QJsonObject &arguments)
{
    const QString sessionId = arguments[QStringLiteral("session_id")].toString();
    const QString note = arguments[QStringLiteral("note")].toString();

    if (sessionId.isEmpty())
        return makeErrorResult(QStringLiteral("Error: session_id is required"));

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return makeErrorResult(QStringLiteral("Error: Could not connect to Kate editor DBus service."));

    QDBusReply<QString> reply = iface.call(QStringLiteral("setSessionNote"), sessionId, note);
    if (!reply.isValid())
        return makeErrorResult(QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message()));

    const QString response = reply.value();
    bool isError = response.startsWith(QStringLiteral("ERROR:"));
    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = response;
    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    if (isError)
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

QJsonObject MCPServer::executeGetSessionId(const QJsonObject &arguments)
{
    Q_UNUSED(arguments);

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return makeErrorResult(QStringLiteral("Error: Could not connect to Kate editor DBus service."));

    QDBusReply<QString> reply = iface.call(QStringLiteral("getSessionId"));
    if (!reply.isValid())
        return makeErrorResult(QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message()));

    const QString sessionId = reply.value();
    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = sessionId.isEmpty()
        ? QStringLiteral("No active session")
        : sessionId;
    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    return result;
}

QJsonObject MCPServer::executeReadClipboard(const QJsonObject &arguments)
{
    Q_UNUSED(arguments);

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return makeErrorResult(QStringLiteral("Error: Could not connect to Kate editor DBus service."));

    QDBusReply<QString> reply = iface.call(QStringLiteral("getClipboardText"));
    if (!reply.isValid())
        return makeErrorResult(QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message()));

    const QString text = reply.value();
    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = text.isEmpty()
        ? QStringLiteral("(clipboard is empty)")
        : text;
    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    return result;
}

QJsonObject MCPServer::executePasteToTerminal(const QJsonObject &arguments)
{
    const QString text = arguments[QStringLiteral("text")].toString();
    if (text.isEmpty())
        return makeErrorResult(QStringLiteral("Error: text is required"));

    QDBusInterface iface(QStringLiteral("org.kde.katecode.editor"),
                         QStringLiteral("/KateCode/Editor"),
                         QStringLiteral("org.kde.katecode.Editor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return makeErrorResult(QStringLiteral("Error: Could not connect to Kate editor DBus service."));

    QDBusReply<QString> reply = iface.call(QStringLiteral("pasteToTerminal"), text);
    if (!reply.isValid())
        return makeErrorResult(QStringLiteral("Error: DBus call failed: %1").arg(reply.error().message()));

    const QString response = reply.value();
    bool isError = response.startsWith(QStringLiteral("ERROR:"));
    QJsonObject textContent;
    textContent[QStringLiteral("type")] = QStringLiteral("text");
    textContent[QStringLiteral("text")] = response;
    QJsonObject result;
    result[QStringLiteral("content")] = QJsonArray{textContent};
    if (isError)
        result[QStringLiteral("isError")] = true;
    return result;
}
