#include "PermissionDialog.h"

#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

PermissionDialog::PermissionDialog(const PermissionRequest &request, QWidget *parent)
    : QDialog(parent)
    , m_request(request)
    , m_selectedOptionId(-1)
{
    setWindowTitle(QStringLiteral("Permission Required"));
    setMinimumWidth(500);

    auto *layout = new QVBoxLayout(this);

    // Title
    m_titleLabel = new QLabel(this);
    m_titleLabel->setText(QStringLiteral("Tool: <b>%1</b>").arg(m_request.toolName));
    m_titleLabel->setWordWrap(true);
    layout->addWidget(m_titleLabel);

    // Tool input details
    m_detailsText = new QTextEdit(this);
    m_detailsText->setReadOnly(true);
    m_detailsText->setMaximumHeight(150);
    QByteArray inputJsonBytes = QJsonDocument(m_request.input).toJson(QJsonDocument::Indented);
    QString inputJson = QString::fromUtf8(inputJsonBytes);
    m_detailsText->setPlainText(inputJson);
    layout->addWidget(m_detailsText);

    // Options
    layout->addWidget(new QLabel(QStringLiteral("Choose an action:"), this));

    m_optionsLayout = new QVBoxLayout();
    m_optionsLayout->setSpacing(8);

    for (const QJsonObject &option : m_request.options) {
        QString optionId = option[QStringLiteral("id")].toString();
        QString label = option[QStringLiteral("label")].toString();
        QString description = option[QStringLiteral("description")].toString();

        auto *optionWidget = new QWidget(this);
        auto *optionLayout = new QVBoxLayout(optionWidget);
        optionLayout->setContentsMargins(8, 8, 8, 8);
        optionWidget->setStyleSheet(QStringLiteral(
            "QWidget { border: 1px solid palette(mid); border-radius: 4px; }"
            "QWidget:hover { background-color: palette(midlight); }"));

        auto *labelWidget = new QLabel(QStringLiteral("<b>%1</b>").arg(label), optionWidget);
        auto *descWidget = new QLabel(description, optionWidget);
        descWidget->setWordWrap(true);
        descWidget->setStyleSheet(QStringLiteral("color: palette(text); font-size: 11px;"));

        optionLayout->addWidget(labelWidget);
        optionLayout->addWidget(descWidget);

        auto *button = new QPushButton(QStringLiteral("Select"), optionWidget);
        button->setProperty("optionId", optionId);
        connect(button, &QPushButton::clicked, this, &PermissionDialog::onOptionClicked);
        optionLayout->addWidget(button);

        m_optionsLayout->addWidget(optionWidget);
    }

    layout->addLayout(m_optionsLayout);
    layout->addStretch();
}

PermissionDialog::~PermissionDialog()
{
}

void PermissionDialog::onOptionClicked()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (!button) {
        return;
    }

    QString optionId = button->property("optionId").toString();

    // Find the option index
    for (int i = 0; i < m_request.options.size(); ++i) {
        if (m_request.options[i][QStringLiteral("id")].toString() == optionId) {
            m_selectedOptionId = i;
            accept();
            return;
        }
    }
}
