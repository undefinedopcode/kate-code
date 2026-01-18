#include "ChatWidget.h"
#include "ChatWebView.h"
#include "ChatInputWidget.h"
#include "PermissionDialog.h"
#include "SessionSelectionDialog.h"
#include "../acp/ACPSession.h"
#include "../config/SettingsStore.h"
#include "../util/SessionStore.h"
#include "../util/SummaryGenerator.h"
#include "../util/SummaryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

ChatWidget::ChatWidget(QWidget *parent)
    : QWidget(parent)
    , m_session(new ACPSession(this))
    , m_sessionStore(new SessionStore(this))
    , m_pendingAction(PendingAction::None)
    , m_settingsStore(nullptr)
    , m_summaryStore(new SummaryStore(this))
    , m_summaryGenerator(nullptr)
{
    // Create layout
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Header bar
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(4, 4, 4, 4);
    headerLayout->setSpacing(8);

    // Title: "Kate Code - Session"
    m_titleLabel = new QLabel(QStringLiteral("Kate Code - Session"), this);
    m_titleLabel->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; }"));
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();

    // New Session button (icon only)
    m_newSessionButton = new QToolButton(this);
    m_newSessionButton->setIcon(QIcon::fromTheme(QStringLiteral("document-new")));
    m_newSessionButton->setToolTip(QStringLiteral("New Session"));
    m_newSessionButton->setAutoRaise(true);
    m_newSessionButton->setEnabled(false);
    headerLayout->addWidget(m_newSessionButton);

    // Connect/Disconnect button (icon only)
    m_connectButton = new QToolButton(this);
    m_connectButton->setIcon(QIcon::fromTheme(QStringLiteral("network-connect")));
    m_connectButton->setToolTip(QStringLiteral("Connect"));
    m_connectButton->setAutoRaise(true);
    headerLayout->addWidget(m_connectButton);

    // Connection status indicator (colored dot)
    m_statusIndicator = new QLabel(QStringLiteral("●"), this);
    m_statusIndicator->setStyleSheet(QStringLiteral("QLabel { color: #888888; font-size: 14px; }"));
    m_statusIndicator->setToolTip(QStringLiteral("Disconnected"));
    headerLayout->addWidget(m_statusIndicator);

    layout->addLayout(headerLayout);

    // Chat web view
    m_chatWebView = new ChatWebView(this);
    m_chatWebView->setMinimumHeight(200);
    m_chatWebView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_chatWebView, 1);

    // Context chips container (for displaying added context chunks)
    m_contextChipsContainer = new QWidget(this);
    auto *chipsLayout = new QHBoxLayout(m_contextChipsContainer);
    chipsLayout->setContentsMargins(4, 2, 4, 2);
    chipsLayout->setSpacing(4);
    chipsLayout->addStretch();
    m_contextChipsContainer->setVisible(false);  // Hidden until chunks are added
    layout->addWidget(m_contextChipsContainer);

    // Message input
    m_inputWidget = new ChatInputWidget(this);
    m_inputWidget->setEnabled(false);
    m_inputWidget->setMinimumHeight(60);
    layout->addWidget(m_inputWidget);

    // Connect signals
    connect(m_connectButton, &QPushButton::clicked, this, &ChatWidget::onConnectClicked);
    connect(m_newSessionButton, &QPushButton::clicked, this, &ChatWidget::onNewSessionClicked);
    connect(m_inputWidget, &ChatInputWidget::messageSubmitted, this, &ChatWidget::onMessageSubmitted);
    connect(m_inputWidget, &ChatInputWidget::permissionModeChanged, this, &ChatWidget::onPermissionModeChanged);
    connect(m_inputWidget, &ChatInputWidget::stopClicked, this, &ChatWidget::onStopClicked);

    // Connect ACP session signals
    connect(m_session, &ACPSession::statusChanged, this, &ChatWidget::onStatusChanged);
    connect(m_session, &ACPSession::messageAdded, this, &ChatWidget::onMessageAdded);
    connect(m_session, &ACPSession::messageUpdated, this, &ChatWidget::onMessageUpdated);
    connect(m_session, &ACPSession::messageFinished, this, &ChatWidget::onMessageFinished);
    connect(m_session, &ACPSession::toolCallAdded, this, &ChatWidget::onToolCallAdded);
    connect(m_session, &ACPSession::toolCallUpdated, this, &ChatWidget::onToolCallUpdated);
    connect(m_session, &ACPSession::todosUpdated, this, &ChatWidget::onTodosUpdated);
    connect(m_session, &ACPSession::permissionRequested, this, &ChatWidget::onPermissionRequested);
    connect(m_session, &ACPSession::modesAvailable, this, &ChatWidget::onModesAvailable);
    connect(m_session, &ACPSession::modeChanged, this, &ChatWidget::onModeChanged);
    connect(m_session, &ACPSession::commandsAvailable, m_inputWidget, &ChatInputWidget::setAvailableCommands);
    connect(m_session, &ACPSession::errorOccurred, this, &ChatWidget::onError);
    connect(m_session, &ACPSession::promptCancelled, this, &ChatWidget::onPromptCancelled);

    // Session persistence signals
    connect(m_session, &ACPSession::initializeComplete, this, &ChatWidget::onInitializeComplete);
    connect(m_session, &ACPSession::sessionLoadFailed, this, &ChatWidget::onSessionLoadFailed);

    // Connect web view permission responses back to ACP
    connect(m_chatWebView, &ChatWebView::permissionResponseReady, this, [this](int requestId, const QString &optionId) {
        QJsonObject outcomeObj;
        outcomeObj[QStringLiteral("outcome")] = QStringLiteral("selected");
        outcomeObj[QStringLiteral("optionId")] = optionId;
        m_session->sendPermissionResponse(requestId, outcomeObj);
    });
}

ChatWidget::~ChatWidget()
{
}

void ChatWidget::prepareForShutdown()
{
    qDebug() << "[ChatWidget] prepareForShutdown called";

    // Trigger summary generation for active session
    triggerSummaryGeneration();

    // Wait for summary to complete (if one was triggered)
    if (m_summaryGenerator && m_summaryGenerator->isGenerating()) {
        qDebug() << "[ChatWidget] Waiting for summary generation to complete...";
        m_summaryGenerator->waitForPendingRequests();
    }

    qDebug() << "[ChatWidget] Shutdown preparation complete";
}

void ChatWidget::setFilePathProvider(ContextProvider provider)
{
    m_filePathProvider = provider;
}

void ChatWidget::setSelectionProvider(ContextProvider provider)
{
    m_selectionProvider = provider;
}

void ChatWidget::setProjectRootProvider(ContextProvider provider)
{
    m_projectRootProvider = provider;
}

void ChatWidget::setFileListProvider(FileListProvider provider)
{
    m_fileListProvider = provider;
}

void ChatWidget::setSettingsStore(SettingsStore *settings)
{
    m_settingsStore = settings;

    // Create summary generator now that we have settings
    if (m_settingsStore && !m_summaryGenerator) {
        m_summaryGenerator = new SummaryGenerator(m_settingsStore, this);
        connect(m_summaryGenerator, &SummaryGenerator::summaryReady,
                this, &ChatWidget::onSummaryReady);
        connect(m_summaryGenerator, &SummaryGenerator::summaryError,
                this, &ChatWidget::onSummaryError);

        // Load API key from KWallet for summary generation
        m_settingsStore->loadApiKey();
    }
}

void ChatWidget::onConnectClicked()
{
    if (m_session->isConnected()) {
        m_session->stop();
        return;
    }

    // Get current project root
    QString projectRoot = m_projectRootProvider ? m_projectRootProvider() : QDir::homePath();

    // Clear any pending summary from previous attempt
    m_pendingSummaryContext.clear();

    // Check if a previous session exists with a summary
    QString lastSessionId = m_sessionStore->getLastSession(projectRoot);
    if (!lastSessionId.isEmpty()) {
        QString summary = m_summaryStore->loadSummary(projectRoot, lastSessionId);
        if (!summary.isEmpty()) {
            // Show dialog to ask user whether to resume with context
            SessionSelectionDialog dialog(lastSessionId, summary, this);
            if (dialog.exec() == QDialog::Accepted) {
                if (dialog.selectedResult() == SessionSelectionDialog::Result::Resume) {
                    // Store summary to send after session connects
                    m_pendingSummaryContext = summary;
                }
                // NewSession: just proceed without summary context
            } else {
                // Cancelled - don't connect
                return;
            }
        }
    }

    m_pendingAction = PendingAction::CreateSession;

    // Add system message
    Message sysMsg;
    sysMsg.id = QStringLiteral("sys_connect");
    sysMsg.role = QStringLiteral("system");
    sysMsg.timestamp = QDateTime::currentDateTime();
    if (m_pendingSummaryContext.isEmpty()) {
        sysMsg.content = QStringLiteral("Starting new session in: %1").arg(projectRoot);
    } else {
        sysMsg.content = QStringLiteral("Resuming session with prior context in: %1").arg(projectRoot);
    }
    m_chatWebView->addMessage(sysMsg);

    m_session->start(projectRoot);
}

void ChatWidget::onNewSessionClicked()
{
    // Get current project root
    QString projectRoot = m_projectRootProvider ? m_projectRootProvider() : QDir::homePath();

    // Clear stored session and any pending summary context
    m_sessionStore->clearSession(projectRoot);
    m_pendingSummaryContext.clear();

    // Stop current session, clear chat, and start a new session
    m_session->stop();
    m_chatWebView->clearMessages();

    m_pendingAction = PendingAction::CreateSession;

    // Add system message for new session
    Message sysMsg;
    sysMsg.id = QStringLiteral("sys_newsession");
    sysMsg.role = QStringLiteral("system");
    sysMsg.content = QStringLiteral("Starting new session in: %1").arg(projectRoot);
    sysMsg.timestamp = QDateTime::currentDateTime();
    m_chatWebView->addMessage(sysMsg);

    m_session->start(projectRoot);
}

void ChatWidget::onStopClicked()
{
    qDebug() << "[ChatWidget] Stop clicked, cancelling prompt";
    m_session->cancelPrompt();
}

void ChatWidget::onPromptCancelled()
{
    qDebug() << "[ChatWidget] Prompt cancelled";
    m_inputWidget->setPromptRunning(false);

    // Add system message to indicate cancellation
    Message sysMsg;
    sysMsg.id = QStringLiteral("sys_cancelled");
    sysMsg.role = QStringLiteral("system");
    sysMsg.content = QStringLiteral("Generation stopped");
    sysMsg.timestamp = QDateTime::currentDateTime();
    m_chatWebView->addMessage(sysMsg);
}

void ChatWidget::onMessageSubmitted(const QString &message)
{
    // Get current Kate context
    QString filePath = m_filePathProvider ? m_filePathProvider() : QString();
    QString selection = m_selectionProvider ? m_selectionProvider() : QString();

    // Send message with context (including added context chunks)
    m_session->sendMessage(message, filePath, selection, m_contextChunks);

    // Clear context chunks after sending
    clearContextChunks();
}

void ChatWidget::onStatusChanged(ConnectionStatus status)
{
    Message sysMsg;
    sysMsg.role = QStringLiteral("system");
    sysMsg.timestamp = QDateTime::currentDateTime();

    switch (status) {
    case ConnectionStatus::Disconnected:
        m_connectButton->setIcon(QIcon::fromTheme(QStringLiteral("network-connect")));
        m_connectButton->setToolTip(QStringLiteral("Connect"));
        m_connectButton->setEnabled(true);
        m_newSessionButton->setEnabled(false);
        m_inputWidget->setEnabled(false);
        m_statusIndicator->setStyleSheet(QStringLiteral("QLabel { color: #888888; font-size: 14px; }"));
        m_statusIndicator->setToolTip(QStringLiteral("Disconnected"));
        m_titleLabel->setText(QStringLiteral("Kate Code - Session"));
        sysMsg.id = QStringLiteral("sys_disconnected");
        sysMsg.content = QStringLiteral("Disconnected from claude-code-acp");
        m_chatWebView->addMessage(sysMsg);

        // Trigger summary generation for the ended session
        triggerSummaryGeneration();
        break;
    case ConnectionStatus::Connecting:
        m_connectButton->setEnabled(false);
        m_newSessionButton->setEnabled(false);
        m_statusIndicator->setStyleSheet(QStringLiteral("QLabel { color: #f0ad4e; font-size: 14px; }"));
        m_statusIndicator->setToolTip(QStringLiteral("Connecting..."));
        sysMsg.id = QStringLiteral("sys_connecting");
        sysMsg.content = QStringLiteral("Initializing ACP protocol...");
        m_chatWebView->addMessage(sysMsg);
        break;
    case ConnectionStatus::Connected:
        m_connectButton->setIcon(QIcon::fromTheme(QStringLiteral("network-disconnect")));
        m_connectButton->setToolTip(QStringLiteral("Disconnect"));
        m_connectButton->setEnabled(true);
        m_newSessionButton->setEnabled(true);
        m_inputWidget->setEnabled(true);
        m_statusIndicator->setStyleSheet(QStringLiteral("QLabel { color: #5cb85c; font-size: 14px; }"));
        m_statusIndicator->setToolTip(QStringLiteral("Connected"));
        m_titleLabel->setText(QStringLiteral("Kate Code - Session"));
        sysMsg.id = QStringLiteral("sys_connected");
        sysMsg.content = QStringLiteral("Connected! Session ID: %1").arg(m_session->sessionId());
        m_chatWebView->addMessage(sysMsg);

        // Save session ID for future resume and summary generation
        {
            QString projectRoot = m_projectRootProvider ? m_projectRootProvider() : QDir::homePath();
            m_sessionStore->saveSession(projectRoot, m_session->sessionId());
            m_lastSessionId = m_session->sessionId();
            m_lastProjectRoot = projectRoot;
        }

        // Populate file list for @-completion
        if (m_fileListProvider) {
            QStringList files = m_fileListProvider();
            m_inputWidget->setAvailableFiles(files);
        }

        // Auto-send summary context if resuming a session
        if (!m_pendingSummaryContext.isEmpty()) {
            QString contextMessage = QStringLiteral(
                "Summary from last session:\n\n%1").arg(m_pendingSummaryContext);
            m_session->sendMessage(contextMessage, QString(), QString(), {});
            m_pendingSummaryContext.clear();
        }
        break;
    case ConnectionStatus::Error:
        m_connectButton->setIcon(QIcon::fromTheme(QStringLiteral("network-connect")));
        m_connectButton->setToolTip(QStringLiteral("Connect"));
        m_connectButton->setEnabled(true);
        m_newSessionButton->setEnabled(false);
        m_statusIndicator->setStyleSheet(QStringLiteral("QLabel { color: #d9534f; font-size: 14px; }"));
        m_statusIndicator->setToolTip(QStringLiteral("Error"));
        break;
    }
}

void ChatWidget::onMessageAdded(const Message &message)
{
    m_chatWebView->addMessage(message);

    // Track when assistant starts responding (prompt is running)
    if (message.role == QStringLiteral("assistant")) {
        m_inputWidget->setPromptRunning(true);
    }
}

void ChatWidget::onMessageUpdated(const QString &messageId, const QString &content)
{
    m_chatWebView->updateMessage(messageId, content);
}

void ChatWidget::onMessageFinished(const QString &messageId)
{
    m_chatWebView->finishMessage(messageId);

    // Prompt finished - update running state
    m_inputWidget->setPromptRunning(false);
}

void ChatWidget::onToolCallAdded(const QString &messageId, const ToolCall &toolCall)
{
    m_chatWebView->addToolCall(messageId, toolCall);

    // Request diff highlighting for edits (Edit tool)
    if (!toolCall.edits.isEmpty()) {
        Q_EMIT toolCallHighlightRequested(toolCall.id, toolCall);
    }
}

void ChatWidget::onToolCallUpdated(const QString &messageId, const QString &toolCallId, const QString &status, const QString &result)
{
    Q_UNUSED(messageId);
    m_chatWebView->updateToolCall(messageId, toolCallId, status, result);

    // Clear highlights when tool call completes or fails
    if (status == QStringLiteral("completed") || status == QStringLiteral("failed")) {
        Q_EMIT toolCallClearRequested(toolCallId);
    }
}

void ChatWidget::onTodosUpdated(const QList<TodoItem> &todos)
{
    m_chatWebView->updateTodos(todos);
}

void ChatWidget::onPermissionRequested(const PermissionRequest &request)
{
    // Show inline permission request in web view
    m_chatWebView->showPermissionRequest(request);
}

void ChatWidget::onError(const QString &message)
{
    // Log errors to console instead of showing popups
    // Many "errors" from ACP are just informational stderr output
    qWarning() << "[ChatWidget] ACP error:" << message;
}

void ChatWidget::onInitializeComplete()
{
    qDebug() << "[ChatWidget] Initialize complete, pending action:" << static_cast<int>(m_pendingAction);

    switch (m_pendingAction) {
    case PendingAction::LoadSession:
        m_session->loadSession(m_pendingSessionId);
        break;
    case PendingAction::CreateSession:
        m_session->createNewSession();
        break;
    case PendingAction::None:
        // Fallback: create new session if no action was set
        qWarning() << "[ChatWidget] No pending action set, creating new session";
        m_session->createNewSession();
        break;
    }

    m_pendingAction = PendingAction::None;
    m_pendingSessionId.clear();
}

void ChatWidget::onSessionLoadFailed(const QString &error)
{
    qWarning() << "[ChatWidget] Session load failed, creating new:" << error;

    // Clear stale session from storage
    QString projectRoot = m_projectRootProvider ? m_projectRootProvider() : QDir::homePath();
    m_sessionStore->clearSession(projectRoot);

    // Show system message
    Message sysMsg;
    sysMsg.id = QStringLiteral("sys_load_failed");
    sysMsg.role = QStringLiteral("system");
    sysMsg.content = QStringLiteral("Previous session unavailable, starting new session");
    sysMsg.timestamp = QDateTime::currentDateTime();
    m_chatWebView->addMessage(sysMsg);

    // Create new session
    m_session->createNewSession();
}

void ChatWidget::onPermissionModeChanged(const QString &mode)
{
    qDebug() << "[ChatWidget] User changed mode to:" << mode;
    m_session->setMode(mode);
}

void ChatWidget::onModesAvailable(const QJsonArray &modes)
{
    qDebug() << "[ChatWidget] Modes available:" << modes.size();
    m_inputWidget->setAvailableModes(modes);
}

void ChatWidget::onModeChanged(const QString &modeId)
{
    qDebug() << "[ChatWidget] Mode changed to:" << modeId;
    m_inputWidget->setCurrentMode(modeId);
}

void ChatWidget::addContextChunk(const QString &filePath, int startLine, int endLine, const QString &content)
{
    ContextChunk chunk;
    chunk.filePath = filePath;
    chunk.startLine = startLine;
    chunk.endLine = endLine;
    chunk.content = content;
    chunk.id = QString::number(m_nextChunkId++);

    m_contextChunks.append(chunk);
    updateContextChipsDisplay();

    qDebug() << "[ChatWidget] Added context chunk:" << filePath << "lines" << startLine << "-" << endLine;
}

void ChatWidget::removeContextChunk(const QString &id)
{
    for (int i = 0; i < m_contextChunks.size(); ++i) {
        if (m_contextChunks[i].id == id) {
            m_contextChunks.removeAt(i);
            updateContextChipsDisplay();
            qDebug() << "[ChatWidget] Removed context chunk:" << id;
            return;
        }
    }
}

void ChatWidget::clearContextChunks()
{
    m_contextChunks.clear();
    updateContextChipsDisplay();
    qDebug() << "[ChatWidget] Cleared all context chunks";
}

void ChatWidget::sendPromptWithSelection(const QString &prompt, const QString &filePath, const QString &selection)
{
    if (!m_session || !m_session->isConnected()) {
        qWarning() << "[ChatWidget] Cannot send quick action: not connected to ACP";
        return;
    }

    // Send prompt with selection directly to ACP (no context chunks for quick actions)
    m_session->sendMessage(prompt, filePath, selection, QList<ContextChunk>());

    qDebug() << "[ChatWidget] Sent quick action prompt with selection from:" << filePath;
}

void ChatWidget::onRemoveContextChunk(const QString &id)
{
    removeContextChunk(id);
}

void ChatWidget::updateContextChipsDisplay()
{
    // Clear existing chips
    QLayout *layout = m_contextChipsContainer->layout();
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    if (m_contextChunks.isEmpty()) {
        m_contextChipsContainer->setVisible(false);
        return;
    }

    m_contextChipsContainer->setVisible(true);

    // Add chips for each context chunk
    for (const ContextChunk &chunk : m_contextChunks) {
        QFileInfo fileInfo(chunk.filePath);
        QString label = QStringLiteral("%1:%2-%3")
                           .arg(fileInfo.fileName())
                           .arg(chunk.startLine)
                           .arg(chunk.endLine);

        auto *chipWidget = new QWidget(this);
        auto *chipLayout = new QHBoxLayout(chipWidget);
        chipLayout->setContentsMargins(6, 2, 6, 2);
        chipLayout->setSpacing(4);

        auto *chipLabel = new QLabel(label, chipWidget);
        chipLabel->setStyleSheet(QStringLiteral("QLabel { color: palette(text); }"));

        auto *removeButton = new QPushButton(QStringLiteral("×"), chipWidget);
        removeButton->setFixedSize(16, 16);
        removeButton->setStyleSheet(QStringLiteral(
            "QPushButton { "
            "border: none; "
            "background: transparent; "
            "color: palette(text); "
            "font-weight: bold; "
            "} "
            "QPushButton:hover { "
            "background: rgba(255, 0, 0, 0.3); "
            "}"));

        connect(removeButton, &QPushButton::clicked, this, [this, id = chunk.id]() {
            onRemoveContextChunk(id);
        });

        chipLayout->addWidget(chipLabel);
        chipLayout->addWidget(removeButton);

        chipWidget->setStyleSheet(QStringLiteral(
            "QWidget { "
            "background-color: palette(alternate-base); "
            "border: 1px solid palette(mid); "
            "border-radius: 3px; "
            "}"));

        layout->addWidget(chipWidget);
    }

    // Add stretch to keep chips left-aligned
    qobject_cast<QHBoxLayout*>(layout)->addStretch();
}

void ChatWidget::triggerSummaryGeneration()
{
    qDebug() << "[ChatWidget] triggerSummaryGeneration called";
    qDebug() << "[ChatWidget]   m_lastSessionId:" << m_lastSessionId;
    qDebug() << "[ChatWidget]   m_lastProjectRoot:" << m_lastProjectRoot;
    qDebug() << "[ChatWidget]   m_settingsStore:" << (m_settingsStore ? "set" : "null");
    qDebug() << "[ChatWidget]   m_summaryGenerator:" << (m_summaryGenerator ? "set" : "null");

    // Check if we have what we need for summary generation
    if (m_lastSessionId.isEmpty() || m_lastProjectRoot.isEmpty()) {
        qDebug() << "[ChatWidget] No session to summarize";
        return;
    }

    if (!m_settingsStore || !m_summaryGenerator) {
        qDebug() << "[ChatWidget] Settings or summary generator not available";
        return;
    }

    qDebug() << "[ChatWidget]   summariesEnabled:" << m_settingsStore->summariesEnabled();
    qDebug() << "[ChatWidget]   hasApiKey:" << m_settingsStore->hasApiKey();

    if (!m_settingsStore->summariesEnabled()) {
        qDebug() << "[ChatWidget] Summaries disabled in settings";
        return;
    }

    if (!m_settingsStore->hasApiKey()) {
        qDebug() << "[ChatWidget] No API key configured for summaries";
        return;
    }

    // Read transcript content
    QString transcriptPath = QDir::homePath() +
        QStringLiteral("/.kate-code/transcripts/") +
        m_lastProjectRoot.mid(1).replace(QLatin1Char('/'), QLatin1Char('_')) +
        QStringLiteral("/") + m_lastSessionId + QStringLiteral(".md");

    qDebug() << "[ChatWidget] Looking for transcript at:" << transcriptPath;

    QFile transcriptFile(transcriptPath);
    if (!transcriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[ChatWidget] Could not read transcript:" << transcriptPath;
        return;
    }

    QString transcriptContent = QString::fromUtf8(transcriptFile.readAll());
    transcriptFile.close();

    qDebug() << "[ChatWidget] Transcript length:" << transcriptContent.length();

    if (transcriptContent.isEmpty()) {
        qDebug() << "[ChatWidget] Empty transcript, skipping summary";
        return;
    }

    qDebug() << "[ChatWidget] Generating summary for session:" << m_lastSessionId;
    m_summaryGenerator->generateSummary(m_lastSessionId, m_lastProjectRoot, transcriptContent);
}

void ChatWidget::onSummaryReady(const QString &sessionId, const QString &projectRoot, const QString &summary)
{
    qDebug() << "[ChatWidget] Summary generated for session:" << sessionId;

    // Save summary to file
    m_summaryStore->saveSummary(projectRoot, sessionId, summary);

    // Add system message about summary
    Message sysMsg;
    sysMsg.id = QStringLiteral("sys_summary");
    sysMsg.role = QStringLiteral("system");
    sysMsg.content = QStringLiteral("Session summary saved to ~/.kate-code/summaries/");
    sysMsg.timestamp = QDateTime::currentDateTime();
    m_chatWebView->addMessage(sysMsg);
}

void ChatWidget::onSummaryError(const QString &sessionId, const QString &error)
{
    qWarning() << "[ChatWidget] Summary generation failed for" << sessionId << ":" << error;
    // Don't show error to user - summary is optional
}
