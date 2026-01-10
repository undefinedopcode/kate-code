#include "KateCodePlugin.h"
#include "KateCodeView.h"

#include <KPluginFactory>
#include <KTextEditor/MainWindow>

K_PLUGIN_FACTORY_WITH_JSON(KateCodePluginFactory, "katecode.json", registerPlugin<KateCodePlugin>();)

KateCodePlugin::KateCodePlugin(QObject *parent, const QVariantList &)
    : KTextEditor::Plugin(parent)
{
}

KateCodePlugin::~KateCodePlugin()
{
    // Views are cleaned up automatically as they're children of MainWindow
}

QObject *KateCodePlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    auto *view = new KateCodeView(this, mainWindow);
    m_views.append(view);
    return view;
}

#include "KateCodePlugin.moc"
