#!/bin/bash
# Build .deb, .rpm, and .pkg.tar.zst packages using Docker
# Usage: ./build-packages.sh [deb|rpm|arch|all]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

mkdir -p dist

build_deb() {                                                                                                                                                                                                                         
    echo "=== Building .deb package ==="                  
    docker build -f Dockerfile.deb -t kate-code-deb .
    docker create --name deb-out kate-code-deb
    docker cp deb-out:/dist/. dist/
    docker rm deb-out
    echo "=== .deb package built in dist/ ==="
}

build_rpm() {
    echo "=== Building .rpm package ==="
    docker build -f Dockerfile.rpm -t kate-code-rpm .
    docker create --name rpm-out kate-code-rpm
    docker cp rpm-out:/dist/. dist/
    docker rm rpm-out
    echo "=== .rpm package built in dist/ ==="
}

build_arch() {
    echo "=== Building .pkg.tar.zst package (Arch) ==="
    docker build -f Dockerfile.arch -t kate-code-arch .
    docker create --name arch-out kate-code-arch
    docker cp arch-out:/dist/. dist/
    docker rm arch-out
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
