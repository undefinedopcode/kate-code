#include "ChatInputWidget.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QTextEdit>

ChatInputWidget::ChatInputWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Multiline text input
    m_textEdit = new QTextEdit(this);
    m_textEdit->setPlaceholderText(QStringLiteral("Type a message... (Enter to send, Shift+Enter for newline)"));
    m_textEdit->setMinimumHeight(50);
    m_textEdit->setMaximumHeight(100);
    m_textEdit->installEventFilter(this);

    // Send button
    m_sendButton = new QPushButton(QStringLiteral("Send"), this);
    m_sendButton->setMinimumWidth(80);
    m_sendButton->setMinimumHeight(50);

    layout->addWidget(m_textEdit, 1);
    layout->addWidget(m_sendButton);

    connect(m_sendButton, &QPushButton::clicked, this, &ChatInputWidget::onSendClicked);
}

ChatInputWidget::~ChatInputWidget()
{
}

void ChatInputWidget::setEnabled(bool enabled)
{
    m_textEdit->setEnabled(enabled);
    m_sendButton->setEnabled(enabled);
}

void ChatInputWidget::clear()
{
    m_textEdit->clear();
}

QString ChatInputWidget::text() const
{
    return m_textEdit->toPlainText();
}

bool ChatInputWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_textEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

        // Enter without Shift = send message
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (!(keyEvent->modifiers() & Qt::ShiftModifier)) {
                onSendClicked();
                return true;  // Consume the event
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
