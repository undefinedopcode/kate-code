#pragma once

#include <QString>
#include <QJsonObject>

namespace KSyntaxHighlighting {
    class Theme;
}

class KateThemeConverter
{
public:
    // Get the current Kate color theme name from Kate's config
    static QString getCurrentKateTheme();

    // Find and load a Kate theme by name
    // Searches user themes (~/.local/share) first, then system themes, then built-in themes
    static QJsonObject loadKateTheme(const QString &themeName);

    // Load a theme from KSyntaxHighlighting library (built-in themes)
    static QJsonObject loadBuiltinTheme(const QString &themeName);

    // Convert KSyntaxHighlighting::Theme to JSON format
    static QJsonObject themeToJson(const KSyntaxHighlighting::Theme &theme);

    // Convert Kate theme JSON to highlight.js CSS
    static QString convertToHighlightJsCSS(const QJsonObject &kateTheme);

    // Get CSS for the current Kate theme
    static QString getCurrentThemeCSS();

private:
    // Map Kate text-style names to highlight.js CSS classes
    static QStringList mapKateStyleToHljs(const QString &kateStyle);

    // Convert Kate color format to CSS
    static QString formatColor(const QString &color);
};
