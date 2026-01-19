#include "TerminalManager.h"

#include <QDebug>
#include <QEventLoop>
#include <QTimer>

#include <KPtyDevice>

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

    auto *process = new KPtyProcess(this);
    process->setWorkingDirectory(cwd);
    process->setProcessEnvironment(env);

    // Use PTY for all channels - this makes programs think they're in a real terminal
    process->setPtyChannels(KPtyProcess::AllChannels);

    // Store terminal ID in process property for slot handlers
    process->setProperty("terminalId", terminalId);

    connect(process, &KPtyProcess::finished, this, &TerminalManager::onProcessFinished);
    connect(process, &KPtyProcess::errorOccurred, this, &TerminalManager::onProcessError);

    TerminalData data;
    data.process = process;
    data.outputByteLimit = outputByteLimit;
    data.command = command;
    m_terminals.insert(terminalId, data);

    process->setProgram(command);
    process->setArguments(args);
    process->start();

    if (!process->waitForStarted(5000)) {
        qWarning() << "[TerminalManager] Failed to start process for terminal" << terminalId;
        m_terminals.remove(terminalId);
        process->deleteLater();
        return QString();
    }

    // Connect to PTY device for reading output after process starts
    KPtyDevice *pty = process->pty();
    if (pty) {
        pty->setProperty("terminalId", terminalId);
        connect(pty, &KPtyDevice::readyRead, this, &TerminalManager::onProcessReadyRead);

        // Set terminal window size so programs know available columns/rows
        pty->setWinSize(m_defaultRows, m_defaultColumns);
    }

    qDebug() << "[TerminalManager] Terminal" << terminalId << "started with PTY size" << m_defaultColumns << "x" << m_defaultRows;
    return terminalId;
}

void TerminalManager::onProcessReadyRead()
{
    auto *pty = qobject_cast<KPtyDevice *>(sender());
    if (!pty) {
        return;
    }

    QString terminalId = pty->property("terminalId").toString();
    if (!m_terminals.contains(terminalId)) {
        return;
    }

    QByteArray newData = pty->readAll();
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

    auto *process = qobject_cast<KPtyProcess *>(sender());
    if (!process) {
        return;
    }

    QString terminalId = process->property("terminalId").toString();
    if (!m_terminals.contains(terminalId)) {
        return;
    }

    qDebug() << "[TerminalManager] Terminal" << terminalId << "finished with exit code:" << exitCode;

    // Read any remaining output from PTY
    KPtyDevice *pty = process->pty();
    if (pty) {
        QByteArray remaining = pty->readAll();
        if (!remaining.isEmpty()) {
            m_terminals[terminalId].outputBuffer.append(remaining);
            truncateOutputIfNeeded(terminalId);
        }
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
    auto *process = qobject_cast<KPtyProcess *>(sender());
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

void TerminalManager::setDefaultTerminalSize(int columns, int rows)
{
    m_defaultColumns = qBound(40, columns, 500);  // Reasonable bounds
    m_defaultRows = qBound(10, rows, 200);
    qDebug() << "[TerminalManager] Default terminal size set to" << m_defaultColumns << "x" << m_defaultRows;
}
