add_definitions(-DTRANSLATION_DOMAIN=\"katecode\")

set(katecode_SRCS
    # Plugin layer
    plugin/KateCodePlugin.cpp
    plugin/KateCodeView.cpp

    # ACP layer
    acp/ACPService.cpp
    acp/ACPSession.cpp

    # UI layer
    ui/ChatWidget.cpp
    ui/ChatWebView.cpp
    ui/ChatInputWidget.cpp
    ui/PermissionDialog.cpp

    # Util layer
    util/KDEColorScheme.cpp
    util/KateThemeConverter.cpp
)

# Qt resources
qt_add_resources(katecode_RESOURCES katecode.qrc)

add_library(katecode MODULE ${katecode_SRCS} ${katecode_RESOURCES})

target_link_libraries(katecode
    KF6::TextEditor
    KF6::I18n
    KF6::CoreAddons
    KF6::XmlGui
    KF6::SyntaxHighlighting
    Qt6::Core
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::WebChannel
)

# Installation
install(TARGETS katecode DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/ktexteditor)
install(FILES katecode.json DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/ktexteditor)
install(FILES katecodeui.rc DESTINATION ${KDE_INSTALL_DATADIR}/kate/plugins/katecode)
