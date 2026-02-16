Name:           kate-code
Version:        1.0.0
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
BuildRequires:  kf6-kwallet-devel
BuildRequires:  kf6-kpty-devel
BuildRequires:  qt6-qtwebengine-devel

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
%{_libdir}/libexec/kate-mcp-server

%changelog
* Fri Jan 16 2026 April <apriljayres@gmail.com> - 1.0.0-1
- Initial release
