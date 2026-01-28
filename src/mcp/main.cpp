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

#include <unistd.h>

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

    const QByteArray data = QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n';
    write(STDOUT_FILENO, data.constData(), data.size());
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    MCPServer server;
    QByteArray buffer;

    auto *notifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, [&]() {
        char buf[4096];
        const ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) {
            // EOF or error — quit
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
