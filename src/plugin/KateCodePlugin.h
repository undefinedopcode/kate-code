#pragma once

#include <KTextEditor/Plugin>
#include <QObject>
#include <QVariant>

class KateCodeView;

class KateCodePlugin : public KTextEditor::Plugin
{
    Q_OBJECT

public:
    explicit KateCodePlugin(QObject *parent = nullptr, const QVariantList & = QVariantList());
    ~KateCodePlugin() override;

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;

private:
    QList<KateCodeView *> m_views;
};
