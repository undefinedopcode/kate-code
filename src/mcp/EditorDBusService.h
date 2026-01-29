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

Q_SIGNALS:
    // Emitted when a question needs to be shown to the user
    void questionRequested(const QString &requestId, const QString &questionsJson);

    // Emitted when a question times out or is cancelled (UI should remove the prompt)
    void questionCancelled(const QString &requestId);

private:
    // Track pending question requests
    struct PendingQuestion {
        QEventLoop *eventLoop;
        QString response;
        bool completed;
    };
    QHash<QString, PendingQuestion> m_pendingQuestions;
    int m_nextQuestionId = 0;
};
