#!/bin/bash
# Build .deb and .rpm packages using Docker
# Usage: ./build-packages.sh [deb|rpm|all]

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

case "${1:-all}" in
    deb)
        build_deb
        ;;
    rpm)
        build_rpm
        ;;
    all)
        build_deb
        build_rpm
        ;;
    *)
        echo "Usage: $0 [deb|rpm|all]"
        exit 1
        ;;
esac

echo ""
echo "Packages built:"
ls -la dist/
