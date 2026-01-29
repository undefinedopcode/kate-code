#pragma once

#include <KTextEditor/Plugin>
#include <QObject>
#include <QVariant>

class EditorDBusService;
class KateCodeView;
class SettingsStore;

class KateCodePlugin : public KTextEditor::Plugin
{
    Q_OBJECT

public:
    explicit KateCodePlugin(QObject *parent = nullptr, const QVariantList & = QVariantList());
    ~KateCodePlugin() override;

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;

    // Config page support
    int configPages() const override;
    KTextEditor::ConfigPage *configPage(int number, QWidget *parent) override;

    // Settings access for views
    SettingsStore *settings() const { return m_settings; }

    // DBus service access for views (used for question routing)
    EditorDBusService *dbusService() const { return m_dbusService; }

private Q_SLOTS:
    void onAboutToQuit();

private:
    QList<KateCodeView *> m_views;
    SettingsStore *m_settings;
    EditorDBusService *m_dbusService;
};
