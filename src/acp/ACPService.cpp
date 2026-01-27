#include "ACPService.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

ACPService::ACPService(QObject *parent)
    : QObject(parent)
    , m_process(nullptr)
    , m_messageId(0)
    , m_executable(QStringLiteral("claude-code-acp"))
{
}

void ACPService::setExecutable(const QString &executable, const QStringList &args)
{
    m_executable = executable;
    m_executableArgs = args;
}

ACPService::~ACPService()
{
    // Disconnect signals before cleanup to prevent signal emission during destruction
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
    }
    stop();
}

bool ACPService::start(const QString &workingDir)
{
    qDebug() << "[ACPService] Starting" << m_executable << "in:" << workingDir;

    if (m_process) {
        qDebug() << "[ACPService] Stopping existing process";
        stop();
    }

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(workingDir);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &ACPService::onStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &ACPService::onStderr);
    connect(m_process, &QProcess::finished, this, &ACPService::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ACPService::onError);

    // Resolve executable path - when launched from desktop environments,
    // user-local paths like ~/.local/bin may not be on PATH
    QString resolvedExecutable = m_executable;
    if (!QFileInfo(resolvedExecutable).isAbsolute()) {
        QString found = QStandardPaths::findExecutable(resolvedExecutable);
        if (found.isEmpty()) {
            // Fallback: check common user-local directories where curl|bash
            // installers and package managers typically place binaries
            const QString home = QDir::homePath();
            const QStringList fallbackDirs = {
                home + QStringLiteral("/.local/bin"),
                home + QStringLiteral("/bin"),
                home + QStringLiteral("/.cargo/bin"),
            };
            for (const QString &dir : fallbackDirs) {
                QString candidate = dir + QLatin1Char('/') + resolvedExecutable;
                if (QFileInfo::exists(candidate)) {
                    found = candidate;
                    break;
                }
            }
        }
        if (!found.isEmpty()) {
            resolvedExecutable = found;
        }
    }

    qDebug() << "[ACPService] Starting process:" << resolvedExecutable << m_executableArgs;
    m_process->start(resolvedExecutable, m_executableArgs);

    qDebug() << "[ACPService] Waiting for process to start...";
    if (!m_process->waitForStarted(5000)) {
        qDebug() << "[ACPService] Process failed to start";
        Q_EMIT errorOccurred(QStringLiteral("Failed to start %1").arg(m_executable));
        return false;
    }

    qDebug() << "[ACPService] Process started successfully";
    Q_EMIT connected();
    return true;
}

void ACPService::stop()
{
    if (m_process) {
        // Disconnect signals BEFORE killing to prevent onFinished from being called
        // This avoids a race condition where both stop() and onFinished() try to clean up m_process
        disconnect(m_process, nullptr, this, nullptr);

        m_process->kill();
        m_process->waitForFinished(1000);
        m_process->deleteLater();
        m_process = nullptr;

        // Emit disconnected signal since onFinished won't be called (signals disconnected)
        Q_EMIT disconnected(0);
    }
}

int ACPService::sendRequest(const QString &method, const QJsonObject &params)
{
    if (!m_process || m_process->state() != QProcess::Running) {
        qWarning() << "[ACPService] Cannot send request: ACP not connected";
        return -1;
    }

    m_messageId++;

    QJsonObject msg;
    msg[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    msg[QStringLiteral("id")] = m_messageId;
    msg[QStringLiteral("method")] = method;

    if (!params.isEmpty()) {
        msg[QStringLiteral("params")] = params;
    }

    QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n";
    qDebug() << "[ACPService] >>" << method << "id:" << m_messageId;

    m_process->write(data);
    return m_messageId;
}

void ACPService::sendNotification(const QString &method, const QJsonObject &params)
{
    if (!m_process || m_process->state() != QProcess::Running) {
        qWarning() << "[ACPService] Cannot send notification: ACP not connected";
        return;
    }

    QJsonObject msg;
    msg[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    msg[QStringLiteral("method")] = method;

    if (!params.isEmpty()) {
        msg[QStringLiteral("params")] = params;
    }

    QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n";
    qDebug() << "[ACPService] >> notification:" << method;

    m_process->write(data);
}

void ACPService::sendResponse(int requestId, const QJsonObject &result, const QJsonObject &error)
{
    if (!m_process || m_process->state() != QProcess::Running) {
        return;
    }

    QJsonObject msg;
    msg[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    msg[QStringLiteral("id")] = requestId;

    if (!error.isEmpty()) {
        msg[QStringLiteral("error")] = error;
    } else {
        msg[QStringLiteral("result")] = result;
    }

    QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n";
    qDebug() << "[ACPService] >> response for request id:" << requestId;

    m_process->write(data);
}

bool ACPService::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

void ACPService::onStdout()
{
    if (!m_process) {
        return;
    }

    QByteArray data = m_process->readAllStandardOutput();
    m_buffer += QString::fromUtf8(data);

    // Parse newline-delimited JSON
    QStringList lines = m_buffer.split(QLatin1Char('\n'));
    m_buffer = lines.takeLast();  // Keep incomplete line in buffer

    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "[ACPService] Failed to parse JSON:" << parseError.errorString();
            qWarning() << "[ACPService] Line:" << line;
            continue;
        }

        handleMessage(doc.object());
    }
}

void ACPService::handleMessage(const QJsonObject &msg)
{
    if (msg.contains(QStringLiteral("method"))) {
        // Notification or request from ACP
        QString method = msg[QStringLiteral("method")].toString();
        QJsonObject params = msg[QStringLiteral("params")].toObject();
        int requestId = msg[QStringLiteral("id")].toInt(-1);

        // Log session/update type for debugging
        if (method == QStringLiteral("session/update")) {
            QJsonObject update = params[QStringLiteral("update")].toObject();
            QString updateType = update[QStringLiteral("sessionUpdate")].toString();
            qDebug() << "[ACPService] <<" << method << "(type:" << updateType << ")";
        } else {
            qDebug() << "[ACPService] <<" << method;
        }

        Q_EMIT notificationReceived(method, params, requestId);
    } else if (msg.contains(QStringLiteral("id"))) {
        // Response to our request
        int id = msg[QStringLiteral("id")].toInt();
        QJsonObject result = msg[QStringLiteral("result")].toObject();
        QJsonObject error = msg[QStringLiteral("error")].toObject();

        qDebug() << "[ACPService] << response for request id:" << id
                 << "raw:" << QJsonDocument(msg).toJson(QJsonDocument::Compact);
        Q_EMIT responseReceived(id, result, error);
    }
}

void ACPService::onStderr()
{
    if (!m_process) {
        return;
    }

    QByteArray data = m_process->readAllStandardError();
    QString message = QString::fromUtf8(data).trimmed();

    if (!message.isEmpty()) {
        qDebug() << "[ACPService] stderr:" << message;
        Q_EMIT errorOccurred(message);
    }
}

void ACPService::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);
    qDebug() << "[ACPService] Process finished with exit code:" << exitCode;

    if (m_process) {
        // Disconnect signals to prevent further callbacks during cleanup
        disconnect(m_process, nullptr, this, nullptr);
        m_process->deleteLater();
        m_process = nullptr;
    }

    Q_EMIT disconnected(exitCode);
}

void ACPService::onError(QProcess::ProcessError error)
{
    QString errorMsg = m_process ? m_process->errorString() : QString::number(error);
    qWarning() << "[ACPService] Process error:" << errorMsg;
    Q_EMIT errorOccurred(errorMsg);
}
