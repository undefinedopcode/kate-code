#!/bin/bash
# Build .deb, .rpm, and .pkg.tar.zst packages using Docker
# Usage: ./build-packages.sh [deb|rpm|arch|all]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

mkdir -p dist

build_deb() {
    echo "=== Building .deb package ==="
    docker build -f Dockerfile.deb -t kate-code-deb .
    docker run --rm -v "$(pwd)/dist:/output" kate-code-deb
    echo "=== .deb package built in dist/ ==="
}

build_rpm() {
    echo "=== Building .rpm package ==="
    docker build -f Dockerfile.rpm -t kate-code-rpm .
    docker run --rm -v "$(pwd)/dist:/output" kate-code-rpm
    echo "=== .rpm package built in dist/ ==="
}

build_arch() {
    echo "=== Building .pkg.tar.zst package (Arch) ==="
    docker build -f Dockerfile.arch -t kate-code-arch .
    docker run --rm -v "$(pwd)/dist:/output" kate-code-arch
    echo "=== .pkg.tar.zst package built in dist/ ==="
}

case "${1:-all}" in
    deb)
        build_deb
        ;;
    rpm)
        build_rpm
        ;;
    arch)
        build_arch
        ;;
    all)
        build_deb
        build_rpm
        build_arch
        ;;
    *)
        echo "Usage: $0 [deb|rpm|arch|all]"
        exit 1
        ;;
esac

echo ""
echo "Packages built:"
ls -la dist/
