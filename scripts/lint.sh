#!/usr/bin/env bash
# Runs every formatter in check mode, clang-tidy on core and the localization check.
# CI runs this exact script; run it before every commit. Requires: uv, CMake, Ninja, Xcode.
set -euo pipefail

readonly CLANG_FORMAT_VERSION="23.1.3"
readonly CLANG_TIDY_VERSION="22.1.8"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

command -v uvx >/dev/null || { echo "error: uv is required (brew install uv)" >&2; exit 1; }

# Tracked and not-yet-committed files, so the check also covers work in progress.
list_files() {
    git ls-files --cached --others --exclude-standard -- "$@"
}

run_on_files() {
    local files=()
    while IFS= read -r file; do
        [[ -f "$file" ]] && files+=("$file")
    done
    if [[ ${#files[@]} -eq 0 ]]; then
        echo "error: no files matched" >&2
        exit 1
    fi
    "$@" "${files[@]}"
}

echo "==> clang-format ${CLANG_FORMAT_VERSION}"
list_files '*.h' '*.hpp' '*.c' '*.cpp' ':!:*/third_party/*' |
    run_on_files uvx "clang-format@${CLANG_FORMAT_VERSION}" --dry-run --Werror

echo "==> swift-format"
xcrun swift-format lint --strict --recursive --parallel \
    Package.swift macos/Osplay macos/OsplayTests core/tests/swift

echo "==> localization"
python3 scripts/check_localization.py

echo "==> clang-tidy ${CLANG_TIDY_VERSION} (core)"
build_dir="core/build/debug"
if [[ ! -f "${build_dir}/compile_commands.json" ]]; then
    (cd core && cmake --preset debug >/dev/null)
fi
tidy_args=(-p "${build_dir}" --quiet)
if [[ "$(uname -s)" == "Darwin" ]]; then
    # The PyPI clang-tidy does not know where Xcode's SDK lives.
    tidy_args+=("--extra-arg=-isysroot$(xcrun --show-sdk-path)")
fi
list_files 'core/src/*.cpp' | run_on_files uvx "clang-tidy@${CLANG_TIDY_VERSION}" "${tidy_args[@]}"

echo "All checks passed."
