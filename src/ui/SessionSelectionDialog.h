#pragma once

#include <QDialog>

class QRadioButton;
class QLabel;
class QPushButton;
class QTextEdit;

/**
 * SessionSelectionDialog - Asks user whether to resume or start new session.
 *
 * Shown when connecting to a project that has a stored session ID.
 * Optionally shows a summary preview if available.
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

    explicit SessionSelectionDialog(const QString &sessionId,
                                    const QString &summaryContent = QString(),
                                    QWidget *parent = nullptr);
    ~SessionSelectionDialog() override = default;

    Result selectedResult() const { return m_result; }

private Q_SLOTS:
    void onContinueClicked();

private:
    QRadioButton *m_resumeRadio;
    QRadioButton *m_newRadio;
    QTextEdit *m_summaryPreview;
    Result m_result;
};
