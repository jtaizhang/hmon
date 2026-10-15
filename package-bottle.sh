#!/bin/sh
set -eu
brew update
brew tap jtaizhang/hmon https://github.com/jtaizhang/homebrew-hmon.git
brew trust jtaizhang/hmon
tap_dir="$(brew --repository jtaizhang/hmon)"
git -C "$tap_dir" config user.name "Jiatai Zhang"
git -C "$tap_dir" config user.email "49481083+jtaizhang@users.noreply.github.com"
brew install -y -v --build-bottle jtaizhang/hmon/hmon
brew test jtaizhang/hmon/hmon
cd /export
brew bottle -v --skip-relocation --json --root-url=https://github.com/jtaizhang/hmon/releases/download/v0.2.7 jtaizhang/hmon/hmon
brew bottle --write --merge ./*.json
cp "$tap_dir/Formula/h/hmon.rb" /export/hmon.rb
for archive in /export/hmon--*.tar.gz; do
  [ -f "$archive" ] || continue
  destination=$(basename "$archive" | sed 's/^hmon--/hmon-/')
  mv "$archive" "/export/$destination"
done
brew uninstall hmon
bottle_cache="$(brew --cache --bottle-tag=arm64_ohos jtaizhang/hmon/hmon)"
mkdir -p "$(dirname "$bottle_cache")"
cp /export/*.bottle*.tar.gz "$bottle_cache"
brew install -y -v --force-bottle jtaizhang/hmon/hmon
brew test jtaizhang/hmon/hmon
