#include "DiffHighlightManager.h"
#include "KateThemeConverter.h"
#include "../config/SettingsStore.h"

#include <KTextEditor/Application>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/Range>
#include <QColor>
#include <QDebug>
#include <QDir>
#include <QUrl>

DiffHighlightManager::DiffHighlightManager(KTextEditor::MainWindow *mainWindow, SettingsStore *settings, QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_settings(settings)
{
    createDeletionAttribute();

    // Connect to settings changes to update colors dynamically
    if (m_settings) {
        connect(m_settings, &SettingsStore::settingsChanged,
                this, &DiffHighlightManager::onSettingsChanged);
    }

    qDebug() << "[DiffHighlightManager] Initialized";
}

DiffHighlightManager::~DiffHighlightManager()
{
    clearAllHighlights();
}

void DiffHighlightManager::createDeletionAttribute()
{
    m_deletionAttr = KTextEditor::Attribute::Ptr(new KTextEditor::Attribute());

    // Detect if Kate theme has a light background
    bool isLightBackground = KateThemeConverter::isLightBackground();

    // Get colors from settings appropriate for the background brightness
    DiffColorScheme scheme = m_settings ? m_settings->diffColorScheme() : DiffColorScheme::RedGreen;
    DiffColors colors = SettingsStore::colorsForScheme(scheme, isLightBackground);

    // Apply deletion colors
    m_deletionAttr->setBackground(colors.deletionBackground);
    m_deletionAttr->setForeground(colors.deletionForeground);

    // Strikethrough effect
    m_deletionAttr->setFontStrikeOut(true);

    qDebug() << "[DiffHighlightManager] Created deletion attribute with colors:"
             << "bg=" << colors.deletionBackground.name()
             << "fg=" << colors.deletionForeground.name()
             << "isLightBackground:" << isLightBackground;
}

void DiffHighlightManager::onSettingsChanged()
{
    // Recreate the attribute with new colors
    createDeletionAttribute();

    // Update existing highlights to use the new attribute
    for (auto it = m_highlights.begin(); it != m_highlights.end(); ++it) {
        for (KTextEditor::MovingRange *range : it.value()) {
            range->setAttribute(m_deletionAttr);
        }
    }

    qDebug() << "[DiffHighlightManager] Updated colors from settings";
}

void DiffHighlightManager::highlightToolCall(const QString &toolCallId, const ToolCall &toolCall)
{
    // Clear any existing highlights for this tool call
    clearToolCallHighlights(toolCallId);

    // Skip if no edits
    if (toolCall.edits.isEmpty()) {
        qDebug() << "[DiffHighlightManager] No edits to highlight for tool call:" << toolCallId;
        return;
    }

    int successCount = 0;
    for (const EditDiff &edit : toolCall.edits) {
        // Use edit's filePath if available, otherwise fall back to toolCall's filePath
        QString filePath = edit.filePath.isEmpty() ? toolCall.filePath : edit.filePath;

        if (highlightEdit(toolCallId, edit, filePath)) {
            successCount++;
        }
    }

    qDebug() << "[DiffHighlightManager] Highlighted" << successCount << "of" << toolCall.edits.size()
             << "edits for tool call:" << toolCallId;
}

void DiffHighlightManager::clearToolCallHighlights(const QString &toolCallId)
{
    if (!m_highlights.contains(toolCallId)) {
        return;
    }

    QList<KTextEditor::MovingRange *> ranges = m_highlights.take(toolCallId);
    for (KTextEditor::MovingRange *range : ranges) {
        delete range;
    }

    qDebug() << "[DiffHighlightManager] Cleared" << ranges.size() << "highlights for tool call:" << toolCallId;
}

void DiffHighlightManager::clearAllHighlights()
{
    int totalCount = 0;
    for (auto it = m_highlights.begin(); it != m_highlights.end(); ++it) {
        for (KTextEditor::MovingRange *range : it.value()) {
            delete range;
            totalCount++;
        }
    }
    m_highlights.clear();

    if (totalCount > 0) {
        qDebug() << "[DiffHighlightManager] Cleared all" << totalCount << "highlights";
    }
}

KTextEditor::Document *DiffHighlightManager::findDocument(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return nullptr;
    }

    // Normalize the file path
    QString normalizedPath = QDir::cleanPath(filePath);
    QUrl fileUrl = QUrl::fromLocalFile(normalizedPath);

    // Get all documents from the application
    KTextEditor::Application *app = KTextEditor::Editor::instance()->application();
    if (!app) {
        qWarning() << "[DiffHighlightManager] No KTextEditor application available";
        return nullptr;
    }

    const QList<KTextEditor::Document *> docs = app->documents();
    for (KTextEditor::Document *doc : docs) {
        if (doc->url() == fileUrl || doc->url().toLocalFile() == normalizedPath) {
            return doc;
        }
    }

    qDebug() << "[DiffHighlightManager] Document not found for path:" << filePath;
    return nullptr;
}

KTextEditor::Range DiffHighlightManager::findTextInDocument(KTextEditor::Document *doc, const QString &text)
{
    if (!doc || text.isEmpty()) {
        return KTextEditor::Range::invalid();
    }

    // Search the entire document
    KTextEditor::Range searchRange(0, 0, doc->lines(), 0);

    // Use document's searchText to find the text
    QList<KTextEditor::Range> results = doc->searchText(searchRange, text);

    if (results.isEmpty()) {
        qDebug() << "[DiffHighlightManager] Text not found in document:" << doc->url().toLocalFile()
                 << "text preview:" << text.left(50);
        return KTextEditor::Range::invalid();
    }

    if (results.size() > 1) {
        qDebug() << "[DiffHighlightManager] Found" << results.size() << "matches, using first";
    }

    return results.first();
}

bool DiffHighlightManager::highlightEdit(const QString &toolCallId, const EditDiff &edit, const QString &fallbackFilePath)
{
    // Skip if no old text to highlight (this is an insertion-only edit)
    if (edit.oldText.isEmpty()) {
        qDebug() << "[DiffHighlightManager] Skipping edit with no oldText (insertion only)";
        return false;
    }

    // Use edit's file path or fall back to the provided path
    QString filePath = edit.filePath.isEmpty() ? fallbackFilePath : edit.filePath;
    if (filePath.isEmpty()) {
        qWarning() << "[DiffHighlightManager] No file path for edit";
        return false;
    }

    // Find the document
    KTextEditor::Document *doc = findDocument(filePath);
    if (!doc) {
        qDebug() << "[DiffHighlightManager] Document not open:" << filePath;
        return false;
    }

    // Check text size limit for performance
    const int MAX_HIGHLIGHT_SIZE = 10000;
    if (edit.oldText.length() > MAX_HIGHLIGHT_SIZE) {
        qDebug() << "[DiffHighlightManager] Text too large to highlight:" << edit.oldText.length() << "chars";
        return false;
    }

    // Find the text in the document
    KTextEditor::Range textRange = findTextInDocument(doc, edit.oldText);
    if (!textRange.isValid()) {
        qWarning() << "[DiffHighlightManager] Could not find text in document:" << filePath;
        return false;
    }

    // Create a moving range to track the text
    KTextEditor::MovingRange *movingRange = doc->newMovingRange(
        textRange,
        KTextEditor::MovingRange::DoNotExpand,
        KTextEditor::MovingRange::InvalidateIfEmpty);

    if (!movingRange) {
        qWarning() << "[DiffHighlightManager] Failed to create moving range";
        return false;
    }

    // Apply the deletion attribute
    movingRange->setAttribute(m_deletionAttr);

    // Store the range for later cleanup
    m_highlights[toolCallId].append(movingRange);

    qDebug() << "[DiffHighlightManager] Highlighted deletion at" << textRange.start().line() + 1 << ":"
             << textRange.start().column() << "to" << textRange.end().line() + 1 << ":" << textRange.end().column();

    return true;
}
