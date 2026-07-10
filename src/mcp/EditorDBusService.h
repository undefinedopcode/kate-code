/*
    SPDX-License-Identifier: MIT
    SPDX-FileCopyrightText: 2025 Kate Code contributors
*/

#pragma once

#include <QEventLoop>
#include <QHash>
#include <QObject>
#include <QStringList>

class EditorDBusService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.katecode.Editor")

public:
    explicit EditorDBusService(QObject *parent = nullptr);

    // Register this service on the session bus.
    // Returns true on success.
    bool registerOnBus();

    // Called by UI when user responds to a question
    void provideQuestionResponse(const QString &requestId, const QString &responseJson);

    // Called by ChatWidget when the active session ID changes
    void updateCurrentSessionId(const QString &sessionId);

public Q_SLOTS:
    QStringList listDocuments();

    // Read a document's content. Returns the text content.
    // If the document is not open, returns an error string starting with "ERROR:".
    QString readDocument(const QString &filePath);

    // Edit a document by replacing old_text with new_text.
    // Returns "OK" on success or "ERROR: ..." on failure.
    QString editDocument(const QString &filePath, const QString &oldText, const QString &newText);

    // Write content to a document (creates or overwrites).
    // Returns "OK" on success or "ERROR: ..." on failure.
    QString writeDocument(const QString &filePath, const QString &content);

    // Ask the user questions - blocks until user responds or timeout.
    // questionsJson is a JSON array of question objects.
    // Returns JSON object with answers keyed by question header, or "ERROR: ..." on failure.
    QString askUserQuestion(const QString &questionsJson);

    // Returns JSON with path, line, column, isModified, and optional selection of the active view.
    QString getActiveDocument();

    // Opens filePath in Kate, optionally moving the cursor to line/column (1-based; pass 0 to skip).
    QString openDocument(const QString &filePath, int line, int column);

    // Closes the document identified by filePath.
    QString closeDocument(const QString &filePath);

    // Saves filePath, or all modified documents if filePath is empty.
    QString saveDocument(const QString &filePath);

    // Returns JSON with path, isModified, isReadOnly for the given document.
    QString getDocumentStatus(const QString &filePath);

    // Reloads filePath from disk, discarding unsaved changes.
    QString revertDocument(const QString &filePath);

    // Sets the note for a session in the session store.
    // Returns "OK" on success or "ERROR: ..." on failure.
    QString setSessionNote(const QString &sessionId, const QString &note);

    // Returns the ID of the currently active kate-code session, or empty string if not connected.
    QString getSessionId();

    // Returns the current clipboard text content.
    QString getClipboardText();

    // Sends text to Kate's embedded terminal without executing it (no Enter).
    // Returns "OK" on success or "ERROR: ..." on failure.
    QString pasteToTerminal(const QString &text);

Q_SIGNALS:
    // Emitted when editDocument succeeds, with the edit parameters
    void editApplied(const QString &filePath, const QString &oldText, const QString &newText);

    // Emitted when a question needs to be shown to the user
    void questionRequested(const QString &requestId, const QString &questionsJson);

    // Emitted when a question times out or is cancelled (UI should remove the prompt)
    void questionCancelled(const QString &requestId);

    // Emitted when a session note should be updated in the session store
    void sessionNoteUpdateRequested(const QString &sessionId, const QString &note);

private:
    QString m_currentSessionId;

    // Track pending question requests
    struct PendingQuestion {
        QEventLoop *eventLoop;
        QString response;
        bool completed;
    };
    QHash<QString, PendingQuestion> m_pendingQuestions;
    int m_nextQuestionId = 0;
};
