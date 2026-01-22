#pragma once

#include <KTextEditor/Document>
#include <KTextEditor/MainWindow>
#include <KXMLGUIClient>
#include <QObject>

class KateCodePlugin;
class ChatWidget;
class DiffHighlightManager;
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
    KTextEditor::Document *findDocumentByPath(const QString &path) const;

    // Shutdown hook for summary generation
    void prepareForShutdown();

private Q_SLOTS:
    void addSelectionToContext();

    // Quick Actions
    void explainCode();
    void findBugs();
    void suggestImprovements();
    void addTests();

    // Edit navigation
    void jumpToEdit(const QString &filePath, int startLine, int endLine);

private:
    void sendQuickAction(const QString &prompt);
    void createToolView();

    KateCodePlugin *m_plugin;
    KTextEditor::MainWindow *m_mainWindow;
    QWidget *m_toolView;
    ChatWidget *m_chatWidget;
    DiffHighlightManager *m_diffHighlightManager;
};