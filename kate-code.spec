Name:           kate-code
Version:        1.5.0
Release:        1%{?dist}
Summary:        Claude Code integration for Kate text editor

License:        MIT
URL:            https://github.com/undefinedopcode/kate-code
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.16
BuildRequires:  extra-cmake-modules >= 6.0.0
BuildRequires:  gcc-c++
BuildRequires:  kf6-ktexteditor-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kxmlgui-devel
BuildRequires:  kf6-syntax-highlighting-devel
BuildRequires:  kf6-kpty-devel
BuildRequires:  qt6-webenginewidgets-devel

Recommends:     claude-code-acp

%description
A Kate plugin that provides Claude AI assistant integration via the
Agent Client Protocol (ACP). Features include chat interface, tool
execution with permission controls, and session management.

%prep
%autosetup

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%{_libdir}/qt6/plugins/kf6/ktexteditor/katecode.so
%{_libdir}/qt6/plugins/kf6/ktexteditor/katecode.json
%{_datadir}/kate/plugins/katecode/katecodeui.rc
%{_libexecdir}/kate-mcp-server

%changelog
* Thu Jul 02 2026 Ben <ben@localhost> - 1.5.0-1
- Generate session summaries with a configured ACP agent chosen from a
  provider dropdown, replacing the hard-coded Anthropic-API models; the
  default "Current agent" asks the live session to summarise itself
  before disconnect/quit, behind a cancellable progress dialogue
- Frame injected resume context as a session restore so agents no longer
  re-run the previous session's last task, and have the agent reply with
  a one-sentence overview confirming the restore
- Remove the now-unused Anthropic API key settings and KWallet plumbing
- Fix a review sweep of bugs: summary-vs-cancelled-prompt race, chat
  wedged after a session/prompt error response, broken reconnect after
  an agent crash, single-agent gate leaks and bypass, terminal-wait
  use-after-free, terminal args quoting, UTF-8 corruption on chunked
  agent output, duplicate summaries, and transcript/summary folder-name
  mismatches
- Stop kate-mcp-server dying intermittently ("MCP error -32000:
  Connection closed"): survive interrupted reads, answer MCP pings,
  ignore SIGPIPE and complete partial writes

* Wed Jul 01 2026 Ben <ben@localhost> - 1.4.1-1
- Surface Codex systemError and genuine ACP errors as distinct chat
  messages instead of dropping them or disguising them as normal output
- Make agent output reliably reach the end of the log: scroll the message
  container, clear stale content on a fresh connect, record the assistant
  transcript for agents that end a turn via the prompt response, and log
  JS exceptions from injected calls

* Wed Jul 01 2026 Ben <ben@localhost> - 1.4.0-1
- Fix a crash when closing Kate while an agent session was active
- Support agents in separate Kate processes via a per-process editor DBus
  name; block a second agent within one process with a clear message
- Prefer ACP session/set_config_option for the mode dropdown with a
  session/set_mode fallback and rollback on failure
- Detect prompt capabilities and orient the agent that it runs in Kate
- Queue follow-up prompts instead of creating a second streaming cursor
- Make the input area resizable up to half the output; add a waiting
  indicator and a file-include button
- Add a save-output control and a global command auto-approval allow-list
- Stop the streaming caret from flashing

* Tue Jun 30 2026 Ben <ben@localhost> - 1.3.1-2
- Preserve ordered WebView updates received while the chat page is loading
- Add a control to clear only the displayed chat output

* Tue Jun 30 2026 Ben <ben@localhost> - 1.3.1-1
- Restore useful ACP approval and sandbox mode labels and plain-text chat input
- Fix web UI icons when the Material Symbols font is unavailable
- Document native Codex ACP session KVPs for model, reasoning effort, and mode

* Mon Jun 29 2026 Ben <ben@localhost> - 1.3.0-1
- Add ordered ACP provider descriptions with full configuration editing
- Add per-provider ACP session configuration options for model and related settings
- Preserve Kate and configured external MCP servers for new and resumed sessions
- Improve standard ACP session/load, config option, and mode update compatibility

* Mon Jun 22 2026 Ben <ben@localhost> - 1.2.0-1
- Source resumable sessions from transcripts so abandoned sessions appear
- Add per-provider true ACP session/load resume with context fallback
- Make built-in providers editable and removable (at least one kept)
- Add optional ACP JSON traffic logging to file (flushed per line)
- Rename Summaries config tab to Advanced; add log and resume options

* Wed May 27 2026 Ben <ben@localhost> - 1.1.0-1
- Improve Codex ACP MCP integration and local MCP discovery

* Fri Jan 16 2026 April <apriljayres@gmail.com> - 1.0.0-1
- Initial release
