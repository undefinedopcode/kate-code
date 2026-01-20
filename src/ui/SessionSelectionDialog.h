#pragma once

#include <QDialog>

class QRadioButton;
class QLabel;
class QPushButton;
class QTextEdit;
class QComboBox;
class SummaryStore;

/**
 * SessionSelectionDialog - Asks user whether to resume or start new session.
 *
 * Shown when connecting to a project that has stored session summaries.
 * Shows a dropdown of available sessions (up to 10, most recent first)
 * and displays the selected session's summary.
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

    explicit SessionSelectionDialog(const QString &projectRoot,
                                    SummaryStore *summaryStore,
                                    QWidget *parent = nullptr);
    ~SessionSelectionDialog() override = default;

    Result selectedResult() const { return m_result; }
    QString selectedSessionId() const;

private Q_SLOTS:
    void onContinueClicked();
    void onSessionChanged(int index);

private:
    QString m_projectRoot;
    SummaryStore *m_summaryStore;
    QComboBox *m_sessionCombo;
    QRadioButton *m_resumeRadio;
    QRadioButton *m_newRadio;
    QTextEdit *m_summaryPreview;
    Result m_result;
};
