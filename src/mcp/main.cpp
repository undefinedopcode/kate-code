/*
    SPDX-License-Identifier: MIT
    SPDX-FileCopyrightText: 2025 Kate Code contributors

    Kate MCP Server - standalone MCP server for Kate editor integration.
    Speaks JSON-RPC 2.0 over stdin/stdout (newline-delimited).
    Uses QSocketNotifier + event loop so that DBus calls work.
*/

#include "MCPServer.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <unistd.h>

// write() can be interrupted or partial (large read responses exceed the pipe
// buffer); emitting a truncated JSON line would corrupt the protocol.
static void writeAll(const QByteArray &data)
{
    const char *ptr = data.constData();
    qint64 remaining = data.size();
    while (remaining > 0) {
        const ssize_t n = write(STDOUT_FILENO, ptr, static_cast<size_t>(remaining));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;  // client gone (EPIPE etc.); EOF on stdin will end us
        }
        ptr += n;
        remaining -= n;
    }
}

static void processLine(const QByteArray &line, MCPServer &server)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return;
    }

    const QJsonObject response = server.handleMessage(doc.object());
    if (response.isEmpty()) {
        return;
    }

    writeAll(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // A client tearing down its pipe must not kill us via SIGPIPE mid-write;
    // the subsequent EOF on stdin ends the process cleanly instead.
    signal(SIGPIPE, SIG_IGN);

    MCPServer server;
    QByteArray buffer;

    auto *notifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, [&]() {
        char buf[4096];
        const ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Interrupted read or spurious wakeup (parents such as node can
            // leave the shared pipe description non-blocking) — NOT end of
            // input. Quitting here silently killed the MCP connection.
            return;
        }
        if (n <= 0) {
            // EOF or a real error — quit
            notifier->setEnabled(false);
            QCoreApplication::quit();
            return;
        }

        buffer.append(buf, static_cast<int>(n));

        // Process complete lines
        int pos;
        while ((pos = buffer.indexOf('\n')) >= 0) {
            const QByteArray line = buffer.left(pos);
            buffer.remove(0, pos + 1);
            processLine(line, server);
        }
    });

    return app.exec();
}
