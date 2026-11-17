#!/usr/bin/env bash
# Builds the user manual (docs/manual/*.md) into static HTML pages with pandoc, using the page
# template pages/manual.template.html. The result is what the project page serves at /manual/.
#
# usage: scripts/build-manual.sh [output-directory]            (default: site/manual)
#   DIORASCII_VERSION=1.3.0   version the download links point to (default: the latest released
#                             section of CHANGELOG.md, see scripts/release-notes.sh --latest)
#   PANDOC=/path/to/pandoc    the pandoc binary (default: the one in the PATH)
# requires: pandoc 2.19 or newer (the CI installs it from apt)
#
# The markdown stays valid on GitHub as it is, and the build adapts it for the site:
#   - "x.y.z" becomes the version, so "Diorascii-x.y.z-Linux-x64.deb" is the real asset name, and a
#     link whose text is such an asset name points straight at that file of the release
#     (the markdown links it to the generic releases/latest page, which works on GitHub as well)
#   - links between chapters (install.md) become install.html
#   - ../../Resources/<image> becomes img/<image>, and the images are copied next to the pages
#   - every relative link and image of the result is checked (except those leaving the manual
#     directory, which only exist in the assembled site); a broken one fails the build
set -euo pipefail
cd "$(dirname "$0")/.."

out="${1:-site/manual}"
pandoc="${PANDOC:-pandoc}"
version="${DIORASCII_VERSION:-$(scripts/release-notes.sh --latest)}"
repo="https://github.com/ChristianAhrens/Diorascii"

command -v "$pandoc" >/dev/null || { echo "error: pandoc not found (set PANDOC or install it)" >&2; exit 1; }

# The chapters in reading order: file|title (the first one is the manual's start page).
chapters=(
  "index.md|Overview"
  "install.md|Installation"
  "usage.md|Using Diorascii"
  "scenes.md|The scenes"
  "faq.md|Troubleshooting"
)

rm -rf "$out"
mkdir -p "$out/img"
cp Resources/*.png "$out/img/"
[ -d docs/manual/img ] && cp -r docs/manual/img/. "$out/img/"

# Chapter title and page name of the chapter at index $1.
title_of() { local e="${chapters[$1]}"; echo "${e#*|}"; }
page_of()  { local e="${chapters[$1]}"; e="${e%%|*}"; echo "${e%.md}.html"; }

for i in "${!chapters[@]}"; do
  src="docs/manual/${chapters[$i]%%|*}"
  page="$(page_of "$i")"
  title="$(title_of "$i")"
  [ -f "$src" ] || { echo "error: $src is missing" >&2; exit 1; }

  # Sidebar, with the current chapter marked.
  nav='  <nav class="side"><a class="brand" href="index.html">Diorascii</a><div class="sub">User manual</div>'
  for j in "${!chapters[@]}"; do
    cur=""; [ "$j" = "$i" ] && cur=' aria-current="page"'
    nav+="<a class=\"chapter\" href=\"$(page_of "$j")\"$cur>$(title_of "$j")</a>"
  done
  nav+="<div class=\"back\"><a href=\"../index.html\">Project page</a><a href=\"$repo/releases/latest\">Latest release</a><a href=\"$repo\">GitHub repository</a><a href=\"../doxygen/index.html\">API documentation</a></div></nav>"

  # Previous / next chapter.
  pager='<div class="pager"><span>'
  [ "$i" -gt 0 ] && pager+="<a href=\"$(page_of $((i-1)))\">&larr; $(title_of $((i-1)))</a>"
  pager+='</span><span>'
  [ "$i" -lt $((${#chapters[@]}-1)) ] && pager+="<a href=\"$(page_of $((i+1)))\">$(title_of $((i+1))) &rarr;</a>"
  pager+='</span></div>'

  "$pandoc" -f gfm -t html5 --standalone --wrap=none --template pages/manual.template.html \
    -V "pagetitle=$title" -V "nav=$nav" -V "pager=$pager" -V "version=$version" "$src" |
  sed -E \
    -e "s#href=\"[^\"]*/releases/latest\"><code>Diorascii-x\.y\.z-([^<]*)</code>#href=\"$repo/releases/download/$version/Diorascii-$version-\1\"><code>Diorascii-x.y.z-\1</code>#g" \
    -e "s#x\.y\.z#$version#g" \
    -e 's|href="([a-z]+)\.md(#[^"]*)?"|href="\1.html\2"|g' \
    -e 's#src="\.\./\.\./Resources/#src="img/#g' \
    > "$out/$page"
done
# Every relative link and image of the pages must exist, or the build fails.
broken=0
for page in "$out"/*.html; do
  while IFS= read -r ref; do
    target="${ref%%#*}"
    [ -z "$target" ] && continue
    case "$target" in ../*) continue ;; esac   # the project page and the API docs: only in the assembled site
    [ -e "$out/$target" ] || { echo "error: $(basename "$page"): broken link or image '$ref'" >&2; broken=1; }
  done < <(grep -oE '(href|src)="[^":]*"' "$page" | sed -E 's/^(href|src)="//; s/"$//' | grep -v '^$')
done
[ "$broken" = 0 ] || exit 1
echo "manual $version: ${#chapters[@]} pages in $out"
