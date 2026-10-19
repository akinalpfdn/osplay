#!/usr/bin/env bash
# Views take every visual value from Theme (DEVPLAN: theming is centralized). Flags literal
# spacing, sizes, fonts and colours in SwiftUI view code; Theme.swift itself is exempt.
set -euo pipefail
cd "$(dirname "$0")/.."

files=$(find macos/Osplay/Features macos/Osplay/App -name '*.swift' | sort)
pattern='\.(padding|frame|spacing|cornerRadius|offset)\([^)]*[^a-zA-Z_.][0-9]+(\.[0-9]+)?\b|\.font\(\.(system|title|body|caption|headline|subheadline|largeTitle|footnote)|Color\.(red|green|blue|orange|yellow|gray|black|white|pink|purple)\b|Color\(red:|spacing: [0-9]'
violations=$(grep -nE "$pattern" $files || true)
if [[ -n "$violations" ]]; then
    echo "Literal visual values in views (use Theme tokens):"
    echo "$violations"
    exit 1
fi
