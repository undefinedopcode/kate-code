#include "KateCodeView.h"
#include "KateCodePlugin.h"
#include "../ui/ChatWidget.h"

#include <KLocalizedString>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QVBoxLayout>

KateCodeView::KateCodeView(KateCodePlugin *plugin, KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
    , KXMLGUIClient()
    , m_plugin(plugin)
    , m_mainWindow(mainWindow)
    , m_toolView(nullptr)
    , m_chatWidget(nullptr)
{
    createToolView();
}

KateCodeView::~KateCodeView()
{
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
