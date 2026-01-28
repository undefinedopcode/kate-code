#include "KateCodePlugin.h"
#include "KateCodeView.h"
#include "../config/KateCodeConfigPage.h"
#include "../config/SettingsStore.h"
#include "../mcp/EditorDBusService.h"

#include <KPluginFactory>
#include <KTextEditor/MainWindow>
#include <QApplication>

K_PLUGIN_FACTORY_WITH_JSON(KateCodePluginFactory, "katecode.json", registerPlugin<KateCodePlugin>();)

KateCodePlugin::KateCodePlugin(QObject *parent, const QVariantList &)
    : KTextEditor::Plugin(parent)
    , m_settings(new SettingsStore(this))
    , m_dbusService(new EditorDBusService(this))
{
    m_dbusService->registerOnBus();

    // Connect to application shutdown to trigger summary generation
    connect(qApp, &QApplication::aboutToQuit, this, &KateCodePlugin::onAboutToQuit);
}

KateCodePlugin::~KateCodePlugin()
{
    // Views are cleaned up automatically as they're children of MainWindow
}

void KateCodePlugin::onAboutToQuit()
{
    qDebug() << "[KateCodePlugin] Application shutting down, preparing views...";
    for (KateCodeView *view : m_views) {
        view->prepareForShutdown();
    }
    qDebug() << "[KateCodePlugin] Shutdown preparation complete";
}

QObject *KateCodePlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    auto *view = new KateCodeView(this, mainWindow);
    m_views.append(view);
    return view;
}

int KateCodePlugin::configPages() const
{
    return 1;
}

KTextEditor::ConfigPage *KateCodePlugin::configPage(int number, QWidget *parent)
{
    if (number != 0) {
        return nullptr;
    }
    return new KateCodeConfigPage(m_settings, parent);
}

#include "KateCodePlugin.moc"
