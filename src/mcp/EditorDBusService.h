/*
    SPDX-License-Identifier: MIT
    SPDX-FileCopyrightText: 2025 Kate Code contributors
*/

#pragma once

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
};
