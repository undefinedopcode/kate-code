#include "ChatWebView.h"
#include "../util/KDEColorScheme.h"
#include "../util/KateThemeConverter.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineSettings>

ChatWebView::ChatWebView(QWidget *parent)
    : QWebEngineView(parent)
    , m_isLoaded(false)
    , m_bridge(new WebBridge(this))
{
    // Configure settings
    settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);

    // Enable developer tools for debugging
    qputenv("QTWEBENGINE_REMOTE_DEBUGGING", "9222");

    // Setup web channel for JavaScript <-> C++ communication
    setupBridge();

    // Load the chat HTML
    setUrl(QUrl(QStringLiteral("qrc:/katecode/web/chat.html")));

    connect(this, &QWebEngineView::loadFinished, this, &ChatWebView::onLoadFinished);
    connect(m_bridge, &WebBridge::permissionResponse, this, &ChatWebView::permissionResponseReady);
    connect(m_bridge, &WebBridge::jumpToEditRequested, this, &ChatWebView::jumpToEditRequested);
}

ChatWebView::~ChatWebView()
{
}

void ChatWebView::onLoadFinished(bool ok)
{
    if (ok) {
        m_isLoaded = true;
        qDebug() << "[ChatWebView] Page loaded successfully";

        // Inject KDE color scheme
        injectColorScheme();

        // Signal that we're ready for additional setup (like diff colors)
        Q_EMIT webViewReady();
    } else {
        qWarning() << "[ChatWebView] Failed to load page";
    }
}

void ChatWebView::injectColorScheme()
{
    KDEColorScheme colorScheme;
    QString cssVars = colorScheme.generateCSSVariables();
    bool isLight = colorScheme.isLightTheme();

    // Get Kate's editor font
    QPair<QString, int> editorFont = KateThemeConverter::getEditorFont();
    QString fontFamily = editorFont.first;
    int fontSize = editorFont.second;

    // Try to load Kate's current theme for syntax highlighting
    QString kateThemeCSS = KateThemeConverter::getCurrentThemeCSS();

    QString hljsTheme;
    QString codeBg;
    QString inlineCodeBg;

    if (!kateThemeCSS.isEmpty()) {
        // Use Kate theme - inject custom CSS directly
        qDebug() << "[ChatWebView] Using Kate theme CSS (" << kateThemeCSS.length() << "bytes)";

        // Try to extract background color from Kate theme
        QString themeName = KateThemeConverter::getCurrentKateTheme();
        QJsonObject themeJson = KateThemeConverter::loadKateTheme(themeName);

        if (!themeJson.isEmpty()) {
            QJsonObject editorColors = themeJson[QStringLiteral("editor-colors")].toObject();
            QString kateCodeBg = editorColors[QStringLiteral("BackgroundColor")].toString();

            if (!kateCodeBg.isEmpty()) {
                codeBg = kateCodeBg;
                qDebug() << "[ChatWebView] Using Kate background color:" << codeBg;
            } else {
                codeBg = isLight ? QStringLiteral("#fafafa") : QStringLiteral("#282c34");
                qDebug() << "[ChatWebView] No Kate background, using fallback:" << codeBg;
            }
        } else {
            codeBg = isLight ? QStringLiteral("#fafafa") : QStringLiteral("#282c34");
            qDebug() << "[ChatWebView] No theme JSON, using fallback:" << codeBg;
        }

        inlineCodeBg = isLight ? QStringLiteral("rgba(0, 0, 0, 0.08)")
                                : QStringLiteral("rgba(0, 0, 0, 0.3)");

        // Theme-aware task purple: darker on light themes, lighter on dark themes for contrast
        QString taskPurple = isLight ? QStringLiteral("#9c27b0") : QStringLiteral("#ce93d8");
        QString taskPurpleBg = isLight ? QStringLiteral("rgba(156, 39, 176, 0.08)")
                                       : QStringLiteral("rgba(206, 147, 216, 0.15)");

        // Terminal text color: dark text on light backgrounds, light text on dark backgrounds
        QString terminalFg = isLight ? QStringLiteral("#1e1e1e") : QStringLiteral("#e0e0e0");

        // Escape the CSS for JavaScript string literal
        QString escapedCSS = kateThemeCSS;
        escapedCSS.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        escapedCSS.replace(QLatin1Char('\''), QStringLiteral("\\'"));
        escapedCSS.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        escapedCSS.replace(QLatin1Char('\r'), QStringLiteral("\\r"));

        // Add code background and font variables to CSS vars
        // Don't quote font family here - quotes will be added in CSS usage
        QString fullCssVars = cssVars + QStringLiteral("; --code-bg: %1; --inline-code-bg: %2; --code-font-family: %3; --code-font-size: %4px; --task-purple: %5; --task-purple-bg: %6; --terminal-fg: %7")
                                            .arg(codeBg, inlineCodeBg, fontFamily)
                                            .arg(fontSize)
                                            .arg(taskPurple, taskPurpleBg, terminalFg);

        QString script = QStringLiteral(
            "applyColorScheme('%1'); "
            "applyCustomHighlightCSS('%2');"
        ).arg(fullCssVars, escapedCSS);

        runJavaScript(script);
    } else {
        // Fallback to bundled highlight.js themes
        qDebug() << "[ChatWebView] Kate theme not available, using fallback";

        hljsTheme = isLight ? QStringLiteral("vendor/atom-one-light.min.css")
                            : QStringLiteral("vendor/atom-one-dark.min.css");
        codeBg = isLight ? QStringLiteral("#fafafa") : QStringLiteral("#282c34");
        inlineCodeBg = isLight ? QStringLiteral("rgba(0, 0, 0, 0.08)")
                                : QStringLiteral("rgba(0, 0, 0, 0.3)");

        // Theme-aware task purple: darker on light themes, lighter on dark themes for contrast
        QString taskPurple = isLight ? QStringLiteral("#9c27b0") : QStringLiteral("#ce93d8");
        QString taskPurpleBg = isLight ? QStringLiteral("rgba(156, 39, 176, 0.08)")
                                       : QStringLiteral("rgba(206, 147, 216, 0.15)");

        // Terminal text color: dark text on light backgrounds, light text on dark backgrounds
        QString terminalFg = isLight ? QStringLiteral("#1e1e1e") : QStringLiteral("#e0e0e0");

        QString fullCssVars = cssVars + QStringLiteral("; --code-bg: %1; --inline-code-bg: %2; --code-font-family: %3; --code-font-size: %4px; --task-purple: %5; --task-purple-bg: %6; --terminal-fg: %7")
                                            .arg(codeBg, inlineCodeBg, fontFamily)
                                            .arg(fontSize)
                                            .arg(taskPurple, taskPurpleBg, terminalFg);

        QString script = QStringLiteral(
            "applyColorScheme('%1'); "
            "applyHighlightTheme('%2');"
        ).arg(fullCssVars, hljsTheme);

        runJavaScript(script);
    }

    qDebug() << "[ChatWebView] Injected KDE color scheme and syntax highlighting";
}

void ChatWebView::addMessage(const Message &message)
{
    if (!m_isLoaded) {
        qWarning() << "[ChatWebView] Cannot add message: page not loaded";
        return;
    }

    QString timestamp = message.timestamp.toString(Qt::ISODate);

    // Build images JSON array for user messages with attachments
    QString imagesJson = QStringLiteral("[]");
    if (!message.images.isEmpty()) {
        QJsonArray imagesArray;
        for (const ImageAttachment &img : message.images) {
            QJsonObject imgObj;
            imgObj[QStringLiteral("data")] = QString::fromLatin1(img.data.toBase64());
            imgObj[QStringLiteral("mimeType")] = img.mimeType;
            imgObj[QStringLiteral("width")] = img.dimensions.width();
            imgObj[QStringLiteral("height")] = img.dimensions.height();
            imagesArray.append(imgObj);
        }
        imagesJson = QString::fromUtf8(QJsonDocument(imagesArray).toJson(QJsonDocument::Compact));
    }

    // Use Base64 encoding to safely pass images JSON through JavaScript
    QString imagesBase64 = QString::fromLatin1(imagesJson.toUtf8().toBase64());

    QString script = QStringLiteral(
        "addMessage('%1', '%2', '%3', '%4', %5, JSON.parse(atob('%6')));"
    ).arg(escapeJsString(message.id),
          escapeJsString(message.role),
          escapeJsString(message.content),
          timestamp,
          message.isStreaming ? QStringLiteral("true") : QStringLiteral("false"),
          imagesBase64);

    runJavaScript(script);
}

void ChatWebView::updateMessage(const QString &messageId, const QString &content)
{
    if (!m_isLoaded) {
        qWarning() << "[ChatWebView] Cannot update message: page not loaded";
        return;
    }

    qDebug() << "[ChatWebView] Updating message:" << messageId << "with" << content.length() << "chars";

    QString script = QStringLiteral("updateMessage('%1', '%2');")
                         .arg(escapeJsString(messageId),
                              escapeJsString(content));

    runJavaScript(script);
}

void ChatWebView::finishMessage(const QString &messageId)
{
    if (!m_isLoaded) return;

    QString script = QStringLiteral("finishMessage('%1');")
                         .arg(escapeJsString(messageId));

    runJavaScript(script);
}

void ChatWebView::addToolCall(const QString &messageId, const ToolCall &toolCall)
{
    if (!m_isLoaded) return;

    QString inputJson = QString::fromUtf8(QJsonDocument(toolCall.input).toJson(QJsonDocument::Compact));

    // Serialize edits array to JSON
    QJsonArray editsArray;
    for (const EditDiff &edit : toolCall.edits) {
        QJsonObject editObj;
        editObj[QStringLiteral("oldText")] = edit.oldText;
        editObj[QStringLiteral("newText")] = edit.newText;
        editObj[QStringLiteral("filePath")] = edit.filePath;
        editsArray.append(editObj);
    }
    QString editsJson = QString::fromUtf8(QJsonDocument(editsArray).toJson(QJsonDocument::Compact));

    // Build script with all 10 arguments including terminalId
    QString script = QStringLiteral("addToolCall('%1', '%2', '%3', '%4', '%5', '%6', '%7', '%8', '%9', '%10');")
                         .arg(escapeJsString(messageId),
                              escapeJsString(toolCall.id),
                              escapeJsString(toolCall.name),
                              escapeJsString(toolCall.status),
                              escapeJsString(toolCall.filePath),
                              escapeJsString(inputJson),
                              escapeJsString(toolCall.oldText),
                              escapeJsString(toolCall.newText),
                              escapeJsString(editsJson),
                              escapeJsString(toolCall.terminalId));

    runJavaScript(script);
}

void ChatWebView::updateToolCall(const QString &messageId, const QString &toolCallId, const QString &status, const QString &result, const QString &filePath, const QString &toolName)
{
    if (!m_isLoaded) return;

    // Base64 encode result to safely pass ANSI escape codes and other special characters
    QByteArray resultBytes = result.toUtf8();
    QString base64Result = QString::fromLatin1(resultBytes.toBase64());

    QString script = QStringLiteral("updateToolCall('%1', '%2', '%3', '%4', '%5', '%6');")
                         .arg(escapeJsString(messageId),
                              escapeJsString(toolCallId),
                              escapeJsString(status),
                              base64Result,
                              escapeJsString(filePath),
                              escapeJsString(toolName));

    runJavaScript(script);
}

void ChatWebView::showPermissionRequest(const PermissionRequest &request)
{
    qDebug() << "[ChatWebView] showPermissionRequest called - requestId:" << request.requestId
             << "toolName:" << request.toolName << "loaded:" << m_isLoaded;

    if (!m_isLoaded) {
        qWarning() << "[ChatWebView] Page not loaded yet, cannot show permission request";
        return;
    }

    // Convert options to JSON
    QJsonArray optionsJson;
    for (const QJsonObject &option : request.options) {
        optionsJson.append(option);
    }

    QByteArray inputJsonBytes = QJsonDocument(request.input).toJson(QJsonDocument::Compact);
    QByteArray optionsJsonBytes = QJsonDocument(optionsJson).toJson(QJsonDocument::Compact);

    // Use Base64 encoding to safely pass JSON through JavaScript string literals
    // This avoids all escaping issues with special characters, newlines, etc.
    QString inputBase64 = QString::fromLatin1(inputJsonBytes.toBase64());
    QString optionsBase64 = QString::fromLatin1(optionsJsonBytes.toBase64());

    qDebug() << "[ChatWebView] Input JSON length:" << inputJsonBytes.length() << "Base64:" << inputBase64.length();

    QString script = QStringLiteral(
        "try { "
        "  window._permInput = JSON.parse(atob('%1')); "
        "  window._permOptions = JSON.parse(atob('%2')); "
        "  showPermissionRequest(%3, '%4', window._permInput, window._permOptions); "
        "} catch(e) { "
        "  console.error('Permission request error:', e); "
        "}"
    ).arg(inputBase64)
     .arg(optionsBase64)
     .arg(request.requestId)
     .arg(escapeJsString(request.toolName));

    runJavaScript(script);
}

void ChatWebView::updateTodos(const QList<TodoItem> &todos)
{
    if (!m_isLoaded) return;

    QJsonArray todosArray;
    for (const TodoItem &todo : todos) {
        QJsonObject todoObj;
        todoObj[QStringLiteral("content")] = todo.content;
        todoObj[QStringLiteral("status")] = todo.status;
        todoObj[QStringLiteral("activeForm")] = todo.activeForm;
        todosArray.append(todoObj);
    }

    QString todosJson = QString::fromUtf8(QJsonDocument(todosArray).toJson(QJsonDocument::Compact));

    QString script = QStringLiteral("updateTodos('%1');")
                         .arg(escapeJsString(todosJson));

    runJavaScript(script);
}

void ChatWebView::clearMessages()
{
    if (!m_isLoaded) return;
    runJavaScript(QStringLiteral("clearMessages();"));
}

void ChatWebView::updateTerminalOutput(const QString &terminalId, const QString &output, bool finished)
{
    if (!m_isLoaded) return;

    // Base64 encode to safely pass terminal output with ANSI codes
    QByteArray outputBytes = output.toUtf8();
    QString base64Output = QString::fromLatin1(outputBytes.toBase64());

    QString script = QStringLiteral("updateTerminal('%1', '%2', %3);")
        .arg(escapeJsString(terminalId),
             base64Output,
             finished ? QStringLiteral("true") : QStringLiteral("false"));

    runJavaScript(script);
}

void ChatWebView::setToolCallTerminalId(const QString &messageId, const QString &toolCallId, const QString &terminalId)
{
    if (!m_isLoaded) return;

    QString script = QStringLiteral("setToolCallTerminalId('%1', '%2', '%3');")
        .arg(escapeJsString(messageId),
             escapeJsString(toolCallId),
             escapeJsString(terminalId));

    runJavaScript(script);
}

void ChatWebView::setupBridge()
{
    QWebChannel *channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("bridge"), m_bridge);
    page()->setWebChannel(channel);
}

void ChatWebView::runJavaScript(const QString &script)
{
    page()->runJavaScript(script, [script](const QVariant &result) {
        Q_UNUSED(result);
        qDebug() << "[ChatWebView] JS executed:" << script.left(100);
    });
}

QString ChatWebView::escapeJsString(const QString &str)
{
    QString escaped = str;
    // Order matters: backslash must be first
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escaped.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    escaped.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    escaped.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    escaped.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    escaped.replace(QLatin1Char('\b'), QStringLiteral("\\b"));
    escaped.replace(QLatin1Char('\f'), QStringLiteral("\\f"));
    // Escape HTML script tags to prevent injection
    escaped.replace(QStringLiteral("</"), QStringLiteral("<\\/"));
    return escaped;
}

void ChatWebView::addTrackedEdit(const TrackedEdit &edit)
{
    if (!m_isLoaded) {
        return;
    }

    // Serialize the edit to JSON
    QJsonObject editObj;
    editObj[QStringLiteral("toolCallId")] = edit.toolCallId;
    editObj[QStringLiteral("filePath")] = edit.filePath;
    editObj[QStringLiteral("startLine")] = edit.startLine;
    editObj[QStringLiteral("oldLineCount")] = edit.oldLineCount;
    editObj[QStringLiteral("newLineCount")] = edit.newLineCount;
    editObj[QStringLiteral("isNewFile")] = edit.isNewFile;

    QString editJson = QString::fromUtf8(QJsonDocument(editObj).toJson(QJsonDocument::Compact));
    QString script = QStringLiteral("addTrackedEdit('%1');").arg(escapeJsString(editJson));
    runJavaScript(script);
}

void ChatWebView::clearEditSummary()
{
    if (!m_isLoaded) {
        return;
    }

    runJavaScript(QStringLiteral("clearEditSummary();"));
}

void ChatWebView::updateDiffColors(const QString &removeBackground, const QString &addBackground)
{
    if (!m_isLoaded) {
        return;
    }

    QString script = QStringLiteral(
        "document.documentElement.style.setProperty('--diff-remove-bg', '%1');"
        "document.documentElement.style.setProperty('--diff-add-bg', '%2');"
    ).arg(removeBackground, addBackground);

    runJavaScript(script);
    qDebug() << "[ChatWebView] Updated diff colors: remove=" << removeBackground << "add=" << addBackground;
}

// WebBridge implementation
void WebBridge::respondToPermission(int requestId, const QString &optionId)
{
    Q_EMIT permissionResponse(requestId, optionId);
}

void WebBridge::logFromJS(const QString &message)
{
    qDebug() << "[JS]" << message;
}

void WebBridge::jumpToEdit(const QString &filePath, int startLine, int endLine)
{
    qDebug() << "[WebBridge] jumpToEdit requested:" << filePath << "lines" << startLine << "-" << endLine;
    Q_EMIT jumpToEditRequested(filePath, startLine, endLine);
}
