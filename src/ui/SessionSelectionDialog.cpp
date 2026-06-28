#include "SessionSelectionDialog.h"
#include "../util/SummaryStore.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>

static constexpr int NewSessionRow = 0;
static constexpr int NewSessionDetailPage = 0;
static constexpr int ExistingSessionDetailPage = 1;
static constexpr int ManualIdDetailPage = 2;
static const QString ManualIdSentinel = QStringLiteral("__manual__");

SessionSelectionDialog::SessionSelectionDialog(const QString &projectRoot,
                                               SessionStore *sessionStore,
                                               SummaryStore *summaryStore,
                                               QWidget *parent)
    : QDialog(parent)
    , m_projectRoot(projectRoot)
    , m_sessionStore(sessionStore)
    , m_rootCombo(nullptr)
    , m_sessionList(nullptr)
    , m_detailStack(nullptr)
    , m_nameEdit(nullptr)
    , m_newNoteEdit(nullptr)
    , m_existingNoteEdit(nullptr)
    , m_renameButton(nullptr)
    , m_idEdit(nullptr)
    , m_result(Result::Cancelled)
    , m_selectedCwd(projectRoot)
{
    Q_UNUSED(summaryStore)

    setWindowTitle(tr("Connect to Session"));
    setMinimumSize(520, 460);
    resize(620, 540);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    // Directory picker
    auto *dirRow = new QHBoxLayout();
    dirRow->addWidget(new QLabel(tr("Working directory:"), this));
    m_rootCombo = new QComboBox(this);
    m_rootCombo->setEditable(true);
    QStringList knownRoots = sessionStore->listAllProjectRoots();
    if (!knownRoots.contains(projectRoot))
        knownRoots.prepend(projectRoot);
    m_rootCombo->addItems(knownRoots);
    m_rootCombo->setCurrentText(projectRoot);
    dirRow->addWidget(m_rootCombo, 1);
    auto *browseButton = new QPushButton(tr("Browse…"), this);
    dirRow->addWidget(browseButton);
    layout->addLayout(dirRow);

    auto *descLabel = new QLabel(tr("Select a session to resume, or start a new one:"), this);
    layout->addWidget(descLabel);

    // Session list
    m_sessionList = new QListWidget(this);
    m_sessionList->setSelectionMode(QAbstractItemView::SingleSelection);

    auto *newItem = new QListWidgetItem(tr("+ New Session"), m_sessionList);
    newItem->setData(Qt::UserRole, QString());

    QList<SessionEntry> sessions = m_sessionStore->listSessions(projectRoot);
    for (const SessionEntry &entry : sessions) {
        QString label = entry.name + QStringLiteral("  —  ") +
                        entry.timestamp.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        auto *item = new QListWidgetItem(label, m_sessionList);
        item->setData(Qt::UserRole, entry.id);
    }

    auto *manualItem = new QListWidgetItem(tr("↩ Resume by session ID..."), m_sessionList);
    manualItem->setData(Qt::UserRole, ManualIdSentinel);

    m_sessionList->setCurrentRow(NewSessionRow);
    layout->addWidget(m_sessionList, 1);

    // Detail stack
    m_detailStack = new QStackedWidget(this);

    // Page 0: new session — name + note
    auto *newPage = new QWidget(this);
    auto *newLayout = new QVBoxLayout(newPage);
    newLayout->setContentsMargins(0, 0, 0, 0);
    newLayout->addWidget(new QLabel(tr("Session name (optional):"), newPage));
    m_nameEdit = new QLineEdit(newPage);
    m_nameEdit->setPlaceholderText(
        QDateTime::currentDateTime().toString(QStringLiteral("'Session 'yyyy-MM-dd")));
    newLayout->addWidget(m_nameEdit);
    newLayout->addWidget(new QLabel(tr("Note (optional):"), newPage));
    m_newNoteEdit = new QTextEdit(newPage);
    m_newNoteEdit->setPlaceholderText(tr("What is this session for?"));
    m_newNoteEdit->setMaximumHeight(80);
    newLayout->addWidget(m_newNoteEdit);
    newLayout->addStretch();
    m_detailStack->addWidget(newPage); // index 0

    // Page 1: existing session — editable note + rename button
    auto *existingPage = new QWidget(this);
    auto *existingLayout = new QVBoxLayout(existingPage);
    existingLayout->setContentsMargins(0, 0, 0, 0);

    auto *noteHeaderLayout = new QHBoxLayout();
    noteHeaderLayout->addWidget(new QLabel(tr("Note:"), existingPage));
    noteHeaderLayout->addStretch();
    m_renameButton = new QPushButton(tr("Rename"), existingPage);
    m_renameButton->setFlat(true);
    noteHeaderLayout->addWidget(m_renameButton);
    m_deleteButton = new QPushButton(tr("Delete"), existingPage);
    m_deleteButton->setFlat(true);
    noteHeaderLayout->addWidget(m_deleteButton);
    existingLayout->addLayout(noteHeaderLayout);

    m_existingNoteEdit = new QTextEdit(existingPage);
    m_existingNoteEdit->setPlaceholderText(tr("Add a note about this session..."));
    existingLayout->addWidget(m_existingNoteEdit, 1);
    m_detailStack->addWidget(existingPage); // index 1

    // Page 2: manual ID input
    auto *manualPage = new QWidget(this);
    auto *manualLayout = new QVBoxLayout(manualPage);
    manualLayout->setContentsMargins(0, 0, 0, 0);
    manualLayout->addWidget(new QLabel(tr("Session ID:"), manualPage));
    m_idEdit = new QLineEdit(manualPage);
    m_idEdit->setPlaceholderText(tr("Paste session ID here"));
    manualLayout->addWidget(m_idEdit);
    manualLayout->addStretch();
    m_detailStack->addWidget(manualPage); // index 2

    layout->addWidget(m_detailStack);

    // Buttons
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *continueButton = buttonBox->addButton(tr("Continue"), QDialogButtonBox::AcceptRole);
    continueButton->setDefault(true);

    connect(m_sessionList, &QListWidget::currentRowChanged,
            this, &SessionSelectionDialog::onItemChanged);
    connect(m_renameButton, &QPushButton::clicked,
            this, &SessionSelectionDialog::onRenameClicked);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &SessionSelectionDialog::onDeleteClicked);
    connect(continueButton, &QPushButton::clicked,
            this, &SessionSelectionDialog::onContinueClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseButton, &QPushButton::clicked,
            this, &SessionSelectionDialog::onBrowseClicked);
    connect(m_rootCombo, &QComboBox::currentTextChanged,
            this, &SessionSelectionDialog::onRootChanged);

    layout->addWidget(buttonBox);
}

void SessionSelectionDialog::saveCurrentNote()
{
    if (m_lastExistingSessionId.isEmpty()) {
        return;
    }
    m_sessionStore->setSessionNote(m_projectRoot, m_lastExistingSessionId,
                                   m_existingNoteEdit->toPlainText().trimmed());
}

void SessionSelectionDialog::onItemChanged(int row)
{
    if (row < 0 || row >= m_sessionList->count()) {
        return;
    }

    // Auto-save note for the session we're leaving
    if (m_detailStack->currentIndex() == ExistingSessionDetailPage) {
        saveCurrentNote();
    }

    QString sessionId = m_sessionList->item(row)->data(Qt::UserRole).toString();

    if (row == NewSessionRow) {
        m_lastExistingSessionId.clear();
        m_detailStack->setCurrentIndex(NewSessionDetailPage);
    } else if (sessionId == ManualIdSentinel) {
        m_lastExistingSessionId.clear();
        m_detailStack->setCurrentIndex(ManualIdDetailPage);
        m_idEdit->setFocus();
    } else {
        m_lastExistingSessionId = sessionId;
        m_detailStack->setCurrentIndex(ExistingSessionDetailPage);

        // Load the note for this session
        QList<SessionEntry> sessions = m_sessionStore->listSessions(m_projectRoot);
        for (const SessionEntry &entry : sessions) {
            if (entry.id == sessionId) {
                m_existingNoteEdit->setPlainText(entry.note);
                break;
            }
        }
    }
}

void SessionSelectionDialog::onRenameClicked()
{
    int row = m_sessionList->currentRow();
    if (row <= NewSessionRow || row >= m_sessionList->count() - 1) {
        return;
    }

    QString currentName = m_sessionList->item(row)->text().section(QStringLiteral("  —  "), 0, 0);
    bool ok = false;
    QString newName = QInputDialog::getText(this, tr("Rename Session"),
                                            tr("New name:"), QLineEdit::Normal,
                                            currentName, &ok);
    if (!ok || newName.trimmed().isEmpty()) {
        return;
    }

    newName = newName.trimmed();
    QString sessionId = m_sessionList->item(row)->data(Qt::UserRole).toString();
    m_sessionStore->renameSession(m_projectRoot, sessionId, newName);

    // Update the list item label
    QString timestamp = m_sessionList->item(row)->text().section(QStringLiteral("  —  "), 1);
    m_sessionList->item(row)->setText(newName + QStringLiteral("  —  ") + timestamp);
}

void SessionSelectionDialog::onDeleteClicked()
{
    int row = m_sessionList->currentRow();
    if (row <= NewSessionRow || row >= m_sessionList->count() - 1) {
        return;
    }

    QString sessionName = m_sessionList->item(row)->text().section(QStringLiteral("  —  "), 0, 0);
    auto answer = QMessageBox::question(this, tr("Delete Session"),
        tr("Do you really want to delete \"%1\"?").arg(sessionName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    QString sessionId = m_sessionList->item(row)->data(Qt::UserRole).toString();
    m_sessionStore->removeSession(m_projectRoot, sessionId);
    m_lastExistingSessionId.clear();

    delete m_sessionList->takeItem(row);

    // Select nearest remaining session
    int newRow = qMin(row, m_sessionList->count() - 2); // -2 to avoid manual ID row
    m_sessionList->setCurrentRow(qMax(newRow, NewSessionRow));
}

void SessionSelectionDialog::onContinueClicked()
{
    // Save any unsaved note before accepting
    if (m_detailStack->currentIndex() == ExistingSessionDetailPage) {
        saveCurrentNote();
    }

    int row = m_sessionList->currentRow();
    QString sessionId = m_sessionList->item(row)->data(Qt::UserRole).toString();

    if (row == NewSessionRow) {
        m_result = Result::NewSession;
        m_selectedSessionId.clear();
        m_selectedSessionName = m_nameEdit->text().trimmed();
        if (m_selectedSessionName.isEmpty()) {
            m_selectedSessionName = QDateTime::currentDateTime()
                .toString(QStringLiteral("'Session 'yyyy-MM-dd"));
        }
        m_selectedSessionNote = m_newNoteEdit->toPlainText().trimmed();
    } else if (sessionId == ManualIdSentinel) {
        m_selectedSessionId = m_idEdit->text().trimmed();
        if (m_selectedSessionId.isEmpty()) {
            m_idEdit->setPlaceholderText(tr("Session ID required"));
            return;
        }
        m_result = Result::Resume;
        m_selectedSessionName.clear();
        m_selectedSessionNote.clear();
    } else {
        m_result = Result::Resume;
        m_selectedSessionId = sessionId;
        m_selectedSessionName.clear();
        m_selectedSessionNote.clear();
    }
    m_selectedCwd = m_rootCombo->currentText().trimmed();
    accept();
}

void SessionSelectionDialog::onBrowseClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Working Directory"),
                                                    m_rootCombo->currentText());
    if (!dir.isEmpty())
        m_rootCombo->setCurrentText(dir);
}

void SessionSelectionDialog::onRootChanged(const QString &root)
{
    m_selectedCwd = root;
    m_lastExistingSessionId.clear();

    // Rebuild session list for the new root
    m_sessionList->clear();

    auto *newItem = new QListWidgetItem(tr("+ New Session"), m_sessionList);
    newItem->setData(Qt::UserRole, QString());

    QList<SessionEntry> sessions = m_sessionStore->listSessions(root);
    for (const SessionEntry &entry : sessions) {
        QString label = entry.name + QStringLiteral("  —  ") +
                        entry.timestamp.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        auto *item = new QListWidgetItem(label, m_sessionList);
        item->setData(Qt::UserRole, entry.id);
    }

    auto *manualItem = new QListWidgetItem(tr("↩ Resume by session ID..."), m_sessionList);
    manualItem->setData(Qt::UserRole, ManualIdSentinel);

    m_sessionList->setCurrentRow(NewSessionRow);
}
