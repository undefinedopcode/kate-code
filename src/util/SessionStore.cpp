#include "SessionStore.h"

#include <QDebug>
#include <QDir>

SessionStore::SessionStore(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("katecode"), QStringLiteral("kate-code"))
{
    qDebug() << "[SessionStore] Initialized, config file:" << m_settings.fileName();
}

void SessionStore::addSession(const QString &projectRoot, const QString &sessionId, const QString &name, const QString &note)
{
    if (projectRoot.isEmpty() || sessionId.isEmpty()) {
        qWarning() << "[SessionStore] Cannot add: empty project root or session ID";
        return;
    }

    QString key = normalizeKey(projectRoot);
    QString sessionName = name.isEmpty()
        ? QStringLiteral("Session %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd")))
        : name;

    // Load existing sessions
    QList<SessionEntry> existing = listSessions(projectRoot);

    // If session already exists, preserve its name/note unless new ones are provided
    QString resolvedNote = note;
    for (const SessionEntry &e : existing) {
        if (e.id == sessionId) {
            if (name.isEmpty()) sessionName = e.name;
            if (note.isEmpty()) resolvedNote = e.note;
            break;
        }
    }

    // Remove duplicate if same ID already in list
    existing.erase(std::remove_if(existing.begin(), existing.end(),
        [&sessionId](const SessionEntry &e) { return e.id == sessionId; }),
        existing.end());

    // Prepend new entry
    SessionEntry entry;
    entry.id = sessionId;
    entry.name = sessionName;
    entry.note = resolvedNote;
    entry.timestamp = QDateTime::currentDateTime();
    existing.prepend(entry);

    // Cap at MaxSessions
    if (existing.size() > MaxSessions) {
        existing = existing.mid(0, MaxSessions);
    }

    // Write back
    m_settings.beginGroup(QStringLiteral("Sessions2"));
    m_settings.beginGroup(key);
    m_settings.remove(QString()); // clear existing entries for this key
    m_settings.beginWriteArray(QStringLiteral("sessions"));
    for (int i = 0; i < existing.size(); ++i) {
        m_settings.setArrayIndex(i);
        m_settings.setValue(QStringLiteral("id"), existing.at(i).id);
        m_settings.setValue(QStringLiteral("name"), existing.at(i).name);
        m_settings.setValue(QStringLiteral("note"), existing.at(i).note);
        m_settings.setValue(QStringLiteral("timestamp"), existing.at(i).timestamp.toString(Qt::ISODate));
    }
    m_settings.endArray();
    m_settings.endGroup();
    m_settings.endGroup();
    m_settings.sync();

    qDebug() << "[SessionStore] Added session" << sessionId << "for" << projectRoot;
}

QList<SessionEntry> SessionStore::listSessions(const QString &projectRoot) const
{
    if (projectRoot.isEmpty()) {
        return {};
    }

    QString key = normalizeKey(projectRoot);
    QSettings &settings = const_cast<QSettings &>(m_settings);

    settings.beginGroup(QStringLiteral("Sessions2"));
    settings.beginGroup(key);
    int count = settings.beginReadArray(QStringLiteral("sessions"));

    QList<SessionEntry> result;
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        SessionEntry entry;
        entry.id = settings.value(QStringLiteral("id")).toString();
        entry.name = settings.value(QStringLiteral("name")).toString();
        entry.note = settings.value(QStringLiteral("note")).toString();
        entry.timestamp = QDateTime::fromString(
            settings.value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
        if (!entry.id.isEmpty()) {
            result.append(entry);
        }
    }

    settings.endArray();
    settings.endGroup();
    settings.endGroup();

    return result;
}

bool SessionStore::hasSession(const QString &projectRoot) const
{
    return !listSessions(projectRoot).isEmpty();
}

void SessionStore::removeSession(const QString &projectRoot, const QString &sessionId)
{
    QList<SessionEntry> sessions = listSessions(projectRoot);
    sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
        [&sessionId](const SessionEntry &e) { return e.id == sessionId; }),
        sessions.end());
    writeAll(projectRoot, sessions);
}

void SessionStore::renameSession(const QString &projectRoot, const QString &sessionId, const QString &newName)
{
    QList<SessionEntry> sessions = listSessions(projectRoot);
    for (auto &entry : sessions) {
        if (entry.id == sessionId) {
            entry.name = newName;
            break;
        }
    }
    writeAll(projectRoot, sessions);
}

void SessionStore::setSessionNote(const QString &projectRoot, const QString &sessionId, const QString &note)
{
    QList<SessionEntry> sessions = listSessions(projectRoot);
    for (auto &entry : sessions) {
        if (entry.id == sessionId) {
            entry.note = note;
            break;
        }
    }
    writeAll(projectRoot, sessions);
}

void SessionStore::clearSession(const QString &projectRoot)
{
    if (projectRoot.isEmpty()) {
        return;
    }

    QString key = normalizeKey(projectRoot);
    m_settings.beginGroup(QStringLiteral("Sessions2"));
    m_settings.remove(key);
    m_settings.endGroup();
    m_settings.sync();

    qDebug() << "[SessionStore] Cleared sessions for" << projectRoot;
}

QString SessionStore::getLastSession(const QString &projectRoot) const
{
    QList<SessionEntry> sessions = listSessions(projectRoot);
    return sessions.isEmpty() ? QString() : sessions.first().id;
}

void SessionStore::saveSession(const QString &projectRoot, const QString &sessionId)
{
    addSession(projectRoot, sessionId, QString());
}

void SessionStore::writeAll(const QString &projectRoot, const QList<SessionEntry> &sessions)
{
    QString key = normalizeKey(projectRoot);
    m_settings.beginGroup(QStringLiteral("Sessions2"));
    m_settings.beginGroup(key);
    m_settings.remove(QString());
    m_settings.beginWriteArray(QStringLiteral("sessions"));
    for (int i = 0; i < sessions.size(); ++i) {
        m_settings.setArrayIndex(i);
        m_settings.setValue(QStringLiteral("id"), sessions.at(i).id);
        m_settings.setValue(QStringLiteral("name"), sessions.at(i).name);
        m_settings.setValue(QStringLiteral("note"), sessions.at(i).note);
        m_settings.setValue(QStringLiteral("timestamp"), sessions.at(i).timestamp.toString(Qt::ISODate));
    }
    m_settings.endArray();
    m_settings.endGroup();
    m_settings.endGroup();
    m_settings.sync();
}

QStringList SessionStore::listAllProjectRoots() const
{
    QSettings &settings = const_cast<QSettings &>(m_settings);
    settings.beginGroup(QStringLiteral("Sessions2"));
    QStringList keys = settings.childGroups();
    settings.endGroup();

    QStringList roots;
    for (const QString &key : keys) {
        QString path = key;
        path.replace(QStringLiteral("__"), QStringLiteral("/"));
        if (!path.isEmpty())
            roots.append(path);
    }
    roots.sort();
    return roots;
}

QString SessionStore::normalizeKey(const QString &projectRoot) const
{
    QString normalized = QDir::cleanPath(projectRoot);
    normalized.replace(QLatin1Char('/'), QLatin1String("__"));
    return normalized;
}
