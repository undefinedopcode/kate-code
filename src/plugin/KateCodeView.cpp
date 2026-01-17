#include "KateCodeView.h"
#include "KateCodePlugin.h"
#include "../acp/ACPModels.h"
#include "../config/SettingsStore.h"
#include "../ui/ChatWidget.h"
#include "../util/DiffHighlightManager.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <KXMLGUIFactory>
#include <QAction>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QVBoxLayout>
#include <functional>

KateCodeView::KateCodeView(KateCodePlugin *plugin, KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
    , KXMLGUIClient()
    , m_plugin(plugin)
    , m_mainWindow(mainWindow)
    , m_toolView(nullptr)
    , m_chatWidget(nullptr)
    , m_diffHighlightManager(nullptr)
{
    createToolView();

    // Create diff highlight manager for showing pending edits in editor
    m_diffHighlightManager = new DiffHighlightManager(m_mainWindow, this);

    // Editor diff highlighting disabled - diffs are shown inline in tool call UI instead
    // If re-enabling, connect ChatWidget::toolCallHighlightRequested/toolCallClearRequested
    // to m_diffHighlightManager->highlightToolCall() and clearToolCallHighlights()
    Q_UNUSED(m_diffHighlightManager);

    // Load UI definition file from Qt resources
    setXMLFile(QStringLiteral(":/katecode/katecodeui.rc"));
    qDebug() << "[KateCodeView] Loaded UI file from resources: :/katecode/katecodeui.rc";

    // Create context menu action for adding selection to context
    QAction *addContextAction = new QAction(QIcon::fromTheme(QStringLiteral("list-add")),
                                           i18n("Add to Claude Context..."),
                                           this);
    actionCollection()->addAction(QStringLiteral("kate_code_add_context"), addContextAction);
    actionCollection()->setDefaultShortcut(addContextAction, Qt::CTRL | Qt::ALT | Qt::Key_A);
    connect(addContextAction, &QAction::triggered, this, &KateCodeView::addSelectionToContext);
    qDebug() << "[KateCodeView] Registered action: kate_code_add_context with shortcut Ctrl+Alt+A";

    // Quick Action: Explain Code
    QAction *explainAction = new QAction(QIcon::fromTheme(QStringLiteral("help-about")),
                                         i18n("Explain Code"),
                                         this);
    actionCollection()->addAction(QStringLiteral("kate_code_explain"), explainAction);
    actionCollection()->setDefaultShortcut(explainAction, Qt::CTRL | Qt::ALT | Qt::Key_E);
    connect(explainAction, &QAction::triggered, this, &KateCodeView::explainCode);

    // Quick Action: Find Bugs
    QAction *findBugsAction = new QAction(QIcon::fromTheme(QStringLiteral("tools-report-bug")),
                                          i18n("Find Bugs"),
                                          this);
    actionCollection()->addAction(QStringLiteral("kate_code_find_bugs"), findBugsAction);
    actionCollection()->setDefaultShortcut(findBugsAction, Qt::CTRL | Qt::ALT | Qt::Key_B);
    connect(findBugsAction, &QAction::triggered, this, &KateCodeView::findBugs);

    // Quick Action: Suggest Improvements
    QAction *improvementsAction = new QAction(QIcon::fromTheme(QStringLiteral("tools-wizard")),
                                              i18n("Suggest Improvements"),
                                              this);
    actionCollection()->addAction(QStringLiteral("kate_code_improvements"), improvementsAction);
    actionCollection()->setDefaultShortcut(improvementsAction, Qt::CTRL | Qt::ALT | Qt::Key_I);
    connect(improvementsAction, &QAction::triggered, this, &KateCodeView::suggestImprovements);

    // Quick Action: Add Tests
    QAction *addTestsAction = new QAction(QIcon::fromTheme(QStringLiteral("document-new")),
                                          i18n("Add Tests"),
                                          this);
    actionCollection()->addAction(QStringLiteral("kate_code_add_tests"), addTestsAction);
    actionCollection()->setDefaultShortcut(addTestsAction, Qt::CTRL | Qt::ALT | Qt::Key_T);
    connect(addTestsAction, &QAction::triggered, this, &KateCodeView::addTests);

    qDebug() << "[KateCodeView] Registered Quick Actions: Explain, Find Bugs, Improvements, Add Tests";

    // Register the action with Kate's editor context menu
    m_mainWindow->guiFactory()->addClient(this);
    qDebug() << "[KateCodeView] Added XMLGUI client to Kate";
}

KateCodeView::~KateCodeView()
{
    // Remove XMLGUI client before destruction to prevent leaks and crashes
    if (m_mainWindow && m_mainWindow->guiFactory()) {
        m_mainWindow->guiFactory()->removeClient(this);
    }

    // ToolView is owned by MainWindow and cleaned up automatically
}

void KateCodeView::createToolView()
{
    // Create side panel tool view
    m_toolView = m_mainWindow->createToolView(
        m_plugin,
        QStringLiteral("katecode"),
        KTextEditor::MainWindow::Left,
        QIcon::fromTheme(QStringLiteral("code-context")),
        i18n("Claude Code")
    );

    if (!m_toolView) {
        qDebug() << "[KateCode] ERROR - toolView is null!";
        return;
    }

    // Work with existing layout if present, or create new one
    QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(m_toolView->layout());
    if (!layout) {
        layout = new QVBoxLayout(m_toolView);
        qDebug() << "[KateCode] Created new QVBoxLayout";
    } else {
        qDebug() << "[KateCode] Using existing QVBoxLayout";
    }

    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Create chat widget
    m_chatWidget = new ChatWidget(m_toolView);
    m_chatWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_chatWidget);

    // Set context providers so ChatWidget can get current file/selection and project root
    m_chatWidget->setFilePathProvider([this]() { return getCurrentFilePath(); });
    m_chatWidget->setSelectionProvider([this]() { return getCurrentSelection(); });
    m_chatWidget->setProjectRootProvider([this]() { return getProjectRoot(); });
    m_chatWidget->setFileListProvider([this]() { return getProjectFiles(); });

    // Inject settings store for summary generation
    m_chatWidget->setSettingsStore(m_plugin->settings());
}

QString KateCodeView::getCurrentFilePath() const
{
    KTextEditor::View *view = m_mainWindow->activeView();
    if (view && view->document()) {
        return view->document()->url().toLocalFile();
    }
    return QString();
}

QString KateCodeView::getCurrentSelection() const
{
    KTextEditor::View *view = m_mainWindow->activeView();
    if (view) {
        return view->selectionText();
    }
    return QString();
}

QString KateCodeView::getProjectRoot() const
{
    QString projectRoot;

    // 1. First try Kate's project plugin (if available)
    QObject *projectPlugin = m_mainWindow->pluginView(QStringLiteral("kateprojectplugin"));
    if (projectPlugin) {
        QVariant baseDir = projectPlugin->property("projectBaseDir");
        if (baseDir.isValid() && !baseDir.toString().isEmpty()) {
            projectRoot = baseDir.toString();
            qDebug() << "[KateCode] Found project root from Kate project plugin:" << projectRoot;
            return projectRoot;
        }
    }

    // 2. Fall back to searching from active document
    QString filePath = getCurrentFilePath();
    if (filePath.isEmpty()) {
        return QDir::homePath();
    }

    QDir currentDir(QFileInfo(filePath).absolutePath());

    // 3. Walk up directory tree looking for project indicators
    while (currentDir.exists() && currentDir.path() != QDir::rootPath()) {
        QString dirPath = currentDir.absolutePath();

        // Check for version control systems
        if (currentDir.exists(QStringLiteral(".git")) ||
            currentDir.exists(QStringLiteral(".hg")) ||
            currentDir.exists(QStringLiteral(".svn")) ||
            currentDir.exists(QStringLiteral(".gitignore"))) {
            qDebug() << "[KateCode] Found project root via VCS marker:" << dirPath;
            return dirPath;
        }

        // Check for build system files
        if (currentDir.exists(QStringLiteral("CMakeLists.txt")) ||
            currentDir.exists(QStringLiteral("Makefile")) ||
            currentDir.exists(QStringLiteral("package.json")) ||
            currentDir.exists(QStringLiteral("Cargo.toml")) ||
            currentDir.exists(QStringLiteral("build.gradle")) ||
            currentDir.exists(QStringLiteral("pom.xml")) ||
            currentDir.exists(QStringLiteral("setup.py")) ||
            currentDir.exists(QStringLiteral("pyproject.toml")) ||
            currentDir.exists(QStringLiteral("go.mod"))) {
            qDebug() << "[KateCode] Found project root via build file:" << dirPath;
            return dirPath;
        }

        // Check for IDE project files
        if (currentDir.exists(QStringLiteral(".idea")) ||
            currentDir.exists(QStringLiteral(".vscode")) ||
            currentDir.exists(QStringLiteral(".project")) ||
            currentDir.exists(QStringLiteral(".kate-project"))) {
            qDebug() << "[KateCode] Found project root via IDE marker:" << dirPath;
            return dirPath;
        }

        if (!currentDir.cdUp()) {
            break;
        }
    }

    // Fallback to document directory
    qDebug() << "[KateCode] No project root found, using document directory";
    return QFileInfo(filePath).absolutePath();
}

QStringList KateCodeView::getProjectFiles() const
{
    QString projectRoot = getProjectRoot();
    if (projectRoot.isEmpty()) {
        return QStringList();
    }

    QStringList files;
    QDir rootDir(projectRoot);

    // Directories to ignore
    static const QStringList ignoredDirs = {
        QStringLiteral(".git"),
        QStringLiteral(".hg"),
        QStringLiteral(".svn"),
        QStringLiteral("node_modules"),
        QStringLiteral("build"),
        QStringLiteral("dist"),
        QStringLiteral("target"),
        QStringLiteral(".idea"),
        QStringLiteral(".vscode"),
        QStringLiteral("__pycache__"),
        QStringLiteral(".pytest_cache"),
        QStringLiteral(".tox"),
        QStringLiteral("venv"),
        QStringLiteral(".venv"),
        QStringLiteral("env")
    };

    // Recursive function to scan directories
    std::function<void(const QDir &, const QString &)> scanDir = [&](const QDir &dir, const QString &relativePath) {
        // Get all entries
        QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

        for (const QFileInfo &entry : entries) {
            QString name = entry.fileName();
            QString relPath = relativePath.isEmpty() ? name : relativePath + QStringLiteral("/") + name;

            if (entry.isDir()) {
                // Skip ignored directories
                if (!ignoredDirs.contains(name)) {
                    scanDir(QDir(entry.filePath()), relPath);
                }
            } else if (entry.isFile()) {
                files.append(relPath);
            }
        }
    };

    scanDir(rootDir, QString());

    qDebug() << "[KateCode] Found" << files.size() << "files in project";
    return files;
}

void KateCodeView::addSelectionToContext()
{
    KTextEditor::View *view = m_mainWindow->activeView();
    if (!view || !view->document()) {
        qWarning() << "[KateCode] No active view or document";
        return;
    }

    QString selection = view->selectionText();
    if (selection.isEmpty()) {
        qWarning() << "[KateCode] No text selected";
        return;
    }

    QString filePath = view->document()->url().toLocalFile();
    if (filePath.isEmpty()) {
        qWarning() << "[KateCode] No file path for document";
        return;
    }

    // Get line numbers for the selection
    auto selectionRange = view->selectionRange();
    int startLine = selectionRange.start().line() + 1;  // Convert to 1-based
    int endLine = selectionRange.end().line() + 1;

    // Add to chat widget's context
    if (m_chatWidget) {
        m_chatWidget->addContextChunk(filePath, startLine, endLine, selection);
        qDebug() << "[KateCode] Added selection to context:" << filePath
                 << "lines" << startLine << "-" << endLine;
    } else {
        qWarning() << "[KateCode] Chat widget not available";
    }
}

void KateCodeView::explainCode()
{
    sendQuickAction(i18n("Please explain what this code does, including its purpose, key logic, and any important details."));
}

void KateCodeView::findBugs()
{
    sendQuickAction(i18n("Please analyze this code for potential bugs, errors, or issues. Consider edge cases, error handling, and correctness."));
}

void KateCodeView::suggestImprovements()
{
    sendQuickAction(i18n("Please suggest improvements for this code. Consider readability, performance, maintainability, and best practices."));
}

void KateCodeView::addTests()
{
    sendQuickAction(i18n("Please generate comprehensive test cases for this code. Include unit tests covering normal cases, edge cases, and error conditions."));
}

void KateCodeView::sendQuickAction(const QString &prompt)
{
    KTextEditor::View *view = m_mainWindow->activeView();
    if (!view || !view->document()) {
        qWarning() << "[KateCode] No active view or document for quick action";
        return;
    }

    QString selection = view->selectionText();
    if (selection.isEmpty()) {
        qWarning() << "[KateCode] No text selected for quick action";
        return;
    }

    QString filePath = view->document()->url().toLocalFile();

    // Send prompt with selection through ChatWidget
    if (m_chatWidget) {
        m_chatWidget->sendPromptWithSelection(prompt, filePath, selection);

        // Show and focus the chat panel so user sees response
        m_mainWindow->showToolView(m_toolView);

        qDebug() << "[KateCode] Sent quick action prompt with selection from:" << filePath;
    } else {
        qWarning() << "[KateCode] Chat widget not available for quick action";
    }
}
