#include "SessionStore.h"

#include <QDebug>
#include <QDir>

SessionStore::SessionStore(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("katecode"), QStringLiteral("kate-code"))
{
    qDebug() << "[SessionStore] Initialized, config file:" << m_settings.fileName();
}

void SessionStore::saveSession(const QString &projectRoot, const QString &sessionId)
{
    if (projectRoot.isEmpty() || sessionId.isEmpty()) {
        qWarning() << "[SessionStore] Cannot save: empty project root or session ID";
        return;
    }

    QString key = normalizeKey(projectRoot);

    m_settings.beginGroup(QStringLiteral("Sessions"));
    m_settings.setValue(key, sessionId);
    m_settings.endGroup();
    m_settings.sync();

    qDebug() << "[SessionStore] Saved session for" << projectRoot << ":" << sessionId;
}

QString SessionStore::getLastSession(const QString &projectRoot) const
{
    if (projectRoot.isEmpty()) {
        return QString();
    }

    QString key = normalizeKey(projectRoot);

    // Need to cast away const for QSettings access
    QSettings &settings = const_cast<QSettings &>(m_settings);

    settings.beginGroup(QStringLiteral("Sessions"));
    QString sessionId = settings.value(key).toString();
    settings.endGroup();

    if (!sessionId.isEmpty()) {
        qDebug() << "[SessionStore] Found session for" << projectRoot << ":" << sessionId;
    }

    return sessionId;
}

void SessionStore::clearSession(const QString &projectRoot)
{
    if (projectRoot.isEmpty()) {
        return;
    }

    QString key = normalizeKey(projectRoot);

    m_settings.beginGroup(QStringLiteral("Sessions"));
    m_settings.remove(key);
    m_settings.endGroup();
    m_settings.sync();

    qDebug() << "[SessionStore] Cleared session for" << projectRoot;
}

bool SessionStore::hasSession(const QString &projectRoot) const
{
    return !getLastSession(projectRoot).isEmpty();
}

QString SessionStore::normalizeKey(const QString &projectRoot) const
{
    // Use cleaned absolute path as key, but replace slashes for QSettings compatibility
    QString normalized = QDir::cleanPath(projectRoot);

    // Replace slashes with a separator that works in QSettings keys
    // QSettings on some platforms treats slashes as group separators
    normalized.replace(QLatin1Char('/'), QLatin1String("__"));

    return normalized;
}
