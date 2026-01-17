#include "SessionSelectionDialog.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTextEdit>
#include <QVBoxLayout>

SessionSelectionDialog::SessionSelectionDialog(const QString &sessionId,
                                               const QString &summaryContent,
                                               QWidget *parent)
    : QDialog(parent)
    , m_summaryPreview(nullptr)
    , m_result(Result::Cancelled)
{
    setWindowTitle(tr("Session Selection"));
    setMinimumWidth(400);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    // Description
    auto *descLabel = new QLabel(tr("A previous session was found for this project."), this);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // Resume option
    m_resumeRadio = new QRadioButton(tr("Resume previous session"), this);
    m_resumeRadio->setChecked(true);
    layout->addWidget(m_resumeRadio);

    // Session ID hint
    QString shortId = sessionId.left(12);
    if (sessionId.length() > 12) {
        shortId += QStringLiteral("...");
    }
    auto *sessionLabel = new QLabel(QStringLiteral("   Session: %1").arg(shortId), this);
    sessionLabel->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    layout->addWidget(sessionLabel);

    // Summary preview (if available)
    if (!summaryContent.isEmpty()) {
        auto *summaryGroup = new QGroupBox(tr("Session Summary"), this);
        auto *summaryLayout = new QVBoxLayout(summaryGroup);

        m_summaryPreview = new QTextEdit(this);
        m_summaryPreview->setPlainText(summaryContent);
        m_summaryPreview->setReadOnly(true);
        m_summaryPreview->setMaximumHeight(150);
        m_summaryPreview->setStyleSheet(QStringLiteral(
            "QTextEdit { background-color: palette(alternate-base); font-size: small; }"));
        summaryLayout->addWidget(m_summaryPreview);

        layout->addWidget(summaryGroup);

        // Make dialog wider to show summary
        setMinimumWidth(500);
    }

    // New session option
    m_newRadio = new QRadioButton(tr("Start new session"), this);
    layout->addWidget(m_newRadio);

    auto *newSessionLabel = new QLabel(tr("   Previous session will be preserved"), this);
    newSessionLabel->setStyleSheet(QStringLiteral("color: gray; font-size: small;"));
    layout->addWidget(newSessionLabel);

    layout->addStretch();

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
