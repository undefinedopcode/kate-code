#pragma once

#include <KTextEditor/Plugin>
#include <QObject>
#include <QVariant>

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

private Q_SLOTS:
    void onAboutToQuit();

private:
    QList<KateCodeView *> m_views;
    SettingsStore *m_settings;
};
