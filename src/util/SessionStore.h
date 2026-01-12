#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

/**
 * SessionStore - Persists session IDs per project root.
 *
 * Uses QSettings to store the mapping in ~/.config/kate-code.conf.
 * Each project root has at most one associated session ID.
 */
class SessionStore : public QObject
{
    Q_OBJECT

public:
    explicit SessionStore(QObject *parent = nullptr);
    ~SessionStore() override = default;

    // Save session ID for a project root
    void saveSession(const QString &projectRoot, const QString &sessionId);

    // Get the last session ID for a project root (empty if none)
    QString getLastSession(const QString &projectRoot) const;

    // Clear the stored session for a project root
    void clearSession(const QString &projectRoot);

    // Check if a session exists for a project root
    bool hasSession(const QString &projectRoot) const;

private:
    // Normalize the project root path for consistent keys
    QString normalizeKey(const QString &projectRoot) const;

    QSettings m_settings;
};
