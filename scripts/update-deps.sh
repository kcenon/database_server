#!/usr/bin/env bash
# Update dependency versions in deps/kcenon-versions.json
#
# Usage:
#   ./scripts/update-deps.sh                    # Update all deps from sibling directories
#   ./scripts/update-deps.sh --check            # Show current vs latest without updating
#   ./scripts/update-deps.sh --dep thread_system # Update only thread_system
#
# Prerequisites:
#   - jq installed
#   - Dependencies cloned as sibling directories (../common_system, etc.)
#     OR use --remote to fetch latest from GitHub

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSIONS_FILE="$PROJECT_ROOT/deps/kcenon-versions.json"

DEPS=(common_system thread_system logger_system monitoring_system database_system network_system container_system)

CHECK_ONLY=false
SINGLE_DEP=""
USE_REMOTE=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check) CHECK_ONLY=true; shift ;;
        --dep) SINGLE_DEP="$2"; shift 2 ;;
        --remote) USE_REMOTE=true; shift ;;
        -h|--help)
            echo "Usage: $0 [--check] [--dep <name>] [--remote]"
            echo ""
            echo "Options:"
            echo "  --check   Show current vs latest without updating"
            echo "  --dep     Update only the specified dependency"
            echo "  --remote  Fetch latest SHA from GitHub (requires gh CLI)"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if ! command -v jq &>/dev/null; then
    echo "Error: jq is required. Install with: brew install jq / apt install jq"
    exit 1
fi

if [ ! -f "$VERSIONS_FILE" ]; then
    echo "Error: $VERSIONS_FILE not found"
    exit 1
fi

get_local_sha() {
    local dep="$1"
    local dep_dir="$PROJECT_ROOT/../$dep"
    if [ -d "$dep_dir/.git" ]; then
        git -C "$dep_dir" rev-parse HEAD
    else
        echo ""
    fi
}

get_remote_sha() {
    local dep="$1"
    if command -v gh &>/dev/null; then
        gh api "repos/kcenon/$dep/commits/main" --jq '.sha' 2>/dev/null || echo ""
    else
        echo ""
    fi
}

get_pinned_sha() {
    local dep="$1"
    jq -r ".\"$dep\".commit" "$VERSIONS_FILE"
}

TODAY=$(date +%Y-%m-%d)
UPDATED=0

for dep in "${DEPS[@]}"; do
    if [ -n "$SINGLE_DEP" ] && [ "$dep" != "$SINGLE_DEP" ]; then
        continue
    fi

    pinned=$(get_pinned_sha "$dep")

    if [ "$USE_REMOTE" = true ]; then
        latest=$(get_remote_sha "$dep")
        source_label="remote"
    else
        latest=$(get_local_sha "$dep")
        source_label="local"
    fi

    if [ -z "$latest" ]; then
        echo "  SKIP  $dep — not found ($source_label)"
        continue
    fi

    short_pinned="${pinned:0:10}"
    short_latest="${latest:0:10}"

    if [ "$pinned" = "$latest" ]; then
        echo "  OK    $dep — $short_pinned (up to date)"
    elif [ "$CHECK_ONLY" = true ]; then
        echo "  STALE $dep — pinned: $short_pinned, $source_label: $short_latest"
    else
        # Update the JSON file
        tmp=$(mktemp)
        jq --arg dep "$dep" --arg sha "$latest" --arg date "$TODAY" \
            '.[$dep].commit = $sha | .[$dep].verified_date = $date' \
            "$VERSIONS_FILE" > "$tmp"
        mv "$tmp" "$VERSIONS_FILE"
        echo "  UPDATE $dep — $short_pinned -> $short_latest"
        UPDATED=$((UPDATED + 1))
    fi
done

if [ "$CHECK_ONLY" = true ]; then
    echo ""
    echo "Run without --check to apply updates."
elif [ "$UPDATED" -gt 0 ]; then
    # Update the top-level _updated field
    tmp=$(mktemp)
    jq --arg date "$TODAY" '._updated = $date' "$VERSIONS_FILE" > "$tmp"
    mv "$tmp" "$VERSIONS_FILE"
    echo ""
    echo "Updated $UPDATED dependencies. Review with: git diff deps/kcenon-versions.json"
else
    echo ""
    echo "All dependencies are up to date."
fi
