#!/bin/sh
# Point the Homebrew formula at a released tag. The release workflow runs this.
# Usage: scripts/update-tap.sh v1.2.3
set -eu

TAG=$1
TAP_REPO=${TAP_REPO:-git@github.com:stohirov/homebrew-tap.git}

# Tags such as v1.0.0-rc1 are not for everyone, so brew users stay where they are.
if ! echo "$TAG" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "Skipping the formula update for $TAG"
    exit 0
fi

URL="https://github.com/stohirov/mina/archive/refs/tags/$TAG.tar.gz"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

curl -fsSL "$URL" -o "$WORK/source.tar.gz"
SHA256=$(shasum -a 256 "$WORK/source.tar.gz" | cut -d' ' -f1)

git clone -q "$TAP_REPO" "$WORK/tap"
cd "$WORK/tap"
sed -e "s|^  url \".*\"|  url \"$URL\"|" \
    -e "s|^  sha256 \".*\"|  sha256 \"$SHA256\"|" \
    Formula/mina.rb > "$WORK/mina.rb"
cp "$WORK/mina.rb" Formula/mina.rb

if git diff --quiet; then
    echo "The formula is already at $TAG"
    exit 0
fi

git -c user.name="github-actions[bot]" \
    -c user.email="41898282+github-actions[bot]@users.noreply.github.com" \
    commit -q -am "Update mina to $TAG"
git push -q origin main
echo "Updated the formula to $TAG"
