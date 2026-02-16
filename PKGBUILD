# Maintainer: Your Name <your.email@example.com>
pkgname=kate-code
pkgver=1.0.0
pkgrel=1
pkgdesc="Claude Code integration for Kate text editor"
arch=('x86_64')
url="https://github.com/undefinedopcode/kate-code"
license=('MIT')
depends=(
    'ktexteditor'
    'ki18n'
    'kcoreaddons'
    'kxmlgui'
    'syntax-highlighting'
    'kwallet'
    'kpty'
    'qt6-webengine'
)
makedepends=(
    'cmake'
    'extra-cmake-modules'
    'gcc'
)
optdepends=(
    'claude-code-acp: Required for Claude Code functionality'
)
source=("${pkgname}::git+https://github.com/undefinedopcode/kate-code.git")
sha256sums=('SKIP')

build() {
    cmake -B build -S "$pkgname" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build
}

package() {
    DESTDIR="$pkgdir" cmake --install build
}
