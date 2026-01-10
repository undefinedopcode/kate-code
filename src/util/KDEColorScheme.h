#pragma once

#include <QColor>
#include <QObject>
#include <QString>

class KDEColorScheme : public QObject
{
    Q_OBJECT

public:
    explicit KDEColorScheme(QObject *parent = nullptr);
    ~KDEColorScheme() override;

    struct Colors {
        QColor backgroundNormal;
        QColor backgroundAlternate;
        QColor foregroundNormal;
        QColor foregroundInactive;
        QColor foregroundNegative;
        QColor foregroundPositive;
        QColor foregroundNeutral;
        QColor foregroundLink;
        QColor foregroundVisited;
        QColor foregroundActive;
        QColor decorationFocus;
        QColor selectionBackground;
        QColor selectionForeground;
    };

    void loadSystemColors();
    Colors colors() const { return m_colors; }

    QString generateCSSVariables() const;

private:
    QColor parseKdeColor(const QString &colorString);
    QString extractColorString(const QVariant &value, const QString &defaultValue);

    Colors m_colors;
};
