#include "ChatWebView.h"
#include "../util/KDEColorScheme.h"

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
    } else {
        qWarning() << "[ChatWebView] Failed to load page";
    }
}

void ChatWebView::injectColorScheme()
{
    KDEColorScheme colorScheme;
    QString cssVars = colorScheme.generateCSSVariables();
    bool isLight = colorScheme.isLightTheme();

    // Choose the appropriate highlight.js theme and code background colors
    QString hljsTheme = isLight ? QStringLiteral("vendor/atom-one-light.min.css")
                                 : QStringLiteral("vendor/atom-one-dark.min.css");
    QString codeBg = isLight ? QStringLiteral("#fafafa") : QStringLiteral("#282c34");
    QString inlineCodeBg = isLight ? QStringLiteral("rgba(0, 0, 0, 0.08)")
                                    : QStringLiteral("rgba(0, 0, 0, 0.3)");

    // Add code background variables to CSS vars
    QString fullCssVars = cssVars + QStringLiteral("; --code-bg: %1; --inline-code-bg: %2")
                                        .arg(codeBg, inlineCodeBg);

    QString script = QStringLiteral(
        "applyColorScheme('%1'); "
        "applyHighlightTheme('%2');"
    ).arg(fullCssVars, hljsTheme);

    runJavaScript(script);

    qDebug() << "[ChatWebView] Injected KDE color scheme and highlight theme:" << hljsTheme;
}

void ChatWebView::addMessage(const Message &message)
{
    if (!m_isLoaded) {
        qWarning() << "[ChatWebView] Cannot add message: page not loaded";
        return;
    }

    QString timestamp = message.timestamp.toString(Qt::ISODate);
    QString script = QStringLiteral("addMessage('%1', '%2', '%3', '%4', %5);")
                         .arg(escapeJsString(message.id),
                              escapeJsString(message.role),
                              escapeJsString(message.content),
                              timestamp,
                              message.isStreaming ? QStringLiteral("true") : QStringLiteral("false"));

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

    QString script = QStringLiteral("addToolCall('%1', '%2', '%3', '%4', '%5', '%6', '%7', '%8');")
                         .arg(escapeJsString(messageId),
                              escapeJsString(toolCall.id),
                              escapeJsString(toolCall.name),
                              escapeJsString(toolCall.status),
                              escapeJsString(toolCall.filePath),
                              escapeJsString(inputJson),
                              escapeJsString(toolCall.oldText),
                              escapeJsString(toolCall.newText));

    runJavaScript(script);
}

void ChatWebView::updateToolCall(const QString &messageId, const QString &toolCallId, const QString &status, const QString &result)
{
    if (!m_isLoaded) return;

    QString script = QStringLiteral("updateToolCall('%1', '%2', '%3', '%4');")
                         .arg(escapeJsString(messageId),
                              escapeJsString(toolCallId),
                              escapeJsString(status),
                              escapeJsString(result));

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

// WebBridge implementation
void WebBridge::respondToPermission(int requestId, const QString &optionId)
{
    Q_EMIT permissionResponse(requestId, optionId);
}

void WebBridge::logFromJS(const QString &message)
{
    qDebug() << "[JS]" << message;
}
