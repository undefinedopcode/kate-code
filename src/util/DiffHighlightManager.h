#pragma once

#include "../acp/ACPModels.h"

#include <KTextEditor/Attribute>
#include <KTextEditor/MainWindow>
#include <KTextEditor/MovingRange>
#include <QHash>
#include <QObject>

class SettingsStore;

namespace KTextEditor {
class Document;
class Range;
}

class DiffHighlightManager : public QObject
{
    Q_OBJECT

public:
    explicit DiffHighlightManager(KTextEditor::MainWindow *mainWindow, SettingsStore *settings = nullptr, QObject *parent = nullptr);
    ~DiffHighlightManager() override;

    // Highlight a tool call's edits in the editor
    void highlightToolCall(const QString &toolCallId, const ToolCall &toolCall);

    // Clear highlights for a specific tool call
    void clearToolCallHighlights(const QString &toolCallId);

    // Clear all highlights
    void clearAllHighlights();

private Q_SLOTS:
    void onSettingsChanged();

private:
    // Find an open document by file path
    KTextEditor::Document *findDocument(const QString &filePath);

    // Search for text in a document and return its range
    KTextEditor::Range findTextInDocument(KTextEditor::Document *doc, const QString &text);

    // Apply highlight to a single edit
    bool highlightEdit(const QString &toolCallId, const EditDiff &edit, const QString &fallbackFilePath);

    // Create the deletion highlight attribute based on current settings
    void createDeletionAttribute();

    KTextEditor::MainWindow *m_mainWindow;
    SettingsStore *m_settings;
    QHash<QString, QList<KTextEditor::MovingRange *>> m_highlights;
    KTextEditor::Attribute::Ptr m_deletionAttr;
};
