#pragma once

#include "../acp/ACPModels.h"

#include <KTextEditor/Attribute>
#include <KTextEditor/MainWindow>
#include <KTextEditor/MovingRange>
#include <QHash>
#include <QObject>

namespace KTextEditor {
class Document;
class Range;
}

class DiffHighlightManager : public QObject
{
    Q_OBJECT

public:
    explicit DiffHighlightManager(KTextEditor::MainWindow *mainWindow, QObject *parent = nullptr);
    ~DiffHighlightManager() override;

    // Highlight a tool call's edits in the editor
    void highlightToolCall(const QString &toolCallId, const ToolCall &toolCall);

    // Clear highlights for a specific tool call
    void clearToolCallHighlights(const QString &toolCallId);

    // Clear all highlights
    void clearAllHighlights();

private:
    // Find an open document by file path
    KTextEditor::Document *findDocument(const QString &filePath);

    // Search for text in a document and return its range
    KTextEditor::Range findTextInDocument(KTextEditor::Document *doc, const QString &text);

    // Apply highlight to a single edit
    bool highlightEdit(const QString &toolCallId, const EditDiff &edit, const QString &fallbackFilePath);

    // Create the deletion highlight attribute (red background + strikethrough)
    void createDeletionAttribute();

    KTextEditor::MainWindow *m_mainWindow;
    QHash<QString, QList<KTextEditor::MovingRange *>> m_highlights;
    KTextEditor::Attribute::Ptr m_deletionAttr;
};
