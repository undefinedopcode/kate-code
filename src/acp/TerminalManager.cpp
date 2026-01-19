#include "TerminalManager.h"

#include <QDebug>
#include <QEventLoop>
#include <QTimer>

TerminalManager::TerminalManager(QObject *parent)
    : QObject(parent)
    , m_idCounter(0)
{
}

TerminalManager::~TerminalManager()
{
    releaseAll();
}

QString TerminalManager::generateTerminalId()
{
    return QStringLiteral("term_%1").arg(++m_idCounter);
}

QString TerminalManager::createTerminal(const QString &command, const QStringList &args,
                                        const QProcessEnvironment &env, const QString &cwd,
                                        qint64 outputByteLimit)
{
    QString terminalId = generateTerminalId();

    qDebug() << "[TerminalManager] Creating terminal" << terminalId << "command:" << command << "args:" << args;

    auto *process = new QProcess(this);
    process->setWorkingDirectory(cwd);
    process->setProcessEnvironment(env);

    // Merge stdout and stderr for unified output
    process->setProcessChannelMode(QProcess::MergedChannels);

    // Store terminal ID in process property for slot handlers
    process->setProperty("terminalId", terminalId);

    connect(process, &QProcess::readyReadStandardOutput, this, &TerminalManager::onProcessReadyRead);
    connect(process, &QProcess::finished, this, &TerminalManager::onProcessFinished);
    connect(process, &QProcess::errorOccurred, this, &TerminalManager::onProcessError);

    TerminalData data;
    data.process = process;
    data.outputByteLimit = outputByteLimit;
    data.command = command;
    m_terminals.insert(terminalId, data);

    process->start(command, args);

    if (!process->waitForStarted(5000)) {
        qWarning() << "[TerminalManager] Failed to start process for terminal" << terminalId;
        m_terminals.remove(terminalId);
        process->deleteLater();
        return QString();
    }

    qDebug() << "[TerminalManager] Terminal" << terminalId << "started successfully";
    return terminalId;
}

void TerminalManager::onProcessReadyRead()
{
    auto *process = qobject_cast<QProcess *>(sender());
    if (!process) {
        return;
    }

    QString terminalId = process->property("terminalId").toString();
    if (!m_terminals.contains(terminalId)) {
        return;
    }

    QByteArray newData = process->readAllStandardOutput();
    m_terminals[terminalId].outputBuffer.append(newData);

    truncateOutputIfNeeded(terminalId);

    // Emit signal for live UI updates
    QString output = QString::fromUtf8(m_terminals[terminalId].outputBuffer);
    bool finished = m_terminals[terminalId].status != TerminalStatus::Running;
    Q_EMIT outputAvailable(terminalId, output, finished);
}

void TerminalManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);

    auto *process = qobject_cast<QProcess *>(sender());
    if (!process) {
        return;
    }

    QString terminalId = process->property("terminalId").toString();
    if (!m_terminals.contains(terminalId)) {
        return;
    }

    qDebug() << "[TerminalManager] Terminal" << terminalId << "finished with exit code:" << exitCode;

    // Read any remaining output
    QByteArray remaining = process->readAllStandardOutput();
    if (!remaining.isEmpty()) {
        m_terminals[terminalId].outputBuffer.append(remaining);
        truncateOutputIfNeeded(terminalId);
    }

    m_terminals[terminalId].exitCode = exitCode;
    if (m_terminals[terminalId].status == TerminalStatus::Running) {
        m_terminals[terminalId].status = TerminalStatus::Exited;
    }

    // Emit final output update and exit signal
    QString output = QString::fromUtf8(m_terminals[terminalId].outputBuffer);
    Q_EMIT outputAvailable(terminalId, output, true);
    Q_EMIT terminalExited(terminalId, exitCode);
}

void TerminalManager::onProcessError(QProcess::ProcessError error)
{
    auto *process = qobject_cast<QProcess *>(sender());
    if (!process) {
        return;
    }

    QString terminalId = process->property("terminalId").toString();
    qWarning() << "[TerminalManager] Terminal" << terminalId << "error:" << error << process->errorString();
}

void TerminalManager::truncateOutputIfNeeded(const QString &terminalId)
{
    if (!m_terminals.contains(terminalId)) {
        return;
    }

    auto &data = m_terminals[terminalId];
    if (data.outputByteLimit > 0 && data.outputBuffer.size() > data.outputByteLimit) {
        // Truncate from the beginning to stay within limit
        qint64 excess = data.outputBuffer.size() - data.outputByteLimit;
        data.outputBuffer.remove(0, excess);
        data.truncated = true;
    }
}

TerminalManager::OutputResult TerminalManager::getOutput(const QString &terminalId) const
{
    OutputResult result;

    if (!m_terminals.contains(terminalId)) {
        qWarning() << "[TerminalManager] getOutput: terminal not found:" << terminalId;
        return result;
    }

    const auto &data = m_terminals[terminalId];
    result.output = QString::fromUtf8(data.outputBuffer);
    result.truncated = data.truncated;

    if (data.status != TerminalStatus::Running) {
        result.exitStatus = data.exitCode;
    }

    return result;
}

TerminalManager::WaitResult TerminalManager::waitForExit(const QString &terminalId, int timeoutMs)
{
    WaitResult result;

    if (!m_terminals.contains(terminalId)) {
        qWarning() << "[TerminalManager] waitForExit: terminal not found:" << terminalId;
        return result;
    }

    auto &data = m_terminals[terminalId];

    // If already finished, return immediately
    if (data.status != TerminalStatus::Running) {
        result.output = QString::fromUtf8(data.outputBuffer);
        result.truncated = data.truncated;
        result.exitStatus = data.exitCode;
        result.success = true;
        return result;
    }

    // Wait for process to finish using event loop
    QEventLoop loop;

    // Connect to finished signal
    QMetaObject::Connection finishedConn = connect(data.process, &QProcess::finished,
        &loop, &QEventLoop::quit);

    // Set up timeout if specified
    QTimer timer;
    if (timeoutMs > 0) {
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
    }

    // Wait
    loop.exec();

    disconnect(finishedConn);
    timer.stop();

    // Check if we timed out
    if (data.status == TerminalStatus::Running) {
        // Timeout occurred
        qDebug() << "[TerminalManager] waitForExit: timeout for terminal" << terminalId;
        result.output = QString::fromUtf8(data.outputBuffer);
        result.truncated = data.truncated;
        result.success = false;
        return result;
    }

    result.output = QString::fromUtf8(data.outputBuffer);
    result.truncated = data.truncated;
    result.exitStatus = data.exitCode;
    result.success = true;
    return result;
}

bool TerminalManager::killTerminal(const QString &terminalId)
{
    if (!m_terminals.contains(terminalId)) {
        qWarning() << "[TerminalManager] killTerminal: terminal not found:" << terminalId;
        return false;
    }

    auto &data = m_terminals[terminalId];

    if (data.status != TerminalStatus::Running) {
        // Already stopped
        return true;
    }

    qDebug() << "[TerminalManager] Killing terminal" << terminalId;

    if (data.process) {
        data.process->kill();
        data.process->waitForFinished(1000);
    }

    data.status = TerminalStatus::Killed;
    return true;
}

bool TerminalManager::releaseTerminal(const QString &terminalId)
{
    if (!m_terminals.contains(terminalId)) {
        qWarning() << "[TerminalManager] releaseTerminal: terminal not found:" << terminalId;
        return false;
    }

    qDebug() << "[TerminalManager] Releasing terminal" << terminalId;

    auto &data = m_terminals[terminalId];

    if (data.process) {
        // Disconnect signals before cleanup
        disconnect(data.process, nullptr, this, nullptr);

        if (data.status == TerminalStatus::Running) {
            data.process->kill();
            data.process->waitForFinished(1000);
        }
        data.process->deleteLater();
    }

    m_terminals.remove(terminalId);
    return true;
}

bool TerminalManager::isValid(const QString &terminalId) const
{
    return m_terminals.contains(terminalId);
}

void TerminalManager::releaseAll()
{
    qDebug() << "[TerminalManager] Releasing all terminals (" << m_terminals.size() << "terminals)";

    QStringList ids = m_terminals.keys();
    for (const QString &id : ids) {
        releaseTerminal(id);
    }
}
