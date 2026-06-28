#pragma once

#include <QDateTime>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

struct SessionEntry {
    QString id;
    QString name;
    QString note;
    QDateTime timestamp;
};

/**
 * SessionStore - Persists named session history per project root.
 *
 * Uses QSettings to store the mapping in ~/.config/kate-code.conf.
 * Each project root has a list of sessions (most recent first), capped at 20.
 */
class SessionStore : public QObject
{
    Q_OBJECT

public:
    explicit SessionStore(QObject *parent = nullptr);
    ~SessionStore() override = default;

    // Add a session to the front of the list for a project root (capped at 20)
    void addSession(const QString &projectRoot, const QString &sessionId, const QString &name, const QString &note = QString());

    // Rename a session
    void renameSession(const QString &projectRoot, const QString &sessionId, const QString &newName);

    // Set or update the note for a session
    void setSessionNote(const QString &projectRoot, const QString &sessionId, const QString &note);

    // List all sessions for a project root, most recent first
    QList<SessionEntry> listSessions(const QString &projectRoot) const;

    // Check if any sessions exist for a project root
    bool hasSession(const QString &projectRoot) const;

    // Remove a specific session entry from the list
    void removeSession(const QString &projectRoot, const QString &sessionId);

    // Clear all sessions for a project root
    void clearSession(const QString &projectRoot);

    // Compatibility: get the most recent session ID (empty if none)
    QString getLastSession(const QString &projectRoot) const;

    // Compatibility: save a single session (adds to front of list with auto-name)
    void saveSession(const QString &projectRoot, const QString &sessionId);

    // List all project roots that have saved sessions
    QStringList listAllProjectRoots() const;

private:
    QString normalizeKey(const QString &projectRoot) const;
    void writeAll(const QString &projectRoot, const QList<SessionEntry> &sessions);

    QSettings m_settings;
    static constexpr int MaxSessions = 20;
};
