#include "ChatWidget.h"
#include "ChatWebView.h"
#include "ChatInputWidget.h"
#include "PermissionDialog.h"
#include "../acp/ACPSession.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ChatWidget::ChatWidget(QWidget *parent)
    : QWidget(parent)
    , m_session(new ACPSession(this))
{
    // Create layout
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Status bar
    auto *statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(4, 4, 4, 4);
    m_statusLabel = new QLabel(QStringLiteral("Disconnected"), this);
    m_newSessionButton = new QPushButton(QStringLiteral("New Session"), this);
    m_newSessionButton->setEnabled(false);
    m_connectButton = new QPushButton(QStringLiteral("Connect"), this);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_newSessionButton);
    statusLayout->addWidget(m_connectButton);
    layout->addLayout(statusLayout);

    // Chat web view
    m_chatWebView = new ChatWebView(this);
    m_chatWebView->setMinimumHeight(200);
    m_chatWebView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_chatWebView, 1);

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

void ChatWidget::onConnectClicked()
{
    if (m_session->isConnected()) {
        m_session->stop();
    } else {
        // Get current project root
        QString projectRoot = m_projectRootProvider ? m_projectRootProvider() : QDir::homePath();

        // Add system message
        Message sysMsg;
        sysMsg.id = QStringLiteral("sys_connect");
        sysMsg.role = QStringLiteral("system");
        sysMsg.content = QStringLiteral("Connecting to claude-code-acp in: %1").arg(projectRoot);
        sysMsg.timestamp = QDateTime::currentDateTime();
        m_chatWebView->addMessage(sysMsg);

        m_session->start(projectRoot);
    }
}

void ChatWidget::onNewSessionClicked()
{
    // Stop current session, clear chat, and start a new session
    m_session->stop();
    m_chatWebView->clearMessages();

    // Get current project root
    QString projectRoot = m_projectRootProvider ? m_projectRootProvider() : QDir::homePath();

    // Add system message for new session
    Message sysMsg;
    sysMsg.id = QStringLiteral("sys_newsession");
    sysMsg.role = QStringLiteral("system");
    sysMsg.content = QStringLiteral("Starting new session in: %1").arg(projectRoot);
    sysMsg.timestamp = QDateTime::currentDateTime();
    m_chatWebView->addMessage(sysMsg);

    m_session->start(projectRoot);
}

void ChatWidget::onMessageSubmitted(const QString &message)
{
    // Get current Kate context
    QString filePath = m_filePathProvider ? m_filePathProvider() : QString();
    QString selection = m_selectionProvider ? m_selectionProvider() : QString();

    // Send message with context
    m_session->sendMessage(message, filePath, selection);
}

void ChatWidget::onStatusChanged(ConnectionStatus status)
{
    QString statusText;
    Message sysMsg;
    sysMsg.role = QStringLiteral("system");
    sysMsg.timestamp = QDateTime::currentDateTime();

    switch (status) {
    case ConnectionStatus::Disconnected:
        statusText = QStringLiteral("Disconnected");
        m_connectButton->setText(QStringLiteral("Connect"));
        m_connectButton->setEnabled(true);
        m_newSessionButton->setEnabled(false);
        m_inputWidget->setEnabled(false);
        sysMsg.id = QStringLiteral("sys_disconnected");
        sysMsg.content = QStringLiteral("Disconnected from claude-code-acp");
        m_chatWebView->addMessage(sysMsg);
        break;
    case ConnectionStatus::Connecting:
        statusText = QStringLiteral("Connecting...");
        m_connectButton->setEnabled(false);
        m_newSessionButton->setEnabled(false);
        sysMsg.id = QStringLiteral("sys_connecting");
        sysMsg.content = QStringLiteral("Initializing ACP protocol...");
        m_chatWebView->addMessage(sysMsg);
        break;
    case ConnectionStatus::Connected:
        statusText = QStringLiteral("Connected");
        m_connectButton->setText(QStringLiteral("Disconnect"));
        m_connectButton->setEnabled(true);
        m_newSessionButton->setEnabled(true);
        m_inputWidget->setEnabled(true);
        sysMsg.id = QStringLiteral("sys_connected");
        sysMsg.content = QStringLiteral("Connected! Session ID: %1").arg(m_session->sessionId());
        m_chatWebView->addMessage(sysMsg);

        // Populate file list for @-completion
        if (m_fileListProvider) {
            QStringList files = m_fileListProvider();
            m_inputWidget->setAvailableFiles(files);
        }
        break;
    case ConnectionStatus::Error:
        statusText = QStringLiteral("Error");
        m_connectButton->setText(QStringLiteral("Connect"));
        m_connectButton->setEnabled(true);
        m_newSessionButton->setEnabled(false);
        break;
    }

    m_statusLabel->setText(statusText);
}

void ChatWidget::onMessageAdded(const Message &message)
{
    m_chatWebView->addMessage(message);
}

void ChatWidget::onMessageUpdated(const QString &messageId, const QString &content)
{
    m_chatWebView->updateMessage(messageId, content);
}

void ChatWidget::onMessageFinished(const QString &messageId)
{
    m_chatWebView->finishMessage(messageId);
}

void ChatWidget::onToolCallAdded(const QString &messageId, const ToolCall &toolCall)
{
    m_chatWebView->addToolCall(messageId, toolCall);
}

void ChatWidget::onToolCallUpdated(const QString &messageId, const QString &toolCallId, const QString &status, const QString &result)
{
    m_chatWebView->updateToolCall(messageId, toolCallId, status, result);
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
