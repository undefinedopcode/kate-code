#include "KateCodePlugin.h"
#include "KateCodeView.h"
#include "../config/KateCodeConfigPage.h"
#include "../config/SettingsStore.h"
#include "../mcp/EditorDBusService.h"

#include <KPluginFactory>
#include <KTextEditor/MainWindow>
#include <QApplication>
#include <QTimer>

K_PLUGIN_FACTORY_WITH_JSON(KateCodePluginFactory, "katecode.json", registerPlugin<KateCodePlugin>();)

KateCodePlugin::KateCodePlugin(QObject *parent, const QVariantList &)
    : KTextEditor::Plugin(parent)
    , m_settings(new SettingsStore(this))
    , m_dbusService(new EditorDBusService(this))
{
    // Defer DBus registration to the next event loop iteration.  Calling
    // registerService() directly in the constructor can fail (empty error)
    // if the session bus connection has not fully settled during plugin
    // load.  The single-shot timer also adds a 1 s retry so that transient
    // failures during Kate startup are recovered automatically.
    QTimer::singleShot(0, this, [this]() {
        if (!m_dbusService->registerOnBus()) {
            qWarning() << "[KateCode] DBus registration failed on first attempt, retrying in 1s";
            QTimer::singleShot(1000, this, [this]() {
                m_dbusService->registerOnBus();
            });
        }
    });

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

bool KateCodePlugin::acquireAgentSlot(QObject *owner)
{
    if (m_agentOwner.isNull() || m_agentOwner == owner) {
        m_agentOwner = owner;
        Q_EMIT agentActivityChanged();
        return true;
    }
    return false;
}

void KateCodePlugin::releaseAgentSlot(QObject *owner)
{
    if (m_agentOwner == owner) {
        m_agentOwner = nullptr;
        Q_EMIT agentActivityChanged();
    }
}

bool KateCodePlugin::agentSlotAvailableFor(QObject *owner) const
{
    return m_agentOwner.isNull() || m_agentOwner == owner;
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
