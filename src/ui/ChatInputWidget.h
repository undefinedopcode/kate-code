#pragma once

#include <QWidget>

class QTextEdit;
class QPushButton;

class ChatInputWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatInputWidget(QWidget *parent = nullptr);
    ~ChatInputWidget() override;

    void setEnabled(bool enabled);
    void clear();
    QString text() const;

Q_SIGNALS:
    void messageSubmitted(const QString &message);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private Q_SLOTS:
    void onSendClicked();

private:
    QTextEdit *m_textEdit;
    QPushButton *m_sendButton;
};
