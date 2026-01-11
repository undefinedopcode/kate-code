#pragma once

#include <KTextEditor/MainWindow>
#include <KXMLGUIClient>
#include <QObject>

class KateCodePlugin;
class ChatWidget;
class QWidget;

class KateCodeView : public QObject, public KXMLGUIClient
{
    Q_OBJECT

public:
    explicit KateCodeView(KateCodePlugin *plugin, KTextEditor::MainWindow *mainWindow);
    ~KateCodeView() override;

    // Kate context getters
    QString getCurrentFilePath() const;
    QString getCurrentSelection() const;
    QString getProjectRoot() const;
    QStringList getProjectFiles() const;

private Q_SLOTS:
    void addSelectionToContext();

private:
    void createToolView();

    KateCodePlugin *m_plugin;
    KTextEditor::MainWindow *m_mainWindow;
    QWidget *m_toolView;
    ChatWidget *m_chatWidget;
};
