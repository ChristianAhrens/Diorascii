#!/usr/bin/env bash
# Builds the universal macOS release and packs it exactly like the GitHub release asset
# (Diorascii-<version>-macOS.pkg), an installer that puts `diorascii` into /usr/local/bin, which is
# in the default PATH of every shell, and its license and readme into /usr/local/share/doc/diorascii.
# Installing a newer version over an older one replaces it; to uninstall:
#   sudo rm /usr/local/bin/diorascii && sudo rm -r /usr/local/share/doc/diorascii \
#     && sudo pkgutil --forget com.ChristianAhrens.Diorascii
# Signing adapts to the machine it runs on:
#
#   * no "Developer ID Application" identity in the keychain (CI, fresh checkout):
#       ad-hoc signature on the binary and an unsigned package, so Apple silicon still runs the
#       binary. No notarization; Gatekeeper asks for confirmation (right-click > Open).
#   * identities available (your Mac): the binary gets a hardened-runtime signature with secure
#       timestamp ("Developer ID Application"), the package is signed with the "Developer ID
#       Installer" identity, notarized by Apple's notary service (waiting for the verdict) and the
#       ticket stapled to it, so it also opens offline.
#
# usage: scripts/package-macos.sh [version] [output-dir]
#   version      the release version (the tag), compiled into the binary. Without it the
#                project version of CHANGELOG.md is used for the package, and the binary reports
#                "<version>-dev+<commit>" unless built from that release's tagged commit.
#   output-dir   defaults to dist/
#
# environment:
#   DIORASCII_SIGN_IDENTITY       certificate name, e.g. "Developer ID Application: Name (TEAMID)".
#                                 Default: the first Developer ID Application identity found.
#   DIORASCII_INSTALLER_IDENTITY  e.g. "Developer ID Installer: Name (TEAMID)", the second
#                                 certificate type Apple issues, needed to sign the package.
#                                 Default: the first Developer ID Installer identity found.
#   DIORASCII_NOTARY_PROFILE      notarytool keychain profile. Default: the shared
#                                 APP_NOTARIZATION_CREDENTIALS_PROFILE, created once with:
#                                 xcrun notarytool store-credentials <profile> \
#                                     --apple-id <apple id> --team-id <team id>
#   DIORASCII_SKIP_NOTARIZE=1     sign with the real identities but do not notarize
#   DIORASCII_ADHOC=1             force the CI behaviour even when identities exist
set -euo pipefail
cd "$(dirname "$0")/.."

# The project version is the latest released section of CHANGELOG.md (not the latest git tag, which
# need not be reachable from the branch this runs on).
explicit_version="${1:-}"
version="${explicit_version:-$(scripts/release-notes.sh --latest)}"
out="${2:-dist}"
profile="${DIORASCII_NOTARY_PROFILE:-APP_NOTARIZATION_CREDENTIALS_PROFILE}"
name="Diorascii-$version-macOS"
# Its own build directory: the version passed below stays in the CMake cache, which would make
# later scripts/build.sh builds in a shared directory report the release version.
build="build/package-macos"
work="$out/.work-$name"

[ "$(uname -s)" = Darwin ] || { echo "error: this script runs on macOS" >&2; exit 1; }

identity="${DIORASCII_SIGN_IDENTITY:-}"
installer_identity="${DIORASCII_INSTALLER_IDENTITY:-}"
if [ -z "${DIORASCII_ADHOC:-}" ]; then
  # Installer certificates are not listed under the code-signing policy, so ask without one.
  [ -n "$identity" ] || identity="$(security find-identity -v -p codesigning \
    | sed -n 's/.*"\(Developer ID Application:[^"]*\)".*/\1/p' | head -n 1)"
  [ -n "$installer_identity" ] || installer_identity="$(security find-identity -v \
    | sed -n 's/.*"\(Developer ID Installer:[^"]*\)".*/\1/p' | head -n 1)"
else
  identity=""
  installer_identity=""
fi

if [ -n "$identity" ] && [ -z "$installer_identity" ]; then
  echo "error: found a Developer ID Application identity but no Developer ID Installer one (needed to sign the .pkg); create it in the developer portal or set DIORASCII_ADHOC=1" >&2
  exit 1
fi
if [ -n "$identity" ]; then
  echo "== signing with: $identity / $installer_identity"
else
  echo "== no Developer ID identities (or DIORASCII_ADHOC set): ad-hoc signing, unsigned package, no notarization"
fi

echo "== build $version"
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 "-DDIORASCII_VERSION=$explicit_version"
cmake --build "$build" --config Release --parallel
ctest --test-dir "$build" -C Release --output-on-failure

echo "== stage payload"
rm -rf "$work" "$out/$name.pkg"
mkdir -p "$out"
# The install tree is the package payload as it lands below /usr/local (bin/, share/doc/diorascii/).
root="$work/root"
cmake --install "$build" --config Release --prefix "$root"
binary="$root/bin/diorascii"

if [ -n "$identity" ]; then
  codesign --force --options runtime --timestamp --sign "$identity" "$binary"
else
  codesign --force --sign - "$binary"
fi
codesign --verify --strict --verbose=2 "$binary"
lipo -info "$binary"

echo "== installer package"
pkgbuild --quiet --root "$root" --identifier com.ChristianAhrens.Diorascii --version "$version" \
  --install-location /usr/local --ownership recommended "$work/component.pkg"
if [ -n "$installer_identity" ]; then
  productbuild --package "$work/component.pkg" --sign "$installer_identity" --timestamp "$out/$name.pkg"
else
  productbuild --package "$work/component.pkg" "$out/$name.pkg"
fi

if [ -n "$installer_identity" ]; then
  pkgutil --check-signature "$out/$name.pkg"
  if [ -z "${DIORASCII_SKIP_NOTARIZE:-}" ]; then
    echo "== notarize (profile $profile)"
    xcrun notarytool submit --keychain-profile "$profile" --wait "$out/$name.pkg"
    xcrun stapler staple "$out/$name.pkg"
    spctl --assess --type install --verbose=2 "$out/$name.pkg"
  fi
fi

rm -rf "$work"
echo "== done: $out/$name.pkg"
