#pragma once

#include <KTextEditor/Plugin>
#include <QObject>
#include <QPointer>
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

    // Called from KateCodeView::~KateCodeView() to prevent dangling pointers.
    // Also emits agentActivityChanged() so other windows can refresh their buttons.
    void removeView(KateCodeView *view)
    {
        m_views.removeOne(view);
        Q_EMIT agentActivityChanged();
    }

    // Process-wide single-active-agent gate.
    // Returns true and claims the slot when it is free or already owned by
    // owner; returns false when another window holds it.
    bool acquireAgentSlot(QObject *owner);
    // Releases the slot if owner currently holds it.
    void releaseAgentSlot(QObject *owner);
    // Returns true when the slot is free or already held by owner.
    bool agentSlotAvailableFor(QObject *owner) const;

Q_SIGNALS:
    // Emitted whenever the agent-slot owner changes so all windows can
    // update their button enabled-states.
    void agentActivityChanged();

private Q_SLOTS:
    void onAboutToQuit();

private:
    QList<KateCodeView *> m_views;
    SettingsStore *m_settings;
    EditorDBusService *m_dbusService;
    // Tracks which ChatWidget currently holds the single-agent slot.
    // QPointer auto-clears if the owning widget is destroyed.
    QPointer<QObject> m_agentOwner;
};
