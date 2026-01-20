#pragma once

#include "../acp/ACPModels.h"
#include <QJsonArray>
#include <QWidget>
#include <functional>

class ACPSession;
class ChatWebView;
class ChatInputWidget;
class SessionStore;
class SummaryStore;
class SummaryGenerator;
class SettingsStore;
class QPushButton;
class QToolButton;
class QLabel;

class ChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWidget(QWidget *parent = nullptr);
    ~ChatWidget() override;

    // Settings injection (from KateCodePlugin via KateCodeView)
    void setSettingsStore(SettingsStore *settings);

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

    // Image attachment management
    void clearImageAttachments();

    // Quick Actions - send prompt directly with selection
    void sendPromptWithSelection(const QString &prompt, const QString &filePath, const QString &selection);

    // Shutdown hook - generates summary and waits for completion
    void prepareForShutdown();

protected:
    void resizeEvent(QResizeEvent *event) override;

Q_SIGNALS:
    // Diff highlighting signals for KateCodeView integration
    void toolCallHighlightRequested(const QString &toolCallId, const ToolCall &toolCall);
    void toolCallClearRequested(const QString &toolCallId);

private Q_SLOTS:
    void onConnectClicked();
    void onNewSessionClicked();
    void onStopClicked();
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
    void onPromptCancelled();
    void onImageAttached(const ImageAttachment &image);
    void onRemoveImageAttachment(const QString &id);

    // Session persistence
    void onInitializeComplete();
    void onSessionLoadFailed(const QString &error);

    // Summary generation
    void onSummaryReady(const QString &sessionId, const QString &projectRoot, const QString &summary);
    void onSummaryError(const QString &sessionId, const QString &error);
    void onApiKeyLoadedForSummary(bool success);

private:
    void triggerSummaryGeneration();
    void updateTerminalSize();
    // Pending session action for after initialize completes
    enum class PendingAction {
        None,
        CreateSession,
        LoadSession
    };
    void updateContextChipsDisplay();
    void updateImageChipsDisplay();
    ACPSession *m_session;

    // Context providers
    ContextProvider m_filePathProvider;
    ContextProvider m_selectionProvider;
    ContextProvider m_projectRootProvider;
    FileListProvider m_fileListProvider;

    // Context chunks
    QList<ContextChunk> m_contextChunks;
    int m_nextChunkId = 0;

    // Image attachments
    QList<ImageAttachment> m_imageAttachments;
    int m_nextImageId = 0;

    // UI components
    ChatWebView *m_chatWebView;
    ChatInputWidget *m_inputWidget;
    QToolButton *m_connectButton;
    QToolButton *m_newSessionButton;
    QLabel *m_titleLabel;
    QLabel *m_statusIndicator;
    QWidget *m_contextChipsContainer;

    // Session persistence
    SessionStore *m_sessionStore;
    PendingAction m_pendingAction;
    QString m_pendingSessionId;

    // Summary generation
    SettingsStore *m_settingsStore;
    SummaryStore *m_summaryStore;
    SummaryGenerator *m_summaryGenerator;
    QString m_lastSessionId;
    QString m_lastProjectRoot;

    // Session resumption context (sent automatically after connect)
    QString m_pendingSummaryContext;

    // Track whether user has sent a message (for summary generation decision)
    bool m_userSentMessage = false;

    // Track pending summary generation waiting for API key
    bool m_pendingSummaryAfterKeyLoad = false;
};
