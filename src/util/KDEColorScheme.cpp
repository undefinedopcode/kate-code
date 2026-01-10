#include "KDEColorScheme.h"

#include <QDebug>
#include <QDir>
#include <QSettings>

KDEColorScheme::KDEColorScheme(QObject *parent)
    : QObject(parent)
{
    loadSystemColors();
}

KDEColorScheme::~KDEColorScheme()
{
}

void KDEColorScheme::loadSystemColors()
{
    QString configPath = QDir::homePath() + QStringLiteral("/.config/kdeglobals");
    QSettings kdeConfig(configPath, QSettings::IniFormat);

    qDebug() << "[KDEColorScheme] Loading colors from:" << configPath;

    // Read from Colors:View section
    kdeConfig.beginGroup(QStringLiteral("Colors:View"));
    m_colors.backgroundNormal = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("BackgroundNormal")), QStringLiteral("49,49,58")));
    m_colors.backgroundAlternate = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("BackgroundAlternate")), QStringLiteral("54,56,62")));
    m_colors.foregroundNormal = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("ForegroundNormal")), QStringLiteral("234,234,234")));
    m_colors.foregroundInactive = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("ForegroundInactive")), QStringLiteral("153,153,153")));
    m_colors.foregroundNegative = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("ForegroundNegative")), QStringLiteral("191,3,3")));
    m_colors.foregroundPositive = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("ForegroundPositive")), QStringLiteral("0,110,40")));
    m_colors.foregroundNeutral = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("ForegroundNeutral")), QStringLiteral("176,128,0")));
    m_colors.foregroundLink = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("ForegroundLink")), QStringLiteral("66,133,244")));
    m_colors.foregroundVisited = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("ForegroundVisited")), QStringLiteral("224,64,251")));
    m_colors.foregroundActive = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("ForegroundActive")), QStringLiteral("255,128,224")));
    m_colors.decorationFocus = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("DecorationFocus")), QStringLiteral("86,87,245")));
    kdeConfig.endGroup();

    // Read selection colors
    kdeConfig.beginGroup(QStringLiteral("Colors:Selection"));
    m_colors.selectionBackground = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("BackgroundNormal")), QStringLiteral("86,87,245")));
    m_colors.selectionForeground = parseKdeColor(
        extractColorString(kdeConfig.value(QStringLiteral("ForegroundNormal")), QStringLiteral("255,255,255")));
    kdeConfig.endGroup();

    qDebug() << "[KDEColorScheme] Loaded colors - background:" << m_colors.backgroundNormal.name()
             << "foreground:" << m_colors.foregroundNormal.name();
}

QString KDEColorScheme::generateCSSVariables() const
{
    return QStringLiteral(
        "--bg-primary: %1; "
        "--bg-secondary: %2; "
        "--fg-primary: %3; "
        "--fg-secondary: %4; "
        "--accent: %5; "
        "--link: %6; "
        "--positive: %7; "
        "--negative: %8; "
        "--selection-bg: %9; "
        "--selection-fg: %10"
    ).arg(
        m_colors.backgroundNormal.name(),
        m_colors.backgroundAlternate.name(),
        m_colors.foregroundNormal.name(),
        m_colors.foregroundInactive.name(),
        m_colors.decorationFocus.name(),
        m_colors.foregroundLink.name(),
        m_colors.foregroundPositive.name(),
        m_colors.foregroundNegative.name(),
        m_colors.selectionBackground.name(),
        m_colors.selectionForeground.name()
    );
}

QString KDEColorScheme::extractColorString(const QVariant &value, const QString &defaultValue)
{
    if (value.metaType() == QMetaType::fromType<QStringList>()) {
        QStringList list = value.toStringList();
        if (list.size() >= 3) {
            return list.join(QStringLiteral(","));
        }
    } else if (value.metaType() == QMetaType::fromType<QString>()) {
        return value.toString();
    }
    return defaultValue;
}

QColor KDEColorScheme::parseKdeColor(const QString &colorString)
{
    QStringList rgb = colorString.split(QStringLiteral(","));
    if (rgb.size() >= 3) {
        return QColor(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt());
    }
    return QColor(32, 32, 32); // Default gray
}
