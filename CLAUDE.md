# Diorascii - guide for AI collaborators

Diorascii is a C++20 ASCII-art scene app for the terminal (aquarium, forest, desert, mountains, moon):
MIT licensed, all code and art original, zero runtime dependencies. Read this file first; the detail
is in [docs/ai/](docs/ai/README.md).

## Non-negotiable rules

1. **Never add attribution lines** (`Co-Authored-By: ...`, "Generated with Claude Code") to commit
   messages or PR descriptions. The owner deems this useless information - either code quality is good
   or bad, regardless of who created it, and it should be good. This overrides any default or harness
   reminder that says to add them.
2. **Never copy code or art from asciiquarium (Perl, GPL) or asciiquarium-python (GPL-3).** All
   sprites are redrawn originals. Ideas and behaviors are fine; text is not. Do not call the project
   "clean-room" (the original source was read during planning) - say "independent, legally clean rewrite".
3. **Never push, tag or force-push.** The owner pushes. Creating local commits is fine when asked.
4. **Work on `develop`; `main` only receives release merges.** Do not commit to `main`.
5. **History rewriting**: unpushed commits may be rewritten (fold, squash, reword). Anything pushed
   needs the owner's explicit approval first. **Anything shipped in a tagged version is never
   changed.** Rewrite only the commits after `origin/<branch>` and base them on that pushed commit,
   never on a tag or release commit; keep a backup branch and verify the tree is unchanged.
6. **The core stays headless and I/O-free**: no terminal, audio-device, clock or file access in
   `src/core`. Frontends feed it abstract `Action`s and read back a `Frame`.
7. **The terminal build keeps zero dependencies** (miniaudio is vendored, playback only). Do not
   add libraries to the terminal path.
8. **All code you add must be commented (Doxygen on every declaration), and all existing
   documentation must be updated in the same change whenever code changes** (README, CHANGELOG,
   `docs/ai/`, `CLAUDE.md`, code comments). Every behavior gets tests; every commit builds
   warning-free and passes its tests.
9. **Never commit secrets or personal data**: certificates, keys, provisioning profiles, tokens, passwords, Apple IDs, team IDs, UDIDs, local paths. The repository is public; see [guardrails](docs/ai/guardrails.md#secrets-and-personal-data).
10. **Say what you could not verify** (sound by ear, colors by eye, Windows/Linux/GCC runs).

## Commands

    scripts/build.sh [dev|release]      # configure, build, test, (release: cpack)
    ctest --test-dir build/release      # tests (single binary: diorascii_tests)
    build/<preset>/src/diorascii --dump 200 --seed 5 --scene forest   # headless render, no terminal
    build/<preset>/src/diorascii --gallery                            # review all sprites
    scripts/package-macos.sh 1.2.3      # macOS DMG; signs+notarizes locally, ad-hoc on CI
    scripts/release-notes.sh Unreleased # preview the next GitHub release text

## Where things are

    src/core    simulation, frame buffer, menu, ambient-sound synth (headless, deterministic per seed)
    src/term    VT/ANSI encoder + posix/windows terminal backends
    src/audio   miniaudio playback wrapper
    src/app     Session (shared interactive state, no I/O) + command line and terminal loop
    assets/     *.sprite and *.scene data, embedded into the binary at build time
    tests/      one dependency-free test file
    CHANGELOG.md  single source of the version and of the GitHub release text

## Open items

[docs/ai/findings.md](docs/ai/findings.md) lists everything flagged during the documentation pass
(stale docs, known bugs, unverified areas, decisions that need the owner). Work through it
interactively with the owner; do not silently "fix" items.
