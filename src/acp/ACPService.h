#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

class ACPService : public QObject
{
    Q_OBJECT

public:
    explicit ACPService(QObject *parent = nullptr);
    ~ACPService() override;

    bool start(const QString &workingDir);
    void stop();

    int sendRequest(const QString &method, const QJsonObject &params = QJsonObject());
    void sendResponse(int requestId, const QJsonObject &result = QJsonObject(), const QJsonObject &error = QJsonObject());

    bool isRunning() const;

Q_SIGNALS:
    void notificationReceived(const QString &method, const QJsonObject &params, int requestId);
    void responseReceived(int id, const QJsonObject &result, const QJsonObject &error);
    void connected();
    void disconnected(int exitCode);
    void errorOccurred(const QString &message);

private Q_SLOTS:
    void onStdout();
    void onStderr();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onError(QProcess::ProcessError error);

private:
    void handleMessage(const QJsonObject &msg);

    QProcess *m_process;
    int m_messageId;
    QString m_buffer;
};
