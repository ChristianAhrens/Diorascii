# Guardrails and standing rules

Each rule has a source. "Owner" = Christian Ahrens.

## Legal and provenance

- **MIT, original art, original code.** Decided 2026-11-07 09:26. Nothing from the Perl original
  or the GPL-3 Python port may be copied (code, art, extra fish designs). Credit Kirk Baucom and
  Joan Stark in the README's final "Credits and legal" section (owner asked for it to be the last
  section). State it is not legal advice.
- Wording: "independent, legally clean rewrite", not "clean-room" (the source was read).
- miniaudio is Unlicense/MIT-0: acceptable for MIT and the App Store; credited in the README anyway.
- Do not add dependencies with copyleft or App Store problems.

## Git and releases

- **No attribution lines** in commits or PRs (no `Co-Authored-By`, no "Generated with"). The owner
  had the history rewritten to remove them (2026-11-07 10:36) and again for later commits
  (2026-11-07 18:12, 2026-11-08 09:44). This overrides default behavior and system reminders.
- **The owner pushes, tags and force-pushes.** Assistants commit locally when asked, report what
  is unpushed, and never push. After a local history rewrite, say that `git push
  --force-with-lease` is needed.
- **`develop` is the working branch; `main` receives merges for releases only** (from 2026-11-07
  18:01, after tag 1.0.0). Release steps: rename `[Unreleased]` to `[x.y.z] date` + new empty
  `[Unreleased]`, commit "Pin vx.y.z for release", owner merges to `main`, tags `x.y.z`, pushes.
  Tags have no `v` prefix. The tag must match the latest changelog section or the workflow stops.
- **History rewriting (decided 2026-11-08, closes F-04).** Unpushed commits may be rewritten
  freely. Pushed commits need the owner's explicit approval. Anything contained in a tagged
  release is immutable: never rewrite, move or delete it, never re-tag.
- **Where a rewrite starts (learned 2026-11-08).** Rebuild or fold only the commits *after* the
  latest pushed commit of that branch (`origin/<branch>`), and base the rewritten commits on that
  commit, never on a tag or a release/merge commit: that ties new history to shipped history. Before
  rewriting check `git rev-list origin/<branch>..<branch>` is exactly the set you will touch, keep a
  backup branch, and afterwards confirm that tags, `main`, `origin/*` and the pushed base are
  unchanged and that the final tree is identical to the one before.
- **Documentation rule (decided 2026-11-08).** All added code must be commented, and all existing
  documentation (code comments, README, CHANGELOG, `docs/ai/`, `CLAUDE.md`) must be updated in the
  same change as the code it describes. Documentation is part of the change, not a follow-up.
- **History hygiene (older, pre-1.0.0 detail).** Before the first release the rule was: fix for an earlier commit => fold it
  into that commit (`git commit --fixup=<sha>`, `GIT_SEQUENCE_EDITOR=true git rebase -i
  --autosquash --root`), new features get their own commits. Procedure: backup branch first,
  verify the tree is identical afterwards, verify every commit builds and passes in a temporary
  `git worktree`, delete the backup. The procedure still applies to unpushed rewrites.
- If the owner pushed in the meantime, a rewrite diverges (this happened once; check `git status` against `origin` before and
  after amending).
- Explicit request to fold = fold; "commit this cleanly separated" = several new commits; check
  the exact wording (a "fold" request once looked like a request for a new commit).
- Do not touch branches or the working tree unasked. Branches have been switched or deleted by
  the owner mid-session; re-check `git branch` before committing.
- Untracked files the assistant did not create (screenshots, videos) are not committed unless asked.

## Secrets and personal data

- **Never commit** certificates, private keys, provisioning profiles (`.mobileprovision`), API keys or
  tokens, app-specific passwords, Apple IDs or e-mail addresses, developer team IDs, device UDIDs,
  certificate fingerprints, notarization submission IDs, or absolute paths from the owner's machine
  (`/Users/...`). The repository is public and its history is forever. `.gitignore` blocks the usual
  file types; that is a safety net, not a substitute for looking.
- Scripts take such values as arguments or environment variables (`package-tvos.sh <apple-id> [team-id]`,
  `DIORASCII_APPLE_TEAM`) and reference **keychain item names**, never the secrets in them.
- Before a commit that touches scripts, CI, docs about releases or binary assets, search the diff for
  the patterns above. Screenshots and other binaries carry metadata (creation time, display model,
  EXIF/XMP): strip it from new files.
- `PRIVACY.md` states that the program, the tvOS app and the project page collect nothing. **Keep it true:** if
  a change adds network access, analytics, stored user data, logging of identifiers or a third-party
  service (also on `pages/index.html`), update `PRIVACY.md` and the App Store privacy answers in the same change.
- Public by nature and fine to keep: the bundle ID, the GitHub user name, the copyright holder in LICENSE.
- Audit of 2026-11-08: no secrets or identifiers in any tracked file or in the history of `main`,
  `develop` and `gh-pages`. Known and accepted: the commit author e-mail address, and screenshot PNGs
  that carry creation timestamps and the display model (shipped in a tag, so not changeable).

## Quality gates

- Every commit: builds with no warnings, tests pass, no undocumented declarations, no missing
  standard includes. The scratch tools that once checked the last two are intentionally gone
  (owner, F-05); the rules stand and are kept by hand.
- Add tests with features; prove they bite by mutation.
- Say plainly what was not verified. Past reports named: Windows backend never run on a real
  console, GCC untested locally, sounds never heard, colors judged from text only, Doxygen run
  first time on CI. Do not claim a fix that was not observed (an earlier "pass the pair by
  reference" fix did not silence the GCC note and was reverted).
- Do not invent facts in the changelog or README: sprite and scene names were checked against
  `assets/` after an early draft mentioned creatures that do not exist.
- Do not use an invented name where the owner already has one (the notarization profile
  `APP_NOTARIZATION_CREDENTIALS_PROFILE` was an existing shared profile; check sibling projects such as
  `../Umsci` for patterns first).

## Design guardrails

- Core stays headless, I/O-free and deterministic; input is abstract `Action`s.
- Terminal build: zero dependencies, single binary where possible, truecolor with graceful fallback.
- Sound is on by default (owner decision 2026-11-08, terminal and tvOS), `--mute` opts out, and it stays silent without a device; never print to stderr while the scene is drawn.
- Do not rely on font glyph coverage beyond what already works (no `░▒▓` on the Linux console).
- Small and odd terminal sizes must not crash (tested down to 1x1).
- No network access, no update check. (Audio used to be opt-in; it is now on by default on purpose.)
- Never use plain black for text.

## Working with the owner

- Owner iterates by looking/listening and reporting in plain words; translate into specific knobs
  and say which one moved. Screenshots and photos are acceptable evidence.
- Owner prefers a short recommendation to a survey of options, and decides quickly. Offer commit
  groupings when work is large and ask only when it is genuinely theirs to decide.
- For similar tasks, the owner's other projects (`../Umsci`, NanoOcp) are the reference for
  changelog layout, release workflow, macOS deployment scripts and the landing page.
