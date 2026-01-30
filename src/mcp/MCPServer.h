/*
    SPDX-License-Identifier: MIT
    SPDX-FileCopyrightText: 2025 Kate Code contributors
*/

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class MCPServer
{
public:
    MCPServer();

    // Process a single JSON-RPC message and return the response.
    // Returns a null QJsonObject for notifications (no response needed).
    QJsonObject handleMessage(const QJsonObject &msg);

private:
    QJsonObject handleInitialize(int id, const QJsonObject &params);
    QJsonObject handleToolsList(int id, const QJsonObject &params);
    QJsonObject handleToolsCall(int id, const QJsonObject &params);

    QJsonObject executeDocuments(const QJsonObject &arguments);
    QJsonObject executeRead(const QJsonObject &arguments);
    QJsonObject executeEdit(const QJsonObject &arguments);
    QJsonObject executeWrite(const QJsonObject &arguments);
    QJsonObject executeAskUserQuestion(const QJsonObject &arguments);

    QJsonObject makeResponse(int id, const QJsonObject &result);
    QJsonObject makeErrorResponse(int id, int code, const QString &message);
    QJsonObject makeErrorResult(const QString &message);

    bool m_initialized = false;
};
