#include "ChatInputWidget.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextCursor>
#include <QVBoxLayout>

// ============================================================================
// CommandTextEdit Implementation
// ============================================================================

CommandTextEdit::CommandTextEdit(QWidget *parent)
    : QTextEdit(parent)
{
}

void CommandTextEdit::setCompleter(QCompleter *completer)
{
    if (m_completer) {
        disconnect(m_completer, nullptr, this, nullptr);
    }

    m_completer = completer;

    if (!m_completer) {
        return;
    }

    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);

    connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated),
            this, &CommandTextEdit::insertCompletion);
}

void CommandTextEdit::setModels(QAbstractItemModel *commandModel, QAbstractItemModel *fileModel)
{
    m_commandModel = commandModel;
    m_fileModel = fileModel;
}

void CommandTextEdit::keyPressEvent(QKeyEvent *e)
{
    if (m_completer && m_completer->popup()->isVisible()) {
        // Let completer handle these keys
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return;
        default:
            break;
        }
    }

    // Check for send message shortcut (Enter without Shift)
    bool isShortcut = (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
                      !(e->modifiers() & Qt::ShiftModifier);

    if (!m_completer || !isShortcut) {
        QTextEdit::keyPressEvent(e);
    }

    if (!m_completer) {
        return;
    }

    // Don't trigger completer on navigation/delete keys
    if (e->key() == Qt::Key_Backspace ||
        e->key() == Qt::Key_Delete ||
        e->key() == Qt::Key_Left ||
        e->key() == Qt::Key_Right ||
        e->key() == Qt::Key_Up ||
        e->key() == Qt::Key_Down ||
        e->key() == Qt::Key_Home ||
        e->key() == Qt::Key_End ||
        e->key() == Qt::Key_PageUp ||
        e->key() == Qt::Key_PageDown) {
        return;
    }

    // Log Ctrl+V paste events
    if (e->modifiers().testFlag(Qt::ControlModifier) && e->key() == Qt::Key_V) {
        qDebug() << "[ChatInputWidget] Ctrl+V detected - paste event";
    }

    // Don't trigger completer on modifier keys
    const bool ctrlOrShift = e->modifiers().testFlag(Qt::ControlModifier) ||
                            e->modifiers().testFlag(Qt::ShiftModifier);
    if (ctrlOrShift && e->text().isEmpty()) {
        return;
    }

    // Get the completion context (command or file)
    CompletionContext ctx = completionUnderCursor();

    if (ctx.type != None) {
        // Switch completer model based on context type
        if (ctx.type == Command && m_commandModel) {
            m_completer->setModel(m_commandModel);
            m_completer->setFilterMode(Qt::MatchStartsWith);  // Commands match at start
        } else if (ctx.type == File && m_fileModel) {
            m_completer->setModel(m_fileModel);
            m_completer->setFilterMode(Qt::MatchContains);  // Files match anywhere (contains)
        } else {
            // No model available for this context
            m_completer->popup()->hide();
            return;
        }

        m_completer->setCompletionPrefix(ctx.filterText);

        if (m_completer->completionCount() > 0 || ctx.filterText.isEmpty()) {
            // Position popup at cursor
            QRect cr = cursorRect();
            cr.setWidth(m_completer->popup()->sizeHintForColumn(0) +
                       m_completer->popup()->verticalScrollBar()->sizeHint().width());
            m_completer->complete(cr);
        } else {
            m_completer->popup()->hide();
        }
    } else {
        m_completer->popup()->hide();
    }
}

void CommandTextEdit::insertCompletion(const QString &completion)
{
    if (!m_completer) {
        return;
    }

    QTextCursor tc = textCursor();

    // Find what we're completing
    CompletionContext ctx = completionUnderCursor();
    if (ctx.type == None) {
        return;
    }

    // Move to start of prefix and select it
    tc.setPosition(ctx.prefixStart);
    tc.setPosition(textCursor().position(), QTextCursor::KeepAnchor);

    if (ctx.type == Command) {
        // Extract just the command name (before " - " if it exists)
        QString commandName = completion;
        int dashPos = commandName.indexOf(QStringLiteral(" - "));
        if (dashPos > 0) {
            commandName = commandName.left(dashPos);
        }

        // Insert the full command with leading '/' and trailing space
        tc.insertText(QStringLiteral("/") + commandName + QStringLiteral(" "));
    } else if (ctx.type == File) {
        // Insert the file reference with '@' prefix
        tc.insertText(QStringLiteral("@") + completion);
    }

    setTextCursor(tc);
}

CommandTextEdit::CompletionContext CommandTextEdit::completionUnderCursor() const
{
    CompletionContext context;
    context.type = None;
    context.prefixStart = -1;

    QTextCursor tc = textCursor();
    int cursorPos = tc.position();
    QString allText = toPlainText();

    // FIRST: Check for file reference with '@' anywhere near cursor
    // Look backwards from cursor to find '@'
    // This takes priority over slash commands to handle paths like "@src/ui/file.cpp"
    int searchPos = cursorPos - 1;

    while (searchPos >= 0) {
        QChar ch = allText.at(searchPos);

        // Found '@' - this is a file reference
        if (ch == QLatin1Char('@')) {
            context.type = File;
            context.prefix = allText.mid(searchPos, cursorPos - searchPos);
            context.filterText = context.prefix.length() > 1 ? context.prefix.mid(1) : QString();
            context.prefixStart = searchPos;
            return context;
        }

        // Stop at whitespace or newline (@ must be preceded by space/newline or be at start)
        if (ch.isSpace()) {
            break;
        }

        searchPos--;
    }

    // SECOND: Check for slash command at start of line (only if no '@' found)
    tc.movePosition(QTextCursor::StartOfLine);
    int lineStart = tc.position();
    QString lineText = allText.mid(lineStart, cursorPos - lineStart);

    if (lineText.startsWith(QLatin1Char('/'))) {
        context.type = Command;
        context.prefix = lineText;
        context.filterText = lineText.length() > 1 ? lineText.mid(1) : QString();
        context.prefixStart = lineStart;
        return context;
    }

    return context;
}

// ============================================================================
// ChatInputWidget Implementation
// ============================================================================

ChatInputWidget::ChatInputWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Mode selector row
    auto *modeLayout = new QHBoxLayout();
    modeLayout->setContentsMargins(0, 0, 0, 0);

    auto *modeLabel = new QLabel(QStringLiteral("Mode:"), this);
    m_modeComboBox = new QComboBox(this);
    m_modeComboBox->setMinimumWidth(150);

    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(m_modeComboBox);
    modeLayout->addStretch();

    mainLayout->addLayout(modeLayout);

    // Input row
    auto *inputLayout = new QHBoxLayout();
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(4);

    // Multiline text input with completer support
    m_textEdit = new CommandTextEdit(this);
    m_textEdit->setPlaceholderText(QStringLiteral("Type a message... (Enter to send, Shift+Enter for newline, / for commands)"));
    m_textEdit->setMinimumHeight(50);
    m_textEdit->setMaximumHeight(100);
    m_textEdit->installEventFilter(this);

    // Create completer
    m_completer = new QCompleter(this);
    m_completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchStartsWith);  // Match at start of string
    m_completer->setWrapAround(false);
    m_textEdit->setCompleter(m_completer);

    // Send button
    m_sendButton = new QPushButton(QStringLiteral("Send"), this);
    m_sendButton->setMinimumWidth(80);
    m_sendButton->setMinimumHeight(50);

    inputLayout->addWidget(m_textEdit, 1);
    inputLayout->addWidget(m_sendButton);

    mainLayout->addLayout(inputLayout);

    connect(m_sendButton, &QPushButton::clicked, this, &ChatInputWidget::onSendClicked);
    connect(m_modeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChatInputWidget::onModeChanged);
}

ChatInputWidget::~ChatInputWidget()
{
}

void ChatInputWidget::setEnabled(bool enabled)
{
    m_textEdit->setEnabled(enabled);
    m_sendButton->setEnabled(enabled);
    m_modeComboBox->setEnabled(enabled);
}

void ChatInputWidget::clear()
{
    m_textEdit->clear();
}

QString ChatInputWidget::text() const
{
    return m_textEdit->toPlainText();
}

QString ChatInputWidget::permissionMode() const
{
    return m_modeComboBox->currentData().toString();
}

bool ChatInputWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_textEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

        // Handle Enter without Shift = send message (when completer not visible)
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
            !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            if (!m_completer->popup()->isVisible()) {
                onSendClicked();
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

void ChatInputWidget::onSendClicked()
{
    QString message = m_textEdit->toPlainText().trimmed();
    if (!message.isEmpty()) {
        Q_EMIT messageSubmitted(message);
        m_textEdit->clear();
    }
}

void ChatInputWidget::onModeChanged(int index)
{
    Q_UNUSED(index);
    QString mode = m_modeComboBox->currentData().toString();
    Q_EMIT permissionModeChanged(mode);
}

void ChatInputWidget::setAvailableModes(const QJsonArray &modes)
{
    // Block signals to prevent emitting permissionModeChanged while repopulating
    m_modeComboBox->blockSignals(true);

    // Save current selection if any
    QString currentModeId = m_modeComboBox->currentData().toString();

    // Clear existing items
    m_modeComboBox->clear();

    if (modes.isEmpty()) {
        // Fallback to hardcoded defaults if ACP provides nothing
        m_modeComboBox->addItem(QStringLiteral("Default"), QStringLiteral("default"));
        m_modeComboBox->addItem(QStringLiteral("Plan"), QStringLiteral("plan"));
        m_modeComboBox->addItem(QStringLiteral("Accept Edits"), QStringLiteral("acceptEdits"));
        m_modeComboBox->addItem(QStringLiteral("Don't Ask"), QStringLiteral("dontAsk"));
        qDebug() << "[ChatInputWidget] Using fallback modes (ACP returned empty)";
    } else {
        // Populate from ACP response
        for (const QJsonValue &value : modes) {
            QJsonObject mode = value.toObject();
            QString id = mode[QStringLiteral("id")].toString();
            QString name = mode[QStringLiteral("name")].toString();
            QString description = mode[QStringLiteral("description")].toString();

            // Use name for display, id for data
            m_modeComboBox->addItem(name, id);

            // Set tooltip to description
            int lastIndex = m_modeComboBox->count() - 1;
            m_modeComboBox->setItemData(lastIndex, description, Qt::ToolTipRole);
        }
        qDebug() << "[ChatInputWidget] Loaded" << modes.size() << "modes from ACP";
    }

    // Restore previous selection if it exists in new list
    int restoredIndex = m_modeComboBox->findData(currentModeId);
    if (restoredIndex >= 0) {
        m_modeComboBox->setCurrentIndex(restoredIndex);
    }

    m_modeComboBox->blockSignals(false);
}

void ChatInputWidget::setCurrentMode(const QString &modeId)
{
    if (modeId.isEmpty()) {
        return;
    }

    // Block signals to prevent feedback loop
    m_modeComboBox->blockSignals(true);

    int index = m_modeComboBox->findData(modeId);
    if (index >= 0) {
        m_modeComboBox->setCurrentIndex(index);
        qDebug() << "[ChatInputWidget] Mode selection set to:" << modeId;
    } else {
        qWarning() << "[ChatInputWidget] Mode not found in dropdown:" << modeId;
    }

    m_modeComboBox->blockSignals(false);
}

void ChatInputWidget::setAvailableCommands(const QList<SlashCommand> &commands)
{
    m_availableCommands = commands;

    // Build string list with descriptions for display
    QStringList displayList;
    for (const SlashCommand &cmd : commands) {
        // Truncate long descriptions to keep popup compact
        QString desc = cmd.description;
        if (desc.length() > 50) {
            desc = desc.left(47) + QStringLiteral("...");
        }
        // Format: "commandname - description"
        displayList << QStringLiteral("%1 - %2").arg(cmd.name, desc);
    }

    // Create/update command model
    if (m_commandModel) {
        delete m_commandModel;
    }
    m_commandModel = new QStringListModel(displayList, this);

    // Update models in text edit
    m_textEdit->setModels(m_commandModel, m_fileModel);

    qDebug() << "[ChatInputWidget] Loaded" << commands.size() << "slash commands for QCompleter";
}

void ChatInputWidget::setAvailableFiles(const QStringList &files)
{
    m_availableFiles = files;

    // Create/update file model
    if (m_fileModel) {
        delete m_fileModel;
    }
    m_fileModel = new QStringListModel(files, this);

    // Update models in text edit
    m_textEdit->setModels(m_commandModel, m_fileModel);

    qDebug() << "[ChatInputWidget] Loaded" << files.size() << "files for @-completion";
}
