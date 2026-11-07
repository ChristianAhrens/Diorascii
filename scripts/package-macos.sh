#!/usr/bin/env bash
# Builds the universal macOS release and packs it exactly like the GitHub release asset
# (Diorascii-<version>-macOS.dmg). Signing adapts to the machine it runs on:
#
#   * no "Developer ID Application" identity in the keychain (CI, fresh checkout):
#       ad-hoc signature only, so Apple silicon still runs the binary. No notarization.
#   * identity available (your Mac): hardened-runtime signature with secure timestamp, then
#       the disk image is signed too, notarized by Apple's notary service (waiting for the
#       verdict) and the ticket stapled to it, so it also opens offline.
#
# The disk image holds the binary, a link to /usr/local/bin (drag the binary onto it; /usr/bin
# itself is protected by System Integrity Protection) and an installer script for ~/.local/bin,
# because a link cannot point into a home folder whose name is not known in advance.
#
# usage: scripts/package-macos.sh [version] [output-dir]
#   version      defaults to the latest git tag, or "dev"
#   output-dir   defaults to dist/
#
# environment:
#   DIORASCII_SIGN_IDENTITY    certificate name, e.g. "Developer ID Application: Name (TEAMID)".
#                              Default: the first Developer ID Application identity found.
#   DIORASCII_NOTARY_PROFILE   notarytool keychain profile. Default: the shared
#                              APP_NOTARIZATION_CREDENTIALS_PROFILE, created once with:
#                              xcrun notarytool store-credentials <profile> \
#                                  --apple-id <apple id> --team-id <team id>
#   DIORASCII_SKIP_NOTARIZE=1  sign with the real identity but do not notarize
#   DIORASCII_ADHOC=1          force the CI behaviour even when an identity exists
set -euo pipefail
cd "$(dirname "$0")/.."

version="${1:-$(git describe --tags --abbrev=0 2>/dev/null || echo dev)}"
out="${2:-dist}"
profile="${DIORASCII_NOTARY_PROFILE:-APP_NOTARIZATION_CREDENTIALS_PROFILE}"
name="Diorascii-$version-macOS"
build="build/release-macos"
work="$out/.work-$name"
dmgroot="$work/dmg"

[ "$(uname -s)" = Darwin ] || { echo "error: this script runs on macOS" >&2; exit 1; }

identity="${DIORASCII_SIGN_IDENTITY:-}"
if [ -z "$identity" ] && [ -z "${DIORASCII_ADHOC:-}" ]; then
  identity="$(security find-identity -v -p codesigning \
    | sed -n 's/.*"\(Developer ID Application:[^"]*\)".*/\1/p' | head -n 1)"
fi
[ -n "${DIORASCII_ADHOC:-}" ] && identity=""

if [ -n "$identity" ]; then
  echo "== signing with: $identity"
else
  echo "== no Developer ID Application identity (or DIORASCII_ADHOC set): ad-hoc signing, no notarization"
fi

echo "== build $version"
cmake --preset release-macos "-DDIORASCII_VERSION=$version"
cmake --build "$build" --config Release --parallel
ctest --test-dir "$build" -C Release --output-on-failure

echo "== stage payload"
rm -rf "$work" "$out/$name.dmg"
mkdir -p "$dmgroot"
cmake --install "$build" --config Release --prefix "$work/install"
binary="$dmgroot/diorascii"
cp "$work/install/bin/diorascii" "$binary"
cp LICENSE README.md "$dmgroot/"
ln -s /usr/local/bin "$dmgroot/usr-local-bin"
cat > "$dmgroot/Install to local bin.command" <<'SH'
#!/bin/sh
# Copies diorascii next to this file into ~/.local/bin (add that folder to your PATH if needed).
set -e
dir="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HOME/.local/bin"
cp "$dir/diorascii" "$HOME/.local/bin/diorascii"
echo "Installed to $HOME/.local/bin/diorascii"
case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) echo "Add it to your PATH:  export PATH=\"\$HOME/.local/bin:\$PATH\"";; esac
SH
chmod +x "$dmgroot/Install to local bin.command"

if [ -n "$identity" ]; then
  codesign --force --options runtime --timestamp --sign "$identity" "$binary"
else
  codesign --force --sign - "$binary"
fi
codesign --verify --strict --verbose=2 "$binary"
lipo -info "$binary"

echo "== disk image"
hdiutil create -quiet -volname "Diorascii $version" -srcfolder "$dmgroot" -format UDZO -ov "$out/$name.dmg"

if [ -n "$identity" ]; then
  codesign --force --timestamp --sign "$identity" "$out/$name.dmg"
  if [ -z "${DIORASCII_SKIP_NOTARIZE:-}" ]; then
    echo "== notarize (profile $profile)"
    xcrun notarytool submit --keychain-profile "$profile" --wait "$out/$name.dmg"
    xcrun stapler staple "$out/$name.dmg"
    spctl --assess --type open --context context:primary-signature --verbose=2 "$out/$name.dmg"
  fi
fi

rm -rf "$work"
echo "== done: $out/$name.dmg"
