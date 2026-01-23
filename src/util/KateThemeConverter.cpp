#include "KateThemeConverter.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

QString KateThemeConverter::getCurrentKateTheme()
{
    // Read from Kate's config file
    QString kateConfigPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/katerc");
    QSettings kateConfig(kateConfigPath, QSettings::IniFormat);

    // Get the color theme name from [KTextEditor Renderer] section
    kateConfig.beginGroup(QStringLiteral("KTextEditor Renderer"));
    QString themeName = kateConfig.value(QStringLiteral("Color Theme"), QStringLiteral("")).toString();
    kateConfig.endGroup();

    qDebug() << "[KateThemeConverter] Current Kate theme:" << themeName;
    return themeName;
}

QPair<QString, int> KateThemeConverter::getEditorFont()
{
    // Read Kate's config file directly (QSettings has issues with complex font strings)
    QString kateConfigPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/katerc");

    QFile file(kateConfigPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "[KateThemeConverter] Could not open katerc";
        return QPair<QString, int>(QStringLiteral("monospace"), 11);
    }

    QTextStream in(&file);
    QString fontString;
    bool inRendererSection = false;

    // Read line by line to find the font
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();

        if (line == QStringLiteral("[KTextEditor Renderer]")) {
            inRendererSection = true;
            continue;
        }

        if (inRendererSection) {
            // Check if we've left the section
            if (line.startsWith(QLatin1Char('['))) {
                break;
            }

            // Look for Text Font= line
            if (line.startsWith(QStringLiteral("Text Font="))) {
                fontString = line.mid(10); // Skip "Text Font="
                qDebug() << "[KateThemeConverter] Found font string:" << fontString;
                break;
            }
        }
    }

    file.close();

    if (fontString.isEmpty()) {
        qDebug() << "[KateThemeConverter] No editor font configured, using default";
        return QPair<QString, int>(QStringLiteral("monospace"), 11);
    }

    // Parse the Qt font string format: "Family,PointSize,..."
    QStringList parts = fontString.split(QLatin1Char(','));
    if (parts.size() < 2) {
        qDebug() << "[KateThemeConverter] Invalid font string format:" << fontString;
        return QPair<QString, int>(QStringLiteral("monospace"), 11);
    }

    QString family = parts[0];
    bool ok;
    int pointSize = parts[1].toInt(&ok);
    if (!ok || pointSize <= 0) {
        pointSize = 11; // Default size
    }

    qDebug() << "[KateThemeConverter] Editor font:" << family << "size:" << pointSize;
    return QPair<QString, int>(family, pointSize);
}

QJsonObject KateThemeConverter::loadKateTheme(const QString &themeName)
{
    if (themeName.isEmpty()) {
        qWarning() << "[KateThemeConverter] No theme name provided";
        return QJsonObject();
    }

    // Sanitize theme name to filename (replace spaces with hyphens, lowercase)
    QString fileName = themeName.toLower().replace(QLatin1Char(' '), QLatin1Char('-')) + QStringLiteral(".theme");

    // Search locations in order: user themes, then system themes
    QStringList searchPaths;

    // User themes
    QString userThemesDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                            + QStringLiteral("/org.kde.syntax-highlighting/themes");
    searchPaths << userThemesDir;

    // System themes (multiple possible locations)
    QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dataDir : dataDirs) {
        searchPaths << dataDir + QStringLiteral("/org.kde.syntax-highlighting/themes");
    }

    // Try each search path
    for (const QString &searchPath : searchPaths) {
        QString themePath = searchPath + QLatin1Char('/') + fileName;
        QFile themeFile(themePath);

        if (themeFile.exists() && themeFile.open(QIODevice::ReadOnly)) {
            qDebug() << "[KateThemeConverter] Found theme at:" << themePath;

            QByteArray themeData = themeFile.readAll();
            QJsonDocument doc = QJsonDocument::fromJson(themeData);

            if (doc.isObject()) {
                return doc.object();
            } else {
                qWarning() << "[KateThemeConverter] Failed to parse theme JSON:" << themePath;
            }
        }
    }

    // If not found in files, try built-in themes
    qDebug() << "[KateThemeConverter] Theme not found in files, trying built-in themes";
    QJsonObject builtinTheme = loadBuiltinTheme(themeName);
    if (!builtinTheme.isEmpty()) {
        return builtinTheme;
    }

    qWarning() << "[KateThemeConverter] Theme not found:" << themeName << "(" << fileName << ")";
    return QJsonObject();
}

QJsonObject KateThemeConverter::loadBuiltinTheme(const QString &themeName)
{
    KSyntaxHighlighting::Repository repository;

    // Get all themes from the repository
    const auto themes = repository.themes();

    for (const auto &theme : themes) {
        if (theme.name() == themeName) {
            qDebug() << "[KateThemeConverter] Found built-in theme:" << themeName;
            return themeToJson(theme);
        }
    }

    qDebug() << "[KateThemeConverter] Built-in theme not found:" << themeName;
    qDebug() << "[KateThemeConverter] Available built-in themes:" << repository.themes().size();

    return QJsonObject();
}

QJsonObject KateThemeConverter::themeToJson(const KSyntaxHighlighting::Theme &theme)
{
    using namespace KSyntaxHighlighting;

    QJsonObject themeJson;

    // Metadata
    QJsonObject metadata;
    metadata[QStringLiteral("name")] = theme.name();
    themeJson[QStringLiteral("metadata")] = metadata;

    // Editor colors - convert QRgb to hex string (without alpha channel for CSS compatibility)
    auto rgbToHex = [](QRgb rgb) {
        QColor color = QColor::fromRgba(rgb);
        // Use HexRgb format (#RRGGBB) instead of HexArgb (#AARRGGBB) for better CSS compatibility
        return color.name(QColor::HexRgb);
    };

    QJsonObject editorColors;
    editorColors[QStringLiteral("BackgroundColor")] = rgbToHex(theme.editorColor(Theme::BackgroundColor));
    editorColors[QStringLiteral("TextSelection")] = rgbToHex(theme.editorColor(Theme::TextSelection));
    editorColors[QStringLiteral("CurrentLine")] = rgbToHex(theme.editorColor(Theme::CurrentLine));
    editorColors[QStringLiteral("SearchHighlight")] = rgbToHex(theme.editorColor(Theme::SearchHighlight));
    editorColors[QStringLiteral("ReplaceHighlight")] = rgbToHex(theme.editorColor(Theme::ReplaceHighlight));
    editorColors[QStringLiteral("BracketMatching")] = rgbToHex(theme.editorColor(Theme::BracketMatching));
    editorColors[QStringLiteral("TabMarker")] = rgbToHex(theme.editorColor(Theme::TabMarker));
    editorColors[QStringLiteral("IndentationLine")] = rgbToHex(theme.editorColor(Theme::IndentationLine));
    editorColors[QStringLiteral("IconBorder")] = rgbToHex(theme.editorColor(Theme::IconBorder));
    editorColors[QStringLiteral("CodeFolding")] = rgbToHex(theme.editorColor(Theme::CodeFolding));
    editorColors[QStringLiteral("LineNumbers")] = rgbToHex(theme.editorColor(Theme::LineNumbers));
    editorColors[QStringLiteral("CurrentLineNumber")] = rgbToHex(theme.editorColor(Theme::CurrentLineNumber));
    editorColors[QStringLiteral("WordWrapMarker")] = rgbToHex(theme.editorColor(Theme::WordWrapMarker));
    editorColors[QStringLiteral("ModifiedLines")] = rgbToHex(theme.editorColor(Theme::ModifiedLines));
    editorColors[QStringLiteral("SavedLines")] = rgbToHex(theme.editorColor(Theme::SavedLines));
    editorColors[QStringLiteral("Separator")] = rgbToHex(theme.editorColor(Theme::Separator));
    editorColors[QStringLiteral("SpellChecking")] = rgbToHex(theme.editorColor(Theme::SpellChecking));
    themeJson[QStringLiteral("editor-colors")] = editorColors;

    // Text styles - map Theme::TextStyle enum to style names
    QJsonObject textStyles;

    auto addTextStyle = [&](const QString &name, Theme::TextStyle style) {
        QJsonObject styleObj;

        // Text color (always present)
        QRgb textColor = theme.textColor(style);
        styleObj[QStringLiteral("text-color")] = rgbToHex(textColor);

        // Selected text color
        QRgb selectedTextColor = theme.selectedTextColor(style);
        if (selectedTextColor != 0) { // Check if non-zero (valid)
            styleObj[QStringLiteral("selected-text-color")] = rgbToHex(selectedTextColor);
        }

        // Background color
        QRgb bgColor = theme.backgroundColor(style);
        if (bgColor != 0) {
            styleObj[QStringLiteral("background-color")] = rgbToHex(bgColor);
        }

        // Selected background color
        QRgb selectedBgColor = theme.selectedBackgroundColor(style);
        if (selectedBgColor != 0) {
            styleObj[QStringLiteral("selected-background-color")] = rgbToHex(selectedBgColor);
        }

        styleObj[QStringLiteral("bold")] = theme.isBold(style);
        styleObj[QStringLiteral("italic")] = theme.isItalic(style);
        styleObj[QStringLiteral("underline")] = theme.isUnderline(style);
        styleObj[QStringLiteral("strike-through")] = theme.isStrikeThrough(style);

        textStyles[name] = styleObj;
    };

    // Add all text styles
    addTextStyle(QStringLiteral("Normal"), Theme::Normal);
    addTextStyle(QStringLiteral("Keyword"), Theme::Keyword);
    addTextStyle(QStringLiteral("Function"), Theme::Function);
    addTextStyle(QStringLiteral("Variable"), Theme::Variable);
    addTextStyle(QStringLiteral("ControlFlow"), Theme::ControlFlow);
    addTextStyle(QStringLiteral("Operator"), Theme::Operator);
    addTextStyle(QStringLiteral("BuiltIn"), Theme::BuiltIn);
    addTextStyle(QStringLiteral("Extension"), Theme::Extension);
    addTextStyle(QStringLiteral("Preprocessor"), Theme::Preprocessor);
    addTextStyle(QStringLiteral("Attribute"), Theme::Attribute);
    addTextStyle(QStringLiteral("Char"), Theme::Char);
    addTextStyle(QStringLiteral("SpecialChar"), Theme::SpecialChar);
    addTextStyle(QStringLiteral("String"), Theme::String);
    addTextStyle(QStringLiteral("VerbatimString"), Theme::VerbatimString);
    addTextStyle(QStringLiteral("SpecialString"), Theme::SpecialString);
    addTextStyle(QStringLiteral("Import"), Theme::Import);
    addTextStyle(QStringLiteral("DataType"), Theme::DataType);
    addTextStyle(QStringLiteral("DecVal"), Theme::DecVal);
    addTextStyle(QStringLiteral("BaseN"), Theme::BaseN);
    addTextStyle(QStringLiteral("Float"), Theme::Float);
    addTextStyle(QStringLiteral("Constant"), Theme::Constant);
    addTextStyle(QStringLiteral("Comment"), Theme::Comment);
    addTextStyle(QStringLiteral("Documentation"), Theme::Documentation);
    addTextStyle(QStringLiteral("Annotation"), Theme::Annotation);
    addTextStyle(QStringLiteral("CommentVar"), Theme::CommentVar);
    addTextStyle(QStringLiteral("RegionMarker"), Theme::RegionMarker);
    addTextStyle(QStringLiteral("Information"), Theme::Information);
    addTextStyle(QStringLiteral("Warning"), Theme::Warning);
    addTextStyle(QStringLiteral("Alert"), Theme::Alert);
    addTextStyle(QStringLiteral("Error"), Theme::Error);
    addTextStyle(QStringLiteral("Others"), Theme::Others);

    themeJson[QStringLiteral("text-styles")] = textStyles;

    return themeJson;
}

QStringList KateThemeConverter::mapKateStyleToHljs(const QString &kateStyle)
{
    // Map Kate token types to highlight.js CSS classes
    // Kate uses semantic names, highlight.js uses similar but slightly different names

    static const QMap<QString, QStringList> mapping = {
        // Comments
        {QStringLiteral("Comment"), {QStringLiteral(".hljs-comment")}},
        {QStringLiteral("Documentation"), {QStringLiteral(".hljs-comment"), QStringLiteral(".hljs-doc")}},
        {QStringLiteral("CommentVar"), {QStringLiteral(".hljs-doctag")}},

        // Keywords
        {QStringLiteral("Keyword"), {QStringLiteral(".hljs-keyword")}},
        {QStringLiteral("ControlFlow"), {QStringLiteral(".hljs-keyword")}},

        // Types
        {QStringLiteral("DataType"), {QStringLiteral(".hljs-type"), QStringLiteral(".hljs-class .hljs-title")}},
        {QStringLiteral("BuiltIn"), {QStringLiteral(".hljs-built_in")}},

        // Literals
        {QStringLiteral("String"), {QStringLiteral(".hljs-string")}},
        {QStringLiteral("Char"), {QStringLiteral(".hljs-string")}},
        {QStringLiteral("VerbatimString"), {QStringLiteral(".hljs-string")}},
        {QStringLiteral("SpecialString"), {QStringLiteral(".hljs-string")}},
        {QStringLiteral("DecVal"), {QStringLiteral(".hljs-number")}},
        {QStringLiteral("BaseN"), {QStringLiteral(".hljs-number")}},
        {QStringLiteral("Float"), {QStringLiteral(".hljs-number")}},
        {QStringLiteral("Constant"), {QStringLiteral(".hljs-literal")}},

        // Functions
        {QStringLiteral("Function"), {QStringLiteral(".hljs-title.function"), QStringLiteral(".hljs-function .hljs-title")}},

        // Variables/Attributes
        {QStringLiteral("Variable"), {QStringLiteral(".hljs-variable")}},
        {QStringLiteral("Attribute"), {QStringLiteral(".hljs-attr"), QStringLiteral(".hljs-attribute")}},

        // Preprocessor
        {QStringLiteral("Preprocessor"), {QStringLiteral(".hljs-meta")}},
        {QStringLiteral("Import"), {QStringLiteral(".hljs-keyword")}},

        // Operators
        {QStringLiteral("Operator"), {QStringLiteral(".hljs-operator")}},

        // Special
        {QStringLiteral("SpecialChar"), {QStringLiteral(".hljs-char.escape")}},
        {QStringLiteral("RegionMarker"), {QStringLiteral(".hljs-section")}},
        {QStringLiteral("Annotation"), {QStringLiteral(".hljs-meta")}},

        // Errors/Warnings
        {QStringLiteral("Error"), {QStringLiteral(".hljs-deletion")}},
        {QStringLiteral("Warning"), {QStringLiteral(".hljs-emphasis")}},
        {QStringLiteral("Alert"), {QStringLiteral(".hljs-strong")}},

        // Normal/Others
        {QStringLiteral("Normal"), {QStringLiteral(".hljs")}},
        {QStringLiteral("Others"), {QStringLiteral(".hljs-symbol")}}
    };

    return mapping.value(kateStyle, QStringList());
}

QString KateThemeConverter::formatColor(const QString &color)
{
    // Kate colors can be in formats: #rrggbb or #aarrggbb
    // CSS wants #rrggbb or rgba(r,g,b,a)

    if (color.isEmpty() || !color.startsWith(QLatin1Char('#'))) {
        return color;
    }

    if (color.length() == 7) {
        // #rrggbb - already in CSS format
        return color;
    } else if (color.length() == 9) {
        // #aarrggbb - need to convert to rgba()
        QString aa = color.mid(1, 2);
        QString rr = color.mid(3, 2);
        QString gg = color.mid(5, 2);
        QString bb = color.mid(7, 2);

        bool ok;
        int r = rr.toInt(&ok, 16);
        int g = gg.toInt(&ok, 16);
        int b = bb.toInt(&ok, 16);
        int a = aa.toInt(&ok, 16);

        float alpha = a / 255.0f;

        return QStringLiteral("rgba(%1, %2, %3, %4)")
            .arg(r).arg(g).arg(b).arg(alpha, 0, 'f', 3);
    }

    return color;
}

QString KateThemeConverter::convertToHighlightJsCSS(const QJsonObject &kateTheme)
{
    if (kateTheme.isEmpty()) {
        qWarning() << "[KateThemeConverter] Empty theme object";
        return QString();
    }

    QString css;
    QTextStream stream(&css);

    // Don't set background here - it's handled by --code-bg CSS variable
    // Just add a comment
    stream << "/* Generated from Kate theme */\n";

    // Get text styles
    QJsonObject textStyles = kateTheme[QStringLiteral("text-styles")].toObject();

    // Convert each Kate text style to highlight.js classes
    for (auto it = textStyles.constBegin(); it != textStyles.constEnd(); ++it) {
        QString kateName = it.key();
        QJsonObject style = it.value().toObject();

        // Get color and formatting
        QString textColor = formatColor(style[QStringLiteral("text-color")].toString());
        QString backgroundColor = formatColor(style[QStringLiteral("background-color")].toString());
        bool bold = style[QStringLiteral("bold")].toBool(false);
        bool italic = style[QStringLiteral("italic")].toBool(false);
        bool underline = style[QStringLiteral("underline")].toBool(false);

        // Skip if no color defined
        if (textColor.isEmpty()) {
            continue;
        }

        // Get corresponding highlight.js classes
        QStringList hljsClasses = mapKateStyleToHljs(kateName);
        if (hljsClasses.isEmpty()) {
            continue;
        }

        // Generate CSS rule with higher specificity and !important to override bundled themes
        // Target the span elements inside pre code.hljs and also pre.diff (for edit diffs)
        QStringList specificSelectors;
        for (const QString &selector : hljsClasses) {
            specificSelectors << QStringLiteral("pre code.hljs ") + selector;
            specificSelectors << QStringLiteral("pre.diff ") + selector;
        }

        stream << specificSelectors.join(QStringLiteral(", ")) << " {\n";
        stream << "    color: " << textColor << " !important;\n";

        if (!backgroundColor.isEmpty()) {
            stream << "    background-color: " << backgroundColor << " !important;\n";
        }
        if (bold) {
            stream << "    font-weight: bold !important;\n";
        }
        if (italic) {
            stream << "    font-style: italic !important;\n";
        }
        if (underline) {
            stream << "    text-decoration: underline !important;\n";
        }

        stream << "}\n\n";
    }

    qDebug() << "[KateThemeConverter] Generated CSS length:" << css.length();
    qDebug() << "[KateThemeConverter] CSS preview (first 500 chars):\n" << css.left(500);
    return css;
}

QString KateThemeConverter::getCurrentThemeCSS()
{
    QString themeName = getCurrentKateTheme();
    if (themeName.isEmpty()) {
        qWarning() << "[KateThemeConverter] No theme configured";
        return QString();
    }

    QJsonObject themeJson = loadKateTheme(themeName);
    if (themeJson.isEmpty()) {
        qWarning() << "[KateThemeConverter] Failed to load theme:" << themeName;
        return QString();
    }

    return convertToHighlightJsCSS(themeJson);
}

bool KateThemeConverter::isLightBackground()
{
    // Try to get background color from Kate theme
    QString themeName = getCurrentKateTheme();
    QJsonObject themeJson = loadKateTheme(themeName);

    if (!themeJson.isEmpty()) {
        QJsonObject editorColors = themeJson[QStringLiteral("editor-colors")].toObject();
        QString kateCodeBg = editorColors[QStringLiteral("BackgroundColor")].toString();

        if (!kateCodeBg.isEmpty()) {
            QColor bgColor(kateCodeBg);
            if (bgColor.isValid()) {
                // Use relative luminance formula: 0.299*R + 0.587*G + 0.114*B
                // Values > 128 are considered "light"
                int luminance = (bgColor.red() * 299 + bgColor.green() * 587 + bgColor.blue() * 114) / 1000;
                bool isLight = (luminance > 128);
                qDebug() << "[KateThemeConverter] isLightBackground - Theme:" << themeName
                         << "bg:" << kateCodeBg << "luminance:" << luminance << "isLight:" << isLight;
                return isLight;
            }
        }
    }

    // Fallback: assume dark background (safer default for code editors)
    qDebug() << "[KateThemeConverter] isLightBackground - Could not determine from Kate theme, defaulting to dark";
    return false;
}
