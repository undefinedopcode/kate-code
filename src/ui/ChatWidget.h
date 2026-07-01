#pragma once

#include "../acp/ACPModels.h"
#include <QJsonArray>
#include <QSplitter>
#include <QWidget>
#include <functional>

namespace KTextEditor {
class Document;
}

class ACPSession;
class ACPLogger;
class ChatWebView;
class ChatInputWidget;
class SessionStore;
class SummaryStore;
class SummaryGenerator;
class SettingsStore;
class QComboBox;
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
    using DocumentProvider = std::function<KTextEditor::Document*(const QString &path)>;
    void setFilePathProvider(ContextProvider provider);
    void setSelectionProvider(ContextProvider provider);
    void setProjectRootProvider(ContextProvider provider);
    void setFileListProvider(FileListProvider provider);
    void setDocumentProvider(DocumentProvider provider);

    // Agent-slot hooks wired by KateCodeView so the plugin-level gate can be
    // consulted before any session is started.
    void setAgentSlotHooks(std::function<bool()> acquire,
                           std::function<void()> release,
                           std::function<bool()> available);

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

public Q_SLOTS:
    // Show user question UI (MCP AskUserQuestion tool)
    void showUserQuestion(const QString &requestId, const QString &questionsJson);
    // Remove user question UI (on timeout or cancel)
    void removeUserQuestion(const QString &requestId);
    // Called by KateCodePlugin::agentActivityChanged() to refresh connect/resume
    // button enabled-states when another window acquires or releases the slot.
    void refreshAgentAvailability();

protected:
    void resizeEvent(QResizeEvent *event) override;

Q_SIGNALS:
    // Diff highlighting signals for KateCodeView integration
    void toolCallHighlightRequested(const QString &toolCallId, const ToolCall &toolCall);
    void toolCallClearRequested(const QString &toolCallId);

    // Edit navigation signal
    void jumpToEditRequested(const QString &filePath, int startLine, int endLine);

    // Debug logging signal (forwarded to Kate Output view by KateCodeView)
    void debugLogMessage(const QString &message);

    // User question response (MCP AskUserQuestion tool)
    void userQuestionAnswered(const QString &requestId, const QString &responseJson);

private Q_SLOTS:
    void onConnectClicked();
    void onResumeSessionClicked();
    void onNewSessionClicked();
    void onStopClicked();
    void onMessageSubmitted(const QString &message);
    void onPermissionModeChanged(const QString &mode);
    void onStatusChanged(ConnectionStatus status);
    void onMessageAdded(const Message &message);
    void onMessageUpdated(const QString &messageId, const QString &content);
    void onMessageFinished(const QString &messageId);
    void onToolCallAdded(const QString &messageId, const ToolCall &toolCall);
    void onToolCallUpdated(const QString &messageId, const QString &toolCallId, const QString &status, const QString &result, const QString &filePath = QString(), const QString &toolName = QString());
    void onTodosUpdated(const QList<TodoItem> &todos);
    void onPermissionRequested(const PermissionRequest &request);
    void onModesAvailable(const QJsonArray &modes);
    void onModeChanged(const QString &modeId);
    void onError(const QString &message);
    void onSessionError(const QString &message);
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
    void onSettingsChanged();

    // User question handling
    void onUserQuestionAnswered(const QString &requestId, const QJsonObject &answers);

private:
    void triggerSummaryGeneration();
    // Resolve prior-session context for resuming (summary, on-demand summary, or raw transcript).
    QString resolveResumeContext(const QString &projectRoot, const QString &sessionId);
    void applyDiffColors();
    void applyACPBackend();
    void populateProviderCombo();
    void onProviderComboChanged(int index);
    void updateTerminalSize();
    void resetWebView();
    void connectWebViewSignals();
    // Returns false (and shows a system message) if another window holds the
    // agent slot; returns true if this widget can proceed to start a session.
    bool ensureAgentSlot();
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

    // Agent-slot hooks (set by KateCodeView; default-empty = no gate)
    std::function<bool()> m_tryAcquireAgentSlot;
    std::function<void()> m_releaseAgentSlot;
    std::function<bool()> m_agentSlotAvailable;

    // Last-seen connection status, kept in sync at the top of onStatusChanged()
    ConnectionStatus m_lastStatus = ConnectionStatus::Disconnected;

    // Context chunks
    QList<ContextChunk> m_contextChunks;
    int m_nextChunkId = 0;

    // Image attachments
    QList<ImageAttachment> m_imageAttachments;
    int m_nextImageId = 0;

    // UI components
    ChatWebView *m_chatWebView;
    ChatInputWidget *m_inputWidget;
    QComboBox *m_providerCombo;
    QToolButton *m_connectButton;
    QToolButton *m_resumeSessionButton;
    QToolButton *m_newSessionButton;
    QToolButton *m_clearOutputButton;
    QToolButton *m_saveOutputButton;
    QLabel *m_titleLabel;
    QLabel *m_statusIndicator;
    QWidget *m_contextChipsContainer;
    // Splitter separating the chat view from the input area
    QSplitter *m_splitter;
    QWidget *m_inputArea;

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
    // Whether the pending context should be injected once Connected. Set for
    // option A (context injection) and when option B (true resume) falls back.
    bool m_injectContextOnConnect = false;

    // Writes raw ACP JSON traffic to a per-session file when enabled in settings.
    ACPLogger *m_acpLogger;

    // Track whether user has sent a message (for summary generation decision)
    bool m_userSentMessage = false;

    // Track pending summary generation waiting for API key
    bool m_pendingSummaryAfterKeyLoad = false;

    // Set to true by prepareForShutdown() so a second call is a no-op.
    bool m_shutdownDone = false;
};
