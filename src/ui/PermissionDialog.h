#pragma once

#include "../acp/ACPModels.h"
#include <QDialog>

class QLabel;
class QTextEdit;
class QPushButton;
class QVBoxLayout;

class PermissionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PermissionDialog(const PermissionRequest &request, QWidget *parent = nullptr);
    ~PermissionDialog() override;

    int selectedOptionId() const { return m_selectedOptionId; }

private Q_SLOTS:
    void onOptionClicked();

private:
    PermissionRequest m_request;
    int m_selectedOptionId;

    QLabel *m_titleLabel;
    QTextEdit *m_detailsText;
    QVBoxLayout *m_optionsLayout;
};
