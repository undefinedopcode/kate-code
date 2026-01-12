#pragma once

#include <QDialog>

class QRadioButton;
class QLabel;
class QPushButton;

/**
 * SessionSelectionDialog - Asks user whether to resume or start new session.
 *
 * Shown when connecting to a project that has a stored session ID.
 */
class SessionSelectionDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Result {
        Resume,
        NewSession,
        Cancelled
    };

    explicit SessionSelectionDialog(const QString &sessionId, QWidget *parent = nullptr);
    ~SessionSelectionDialog() override = default;

    Result selectedResult() const { return m_result; }

private Q_SLOTS:
    void onContinueClicked();

private:
    QRadioButton *m_resumeRadio;
    QRadioButton *m_newRadio;
    Result m_result;
};
