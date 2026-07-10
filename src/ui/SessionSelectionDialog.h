#pragma once

#include "../util/SessionStore.h"
#include <QDialog>

class QComboBox;
class QListWidget;
class QLineEdit;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class SummaryStore;

/**
 * SessionSelectionDialog - Shown on Connect when saved sessions exist.
 *
 * Lists saved sessions most-recent-first. "New Session" is always at the top.
 * Selecting an existing session shows an editable note and a Rename button.
 * Selecting "New Session" shows name + note input fields.
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
                                    SessionStore *sessionStore,
                                    SummaryStore *summaryStore,
                                    QWidget *parent = nullptr);
    ~SessionSelectionDialog() override = default;

    Result selectedResult() const { return m_result; }
    QString selectedSessionId() const { return m_selectedSessionId; }
    QString selectedSessionName() const { return m_selectedSessionName; }
    QString selectedSessionNote() const { return m_selectedSessionNote; }
    QString selectedCwd() const { return m_selectedCwd; }

private Q_SLOTS:
    void onItemChanged(int row);
    void onContinueClicked();
    void onRenameClicked();
    void onDeleteClicked();
    void onBrowseClicked();
    void onRootChanged(const QString &root);
    void saveCurrentNote();

private:
    QString m_projectRoot;
    SessionStore *m_sessionStore;

    QComboBox *m_rootCombo;
    QListWidget *m_sessionList;
    QStackedWidget *m_detailStack;

    // New session page widgets
    QLineEdit *m_nameEdit;
    QTextEdit *m_newNoteEdit;

    // Existing session page widgets
    QTextEdit *m_existingNoteEdit;
    QPushButton *m_renameButton;
    QPushButton *m_deleteButton;

    // Manual ID page widgets
    QLineEdit *m_idEdit;

    QString m_lastExistingSessionId; // tracks which session's note is loaded

    Result m_result;
    QString m_selectedSessionId;
    QString m_selectedSessionName;
    QString m_selectedSessionNote;
    QString m_selectedCwd;
};
