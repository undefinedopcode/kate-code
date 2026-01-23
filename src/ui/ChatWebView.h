#pragma once

#include "../acp/ACPModels.h"
#include <QWebEngineView>

class WebBridge;

class ChatWebView : public QWebEngineView
{
    Q_OBJECT

public:
    explicit ChatWebView(QWidget *parent = nullptr);
    ~ChatWebView() override;

    void addMessage(const Message &message);
    void updateMessage(const QString &messageId, const QString &content);
    void finishMessage(const QString &messageId);
    void addToolCall(const QString &messageId, const ToolCall &toolCall);
    void updateToolCall(const QString &messageId, const QString &toolCallId, const QString &status, const QString &result);
    void showPermissionRequest(const PermissionRequest &request);
    void updateTodos(const QList<TodoItem> &todos);
    void clearMessages();

    // Terminal support
    void updateTerminalOutput(const QString &terminalId, const QString &output, bool finished);

    // Edit tracking support
    void addTrackedEdit(const TrackedEdit &edit);
    void clearEditSummary();

    // Diff color scheme support
    void updateDiffColors(const QString &removeBackground, const QString &addBackground);

Q_SIGNALS:
    void permissionResponseReady(int requestId, const QString &optionId);
    void jumpToEditRequested(const QString &filePath, int startLine, int endLine);

private Q_SLOTS:
    void onLoadFinished(bool ok);

private:
    void runJavaScript(const QString &script);
    QString escapeJsString(const QString &str);
    void injectColorScheme();
    void setupBridge();

    bool m_isLoaded;
    WebBridge *m_bridge;
};

// Bridge class for JavaScript to call C++
class WebBridge : public QObject
{
    Q_OBJECT

public:
    explicit WebBridge(QObject *parent = nullptr) : QObject(parent) {}

public Q_SLOTS:
    Q_INVOKABLE void respondToPermission(int requestId, const QString &optionId);
    Q_INVOKABLE void logFromJS(const QString &message);
    Q_INVOKABLE void jumpToEdit(const QString &filePath, int startLine, int endLine);

Q_SIGNALS:
    void permissionResponse(int requestId, const QString &optionId);
    void jumpToEditRequested(const QString &filePath, int startLine, int endLine);
};
