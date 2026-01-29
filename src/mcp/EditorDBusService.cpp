/*
    SPDX-License-Identifier: MIT
    SPDX-FileCopyrightText: 2025 Kate Code contributors
*/

#include "EditorDBusService.h"

#include <KTextEditor/Application>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/MainWindow>
#include <KTextEditor/View>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>
#include <QFile>
#include <QTimer>
#include <QUrl>

EditorDBusService::EditorDBusService(QObject *parent)
    : QObject(parent)
{
}

bool EditorDBusService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!bus.registerService(QStringLiteral("org.kde.katecode.editor"))) {
        qWarning() << "[KateCode] Failed to register DBus service:" << bus.lastError().message();
        return false;
    }

    if (!bus.registerObject(QStringLiteral("/KateCode/Editor"), this, QDBusConnection::ExportAllSlots)) {
        qWarning() << "[KateCode] Failed to register DBus object:" << bus.lastError().message();
        return false;
    }

    qDebug() << "[KateCode] DBus service registered: org.kde.katecode.editor";
    return true;
}

QStringList EditorDBusService::listDocuments()
{
    QStringList result;

    KTextEditor::Application *app = KTextEditor::Editor::instance()->application();
    if (!app) {
        return result;
    }

    const QList<KTextEditor::Document *> docs = app->documents();
    for (KTextEditor::Document *doc : docs) {
        const QString path = doc->url().toLocalFile();
        if (!path.isEmpty()) {
            result.append(path);
        } else {
            // Untitled documents — use document name
            result.append(QStringLiteral("untitled:%1").arg(doc->documentName()));
        }
    }

    return result;
}

QString EditorDBusService::readDocument(const QString &filePath)
{
    KTextEditor::Application *app = KTextEditor::Editor::instance()->application();
    if (!app) {
        return QStringLiteral("ERROR: KTextEditor application not available");
    }

    // Find existing document by URL
    QUrl url = QUrl::fromLocalFile(filePath);
    KTextEditor::Document *doc = app->findUrl(url);

    if (doc) {
        // Document is open — return its content
        return doc->text();
    }

    // Document not open — read from disk
    QFile file(filePath);
    if (!file.exists()) {
        return QStringLiteral("ERROR: File not found: %1").arg(filePath);
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("ERROR: Cannot open file: %1").arg(file.errorString());
    }
    return QString::fromUtf8(file.readAll());
}

QString EditorDBusService::editDocument(const QString &filePath, const QString &oldText, const QString &newText)
{
    KTextEditor::Application *app = KTextEditor::Editor::instance()->application();
    if (!app) {
        return QStringLiteral("ERROR: KTextEditor application not available");
    }

    QUrl url = QUrl::fromLocalFile(filePath);
    KTextEditor::Document *doc = app->findUrl(url);

    if (!doc) {
        // Try to open the document
        KTextEditor::MainWindow *mainWindow = app->activeMainWindow();
        if (!mainWindow) {
            return QStringLiteral("ERROR: No active main window");
        }
        KTextEditor::View *view = mainWindow->openUrl(url);
        if (!view) {
            return QStringLiteral("ERROR: Could not open document: %1").arg(filePath);
        }
        doc = view->document();
    }

    // Find and replace the text
    QString content = doc->text();
    int pos = content.indexOf(oldText);
    if (pos < 0) {
        return QStringLiteral("ERROR: old_text not found in document");
    }

    // Check for uniqueness
    int secondPos = content.indexOf(oldText, pos + 1);
    if (secondPos >= 0) {
        return QStringLiteral("ERROR: old_text is not unique in document (found at multiple positions)");
    }

    // Calculate line/column for the replacement
    int startLine = content.left(pos).count(QLatin1Char('\n'));
    int startCol = pos - content.lastIndexOf(QLatin1Char('\n'), pos) - 1;
    if (startCol < 0) {
        startCol = pos;
    }

    int endPos = pos + oldText.length();
    int endLine = content.left(endPos).count(QLatin1Char('\n'));
    int endCol = endPos - content.lastIndexOf(QLatin1Char('\n'), endPos - 1) - 1;
    if (endCol < 0) {
        endCol = endPos;
    }

    // Perform the replacement
    KTextEditor::Range range(startLine, startCol, endLine, endCol);
    bool success = doc->replaceText(range, newText);

    if (!success) {
        return QStringLiteral("ERROR: Failed to replace text");
    }

    // Auto-save the document
    if (!doc->save()) {
        return QStringLiteral("ERROR: Edit succeeded but failed to save document");
    }

    return QStringLiteral("OK");
}

QString EditorDBusService::writeDocument(const QString &filePath, const QString &content)
{
    KTextEditor::Application *app = KTextEditor::Editor::instance()->application();
    if (!app) {
        return QStringLiteral("ERROR: KTextEditor application not available");
    }

    QUrl url = QUrl::fromLocalFile(filePath);
    KTextEditor::Document *doc = app->findUrl(url);

    if (doc) {
        // Document is open — replace its content and save
        doc->setText(content);
        if (!doc->save()) {
            return QStringLiteral("ERROR: Write succeeded but failed to save document");
        }
        return QStringLiteral("OK");
    }

    // Document not open — create new or open and set content
    KTextEditor::MainWindow *mainWindow = app->activeMainWindow();
    if (!mainWindow) {
        return QStringLiteral("ERROR: No active main window");
    }

    // Check if file exists
    QFile file(filePath);
    bool fileExists = file.exists();

    if (fileExists) {
        // Open existing file
        KTextEditor::View *view = mainWindow->openUrl(url);
        if (!view) {
            return QStringLiteral("ERROR: Could not open document: %1").arg(filePath);
        }
        view->document()->setText(content);
        if (!view->document()->save()) {
            return QStringLiteral("ERROR: Write succeeded but failed to save document");
        }
    } else {
        // Create new document with content, then save to path
        KTextEditor::View *view = mainWindow->openUrl(QUrl());
        if (!view) {
            return QStringLiteral("ERROR: Could not create new document");
        }
        view->document()->setText(content);
        if (!view->document()->saveAs(url)) {
            return QStringLiteral("ERROR: Could not save document to: %1").arg(filePath);
        }
    }

    return QStringLiteral("OK");
}

QString EditorDBusService::askUserQuestion(const QString &questionsJson)
{
    // Generate unique request ID
    QString requestId = QStringLiteral("q_%1_%2")
        .arg(QCoreApplication::applicationPid())
        .arg(m_nextQuestionId++);

    qDebug() << "[EditorDBusService] askUserQuestion called, requestId:" << requestId;

    // Create event loop for blocking
    QEventLoop eventLoop;

    m_pendingQuestions[requestId] = PendingQuestion{
        &eventLoop,
        QString(),
        false
    };

    // Emit signal to Kate plugin UI
    Q_EMIT questionRequested(requestId, questionsJson);

    // Set up timeout (5 minutes)
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
    timeoutTimer.start(300000);

    qDebug() << "[EditorDBusService] Blocking on event loop for user response...";

    // Block until response or timeout
    eventLoop.exec();

    timeoutTimer.stop();

    // Retrieve response
    QString response;
    if (m_pendingQuestions.contains(requestId)) {
        PendingQuestion pending = m_pendingQuestions.take(requestId);
        if (pending.completed) {
            response = pending.response;
            qDebug() << "[EditorDBusService] Got user response:" << response;
        } else {
            response = QStringLiteral("ERROR: Question timeout or cancelled");
            qDebug() << "[EditorDBusService] Question timed out or was cancelled";
            // Notify UI to remove the question prompt
            Q_EMIT questionCancelled(requestId);
        }
    } else {
        response = QStringLiteral("ERROR: Request not found");
    }

    return response;
}

void EditorDBusService::provideQuestionResponse(const QString &requestId, const QString &responseJson)
{
    qDebug() << "[EditorDBusService] provideQuestionResponse called, requestId:" << requestId;

    if (m_pendingQuestions.contains(requestId)) {
        m_pendingQuestions[requestId].response = responseJson;
        m_pendingQuestions[requestId].completed = true;

        // Wake up the waiting event loop
        if (m_pendingQuestions[requestId].eventLoop) {
            m_pendingQuestions[requestId].eventLoop->quit();
        }
    } else {
        qWarning() << "[EditorDBusService] No pending question found for requestId:" << requestId;
    }
}
