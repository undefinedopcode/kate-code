#include "SessionSelectionDialog.h"
#include "../util/SummaryStore.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTextEdit>
#include <QVBoxLayout>

SessionSelectionDialog::SessionSelectionDialog(const QString &projectRoot,
                                               SummaryStore *summaryStore,
                                               QWidget *parent)
    : QDialog(parent)
    , m_projectRoot(projectRoot)
    , m_summaryStore(summaryStore)
    , m_sessionCombo(nullptr)
    , m_summaryPreview(nullptr)
    , m_result(Result::Cancelled)
{
    setWindowTitle(tr("Session Selection"));
    setMinimumSize(500, 400);
    resize(600, 500);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    // Description
    auto *descLabel = new QLabel(tr("Previous sessions found for this project."), this);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // Resume option
    m_resumeRadio = new QRadioButton(tr("Resume session:"), this);
    m_resumeRadio->setChecked(true);
    layout->addWidget(m_resumeRadio);

    // Session dropdown
    m_sessionCombo = new QComboBox(this);
    m_sessionCombo->setMinimumWidth(300);

    // Load sessions (up to 10, most recent first)
    QStringList sessionIds = m_summaryStore->listSessionSummaries(projectRoot);
    int count = qMin(sessionIds.size(), 10);
    for (int i = 0; i < count; ++i) {
        const QString &sessionId = sessionIds.at(i);
        QString summaryPath = m_summaryStore->summaryPath(projectRoot, sessionId);
        QFileInfo fileInfo(summaryPath);
        QString displayText = fileInfo.lastModified().toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        QString shortId = sessionId.left(12);
        if (sessionId.length() > 12) {
            shortId += QStringLiteral("...");
        }
        displayText += QStringLiteral(" - ") + shortId;
        m_sessionCombo->addItem(displayText, sessionId);
    }

    layout->addWidget(m_sessionCombo);

    // Summary preview
    auto *summaryGroup = new QGroupBox(tr("Session Summary"), this);
    auto *summaryLayout = new QVBoxLayout(summaryGroup);

    m_summaryPreview = new QTextEdit(this);
    m_summaryPreview->setReadOnly(true);
    m_summaryPreview->setStyleSheet(QStringLiteral(
        "QTextEdit { background-color: palette(alternate-base); font-size: small; }"));
    summaryLayout->addWidget(m_summaryPreview);

    layout->addWidget(summaryGroup, 1);  // stretch factor 1 to expand

    // Load first session's summary
    if (m_sessionCombo->count() > 0) {
        onSessionChanged(0);
    }

    connect(m_sessionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SessionSelectionDialog::onSessionChanged);

    // New session option
    m_newRadio = new QRadioButton(tr("Start new session"), this);
    layout->addWidget(m_newRadio);

    auto *newSessionLabel = new QLabel(tr("   Previous sessions will be preserved"), this);
    newSessionLabel->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    layout->addWidget(newSessionLabel);

    // Buttons
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *continueButton = buttonBox->addButton(tr("Continue"), QDialogButtonBox::AcceptRole);
    continueButton->setDefault(true);

    connect(continueButton, &QPushButton::clicked, this, &SessionSelectionDialog::onContinueClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addWidget(buttonBox);
}

void SessionSelectionDialog::onContinueClicked()
{
    if (m_resumeRadio->isChecked()) {
        m_result = Result::Resume;
    } else {
        m_result = Result::NewSession;
    }
    accept();
}

void SessionSelectionDialog::onSessionChanged(int index)
{
    if (index < 0 || !m_summaryPreview) {
        return;
    }

    QString sessionId = m_sessionCombo->itemData(index).toString();
    QString summary = m_summaryStore->loadSummary(m_projectRoot, sessionId);
    m_summaryPreview->setPlainText(summary);
}

QString SessionSelectionDialog::selectedSessionId() const
{
    if (!m_sessionCombo || m_sessionCombo->count() == 0) {
        return QString();
    }
    return m_sessionCombo->currentData().toString();
}
