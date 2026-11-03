#!/bin/sh
# One-time setup, or to replace the key: let the release workflow update the
# Homebrew formula. Creates an SSH key that can write only to the tap repository,
# and stores it as a secret of this repository. No copy of it is kept here.
set -eu

KEY_TITLE="mina release workflow"
DIR=$(mktemp -d)
trap 'rm -rf "$DIR"' EXIT

ssh-keygen -q -t ed25519 -N "" -C "$KEY_TITLE" -f "$DIR/key"
gh repo deploy-key add "$DIR/key.pub" --repo stohirov/homebrew-tap --allow-write --title "$KEY_TITLE"
gh secret set HOMEBREW_TAP_DEPLOY_KEY --repo stohirov/mina < "$DIR/key"

# A key without write access would be refused here.
export GIT_SSH_COMMAND="ssh -i $DIR/key -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=$DIR/known_hosts"
git clone -q git@github.com:stohirov/homebrew-tap.git "$DIR/tap"
git -C "$DIR/tap" push --dry-run -q origin main
echo "The deploy key is set up and can write to the tap."
