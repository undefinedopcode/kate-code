#include "SessionSelectionDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

SessionSelectionDialog::SessionSelectionDialog(const QString &sessionId, QWidget *parent)
    : QDialog(parent)
    , m_result(Result::Cancelled)
{
    setWindowTitle(tr("Session Selection"));
    setMinimumWidth(350);

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
