Name:           kate-code
Version:        1.3.1
Release:        2%{?dist}
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
BuildRequires:  kf6-kwallet-devel
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
