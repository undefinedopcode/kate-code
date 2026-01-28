# Kate-Code Plugin

A Kate text editor plugin that wraps Claude Code using claude-code-acp.

## Build Instructions

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Installation

```bash
cmake --install build --prefix ~/.local
```

Then restart Kate and enable the plugin via:
Settings > Configure Kate > Plugins > Enable "Kate Code"

## Architecture

### Layer Structure
- **Plugin**: KateCodePlugin, KateCodeView - Kate integration
- **ACP**: ACPService, ACPSession - JSON-RPC 2.0 over stdin/stdout to claude-code-acp
- **UI**: ChatWidget, ChatWebView, ChatInputWidget, PermissionDialog
- **Util**: KDEColorScheme - reads ~/.config/kdeglobals

### Key Files
- `src/plugin/KateCodePlugin.{h,cpp}` - Plugin registration via K_PLUGIN_CLASS_WITH_JSON
- `src/plugin/KateCodeView.{h,cpp}` - Creates side panel tool view, provides Kate context
- `src/acp/ACPService.{h,cpp}` - QProcess-based claude-code-acp subprocess management
- `src/acp/ACPSession.{h,cpp}` - Protocol flow and session state management
- `src/acp/ACPModels.h` - Data structures (Message, ToolCall, TodoItem, etc.)
- `src/ui/ChatWidget.{h,cpp}` - Main chat interface with ACP integration
- `src/ui/ChatWebView.{h,cpp}` - QWebEngineView for HTML/CSS/JS rendering
- `src/ui/ChatInputWidget.{h,cpp}` - Multiline text input with send button
- `src/ui/PermissionDialog.{h,cpp}` - Modal dialog for tool approval requests
- `src/config/SettingsStore.{h,cpp}` - QSettings + KWallet storage, ACPProvider management
- `src/config/KateCodeConfigPage.{h,cpp}` - Kate config page with provider table, diff colors, summaries
- `src/util/KDEColorScheme.{h,cpp}` - KDE color extraction from kdeglobals
- `src/web/chat.{html,css,js}` - Web assets for chat interface
- `src/katecode.qrc` - Qt resource file for web assets
- `src/katecode.json` - KPlugin metadata

## Development Progress

### Phase 1: Core Plugin Structure ✓
- [x] CMake build system with KF6/Qt6 dependencies
- [x] Directory structure
- [x] KateCodePlugin with K_PLUGIN_CLASS_WITH_JSON registration
- [x] KateCodeView with side panel tool view (left position)
- [x] Basic ChatWidget placeholder
- [x] Successful compilation

### Phase 2: ACP Integration ✓
- [x] ACPService with QProcess management
- [x] ACPModels data structures
- [x] ACPSession with protocol flow (initialize → session/new → session/prompt)
- [x] ChatWidget integrated with ACP session
- [x] Basic UI for testing (Connect button, chat display, message input)

### Phase 3: Chat Functionality ✓
- [x] ChatWebView with QWebEngineView and HTML/CSS/JS rendering
- [x] ChatInputWidget with multiline input (Enter=send, Shift+Enter=newline)
- [x] Message streaming display with role indicators and timestamps
- [x] Tool call display in chat UI
- [x] Qt resource system for bundling web assets
- [x] JavaScript bridge for updating messages from C++

### Phase 4: Full Features ✓
- [x] KDE color scheme extraction from ~/.config/kdeglobals
- [x] Color injection into WebView via JavaScript
- [x] Permission dialog for tool approvals with option selection
- [x] Error display via QMessageBox
- [x] Permission response handling back to ACP

### Phase 5: Kate Integration (TODO)
- [ ] Context actions
- [ ] File/selection context in prompts

## Lessons Learned

### Build System
- Must include `<KLocalizedString>` for i18n() function
- KF6 components needed: I18n, TextEditor, CoreAddons, XmlGui
- Qt6 components: Core, Widgets, WebEngineWidgets

### Plugin Structure
- Use `createToolView()` with `KTextEditor::MainWindow::Left` for side panel
- K_PLUGIN_CLASS_WITH_JSON macro requires katecode.json in same directory
- Plugin views are children of MainWindow and cleaned up automatically

### ACP Protocol
- JSON-RPC 2.0 over stdin/stdout with newline-delimited JSON
- Flow: `initialize` (protocolVersion: 1) → `session/new` (cwd, mcpServers) → `session/prompt`
- Streaming updates via `session/update` notifications with sessionUpdate types:
  - `agent_message_start`, `agent_message_chunk`, `agent_message_end`
  - `tool_call`, `tool_call_update`
  - `plan` (todos)
- Permission requests via `session/request_permission` with requestId that requires response

### ACP Provider Configuration
- Providers are defined by `ACPProvider` struct: id, description, executable, options, builtin flag
- Two built-in providers (Claude Code, Vibe/Mistral) are hardcoded and cannot be deleted
- Custom providers are stored in QSettings via `beginWriteArray("ACP/customProviders")`
- Active provider selection persisted as `ACP/activeProvider` string id
- Provider selector is a QComboBox in the ChatWidget header (disabled while connected)
- Unavailable executables shown grayed with "(not found)" suffix
- Settings page shows a QTableWidget for managing custom providers (add/edit/remove)
- Old `ACPBackend` enum settings are auto-migrated on first launch

### Web Interface
- QWebEngineView loads HTML from Qt resources (qrc:/katecode/web/chat.html)
- JavaScript functions exposed: `addMessage()`, `updateMessage()`, `finishMessage()`, `addToolCall()`, `updateToolCall()`
- C++ calls JavaScript via `page()->runJavaScript()` with proper string escaping
- CSS uses CSS variables for theming (ready for KDE color injection in Phase 4)
- Responsive layout with message bubbles, tool call display, and streaming indicator
