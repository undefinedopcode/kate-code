#include "ChatInputWidget.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

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
    m_modeComboBox->addItem(QStringLiteral("Default"), QStringLiteral("default"));
    m_modeComboBox->addItem(QStringLiteral("Plan"), QStringLiteral("plan"));
    m_modeComboBox->addItem(QStringLiteral("Accept Edits"), QStringLiteral("acceptEdits"));
    m_modeComboBox->addItem(QStringLiteral("Don't Ask"), QStringLiteral("dontAsk"));
    m_modeComboBox->setMinimumWidth(150);

    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(m_modeComboBox);
    modeLayout->addStretch();

    mainLayout->addLayout(modeLayout);

    // Input row
    auto *inputLayout = new QHBoxLayout();
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(4);

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

void ChatInputWidget::onModeChanged(int index)
{
    Q_UNUSED(index);
    QString mode = m_modeComboBox->currentData().toString();
    Q_EMIT permissionModeChanged(mode);
}
