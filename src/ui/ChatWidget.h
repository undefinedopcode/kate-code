#pragma once

#include "../acp/ACPModels.h"
#include <QJsonArray>
#include <QWidget>
#include <functional>

class ACPSession;
class ChatWebView;
class ChatInputWidget;
class QPushButton;
class QLabel;

class ChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWidget(QWidget *parent = nullptr);
    ~ChatWidget() override;

    // Context providers (callbacks to get current file/selection/project root from Kate)
    using ContextProvider = std::function<QString()>;
    using FileListProvider = std::function<QStringList()>;
    void setFilePathProvider(ContextProvider provider);
    void setSelectionProvider(ContextProvider provider);
    void setProjectRootProvider(ContextProvider provider);
    void setFileListProvider(FileListProvider provider);

    // Context chunk management
    void addContextChunk(const QString &filePath, int startLine, int endLine, const QString &content);
    void removeContextChunk(const QString &id);
    void clearContextChunks();

private Q_SLOTS:
    void onConnectClicked();
    void onNewSessionClicked();
    void onMessageSubmitted(const QString &message);
    void onPermissionModeChanged(const QString &mode);
    void onStatusChanged(ConnectionStatus status);
    void onMessageAdded(const Message &message);
    void onMessageUpdated(const QString &messageId, const QString &content);
    void onMessageFinished(const QString &messageId);
    void onToolCallAdded(const QString &messageId, const ToolCall &toolCall);
    void onToolCallUpdated(const QString &messageId, const QString &toolCallId, const QString &status, const QString &result);
    void onTodosUpdated(const QList<TodoItem> &todos);
    void onPermissionRequested(const PermissionRequest &request);
    void onModesAvailable(const QJsonArray &modes);
    void onModeChanged(const QString &modeId);
    void onError(const QString &message);
    void onRemoveContextChunk(const QString &id);

private:
    void updateContextChipsDisplay();
    ACPSession *m_session;

    // Context providers
    ContextProvider m_filePathProvider;
    ContextProvider m_selectionProvider;
    ContextProvider m_projectRootProvider;
    FileListProvider m_fileListProvider;

    // Context chunks
    QList<ContextChunk> m_contextChunks;
    int m_nextChunkId = 0;

    // UI components
    ChatWebView *m_chatWebView;
    ChatInputWidget *m_inputWidget;
    QPushButton *m_connectButton;
    QPushButton *m_newSessionButton;
    QLabel *m_statusLabel;
    QWidget *m_contextChipsContainer;
};
