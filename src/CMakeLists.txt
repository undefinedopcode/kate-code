add_definitions(-DTRANSLATION_DOMAIN=\"katecode\")

set(katecode_SRCS
    # Plugin layer
    plugin/KateCodePlugin.cpp
    plugin/KateCodeView.cpp

    # ACP layer
    acp/ACPService.cpp
    acp/ACPSession.cpp
    acp/TerminalManager.cpp

    # UI layer
    ui/ChatWidget.cpp
    ui/ChatWebView.cpp
    ui/ChatInputWidget.cpp
    ui/PermissionDialog.cpp
    ui/SessionSelectionDialog.cpp

    # Config layer
    config/SettingsStore.cpp
    config/KateCodeConfigPage.cpp

    # MCP layer (DBus service, runs in Kate process)
    mcp/EditorDBusService.cpp

    # Util layer
    util/KDEColorScheme.cpp
    util/KateThemeConverter.cpp
    util/DiffHighlightManager.cpp
    util/EditTracker.cpp
    util/SessionStore.cpp
    util/TranscriptWriter.cpp
    util/SummaryStore.cpp
    util/SummaryGenerator.cpp
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
    KF6::Wallet
    KF6::Pty
    Qt6::Core
    Qt6::DBus
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::WebChannel
    Qt6::Network
)

# Installation
install(TARGETS katecode DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/ktexteditor)
install(FILES katecode.json DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/ktexteditor)
install(FILES katecodeui.rc DESTINATION ${KDE_INSTALL_DATADIR}/kate/plugins/katecode)

# Kate MCP Server executable
add_executable(kate-mcp-server
    mcp/main.cpp
    mcp/MCPServer.cpp
)

target_link_libraries(kate-mcp-server
    Qt6::Core
    Qt6::DBus
)

install(TARGETS kate-mcp-server DESTINATION ${KDE_INSTALL_LIBEXECDIR})

# Tell the plugin where the MCP server is installed
target_compile_definitions(katecode PRIVATE
    KATE_MCP_SERVER_PATH="${KDE_INSTALL_FULL_LIBEXECDIR}/kate-mcp-server"
)
