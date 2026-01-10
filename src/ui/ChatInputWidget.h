#pragma once

#include <QWidget>

class QTextEdit;
class QPushButton;
class QComboBox;

class ChatInputWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatInputWidget(QWidget *parent = nullptr);
    ~ChatInputWidget() override;

    void setEnabled(bool enabled);
    void clear();
    QString text() const;
    QString permissionMode() const;

Q_SIGNALS:
    void messageSubmitted(const QString &message);
    void permissionModeChanged(const QString &mode);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private Q_SLOTS:
    void onSendClicked();
    void onModeChanged(int index);

private:
    QTextEdit *m_textEdit;
    QPushButton *m_sendButton;
    QComboBox *m_modeComboBox;
};
