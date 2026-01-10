#pragma once

#include "../acp/ACPModels.h"
#include <QWidget>
#include <QTextEdit>

class QJsonArray;
class QPushButton;
class QComboBox;
class QCompleter;

// Custom QTextEdit that handles QCompleter for slash commands and file references
class QAbstractItemModel;

class CommandTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit CommandTextEdit(QWidget *parent = nullptr);
    void setCompleter(QCompleter *completer);
    void setModels(QAbstractItemModel *commandModel, QAbstractItemModel *fileModel);
    QCompleter *completer() const { return m_completer; }

protected:
    void keyPressEvent(QKeyEvent *e) override;

private Q_SLOTS:
    void insertCompletion(const QString &completion);

private:
    enum CompletionType {
        None,
        Command,  // '/' prefix
        File      // '@' prefix
    };

    struct CompletionContext {
        CompletionType type;
        QString prefix;        // Full text including prefix char
        QString filterText;    // Text after prefix char for filtering
        int prefixStart;       // Position where prefix starts
    };

    CompletionContext completionUnderCursor() const;
    QCompleter *m_completer = nullptr;
    QAbstractItemModel *m_commandModel = nullptr;
    QAbstractItemModel *m_fileModel = nullptr;
};

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

    void setAvailableModes(const QJsonArray &modes);
    void setCurrentMode(const QString &modeId);
    void setAvailableCommands(const QList<SlashCommand> &commands);
    void setAvailableFiles(const QStringList &files);

Q_SIGNALS:
    void messageSubmitted(const QString &message);
    void permissionModeChanged(const QString &mode);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private Q_SLOTS:
    void onSendClicked();
    void onModeChanged(int index);

private:
    CommandTextEdit *m_textEdit;
    QPushButton *m_sendButton;
    QComboBox *m_modeComboBox;
    QCompleter *m_completer;
    QList<SlashCommand> m_availableCommands;
    QStringList m_availableFiles;

    // Models for completer (switched based on context)
    QAbstractItemModel *m_commandModel = nullptr;
    QAbstractItemModel *m_fileModel = nullptr;
};
