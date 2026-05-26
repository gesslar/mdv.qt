# Releasing

How releases get cut for mdv, plus a handful of non-obvious quirks the CD
pipeline papers over. If you find yourself wondering "why is *that* there",
the answer is probably in **[Lessons & quirks](#lessons--quirks)** at the
bottom — read those before you "clean things up".

## Cutting a release

The release pipeline is **manually triggered but version-gated** — it only
does anything when `CMakeLists.txt`'s `VERSION` has moved past the latest
`v*.*.*` tag. The flow is two steps:

1. Bump `VERSION` in `CMakeLists.txt` and merge to `main`.
2. Run the `release` workflow: Actions tab → **release** → "Run workflow"
   → leave the **"Build + diagnose only"** checkbox **unticked** → green
   button.

That's it. The workflow:

- Verifies the version bump (extracts `VERSION` from `CMakeLists.txt`,
  compares against existing `v*.*.*` tags — if nothing's changed, the
  workflow ends green with a notice and creates nothing).
- Builds the Windows NSIS installer in parallel with the four Linux
  artifacts (`.deb`, `.rpm`, `.AppImage`, `.flatpak`).
- After **all** builds succeed, creates and pushes a `v<version>` tag
  against the exact commit it built (pinned via `github.sha`, so a push
  to `main` mid-run can't move the tag).
- Creates a GitHub Release attached to that tag, with all five artifacts
  uploaded and auto-generated release notes pulled from merged PRs since
  the previous tag.

Tag creation only happens **after** the builds pass, so a failing build
won't leave an orphan tag pointing at code that never shipped.

### Dry-run

Tick the **"Build + diagnose only"** checkbox to skip the tag + release
jobs. The build matrix still runs to completion (artifacts uploaded as
workflow artifacts, downloadable from the run page), but nothing touches
git or the Releases page. Useful for validating the pipeline after a
workflow change without committing to a release.

### Windows: Microsoft Store / MSIX

The NSIS installer published in each GitHub Release is **unsigned**.
Code-signing for the Windows build is done separately through the
**Microsoft Store, via an MSIX wrapper around the NSIS `.exe`** — the
Store-side repackaging applies the signature so end users don't see a
SmartScreen warning. The GitHub Release copy is for direct downloaders
who don't want the Store, and is intended to stay unsigned.

Do **not** add `signtool` / EV-cert steps to the NSIS pipeline.

## Architecture

`.github/workflows/release.yml` has five jobs:

| Job | What it does |
| --- | --- |
| `determine-version` | Extracts `VERSION` from `CMakeLists.txt`, compares against existing `v*.*.*` tags via `gesslar/new-version-questionmark`. Outputs the bare version on a bump or a 🙅🏻‍♂️ sentinel when nothing's changed; downstream jobs gate on the sentinel. |
| `windows` | Installs Qt 6 + MinGW via `jurplel/install-qt-action`, NSIS + GNU make via Chocolatey, runs `make windows`, uploads the installer. |
| `linux` (matrix: deb / rpm / appimage / flatpak) | Each entry runs `docker/dist.sh <format>` inside the pinned Fedora container (`docker/Dockerfile`), uploads its single artifact. Runners are independent, so the four run in parallel — wall-clock time is bounded by the slowest (flatpak). |
| `tag` | Creates and pushes `v<version>`. Gated on every build succeeding AND `inputs.dry_run` being false. |
| `release` | Downloads all artifacts and creates the GitHub Release with auto-generated notes. |

The matrix uses `fail-fast: false` so a flatpak failure doesn't murder
the in-flight deb/rpm/AppImage builds — we want all four results in one
dispatch.

### Why dispatch-triggered, not tag-pushed

The workflow itself creates the tag, so triggering on `push: tags` would
either re-fire (loop) or fire under a ref that's no longer in sync with
the build commit. Manual dispatch + a version-bump check inside the run
is the cleanest split: you decide *when*, the `CMakeLists.txt` bump
decides *whether*.

## Lessons & quirks

These exist because we hit them and have no good fix from inside the repo.
None of them are leftover cruft — don't remove them without reading the
"why".

### Flatpak's `appstream-compose: false` is deliberate

`dev.gesslar.mdv.yml` sets `appstream-compose: false`. This **looks** like
a TODO that someone forgot to remove, but it's a deliberate workaround for
an upstream bug in **glycin** (the image-loader library `appstreamcli
compose` uses to thumbnail the app icon). The chain:

1. Glycin sandboxes its loader process via `bwrap --unshare-all`. That
   creates a fresh network namespace and brings up the loopback interface
   inside it.
2. On GitHub-hosted runners, the kernel refuses the `RTM_NEWADDR` netlink
   op (`bwrap: loopback: Failed RTM_NEWADDR: Operation not permitted`),
   even from inside a `--privileged` container.
3. Glycin runs a bwrap-availability probe ahead of the real spawn that
   *would* route this to a no-sandbox fallback (which works on this
   runner — verified in an earlier diagnostic pass). But the probe only
   matches one specific stderr string (`"No permissions to creating new
   namespace"`) and ignores exit codes, so it doesn't recognize the
   RTM_NEWADDR error and falsely concludes bwrap works.
4. The loader exits with status 1, appstreamcli surfaces it as
   `E: file-read-error` against the icon, and the bundle build fails.

WSL2's kernel doesn't restrict `RTM_NEWADDR` the same way, which is why
`make linux` succeeds locally against the same pinned Fedora image.

**Cost of skipping compose:** the bundle still ships the metainfo XML at
`/share/metainfo/dev.gesslar.mdv.metainfo.xml`. Software centers and
`flatpak info` read that, and end users' appstreamcli composes from it
client-side at install time. **Flathub does its own compose on
submission**, so this setting is irrelevant there — flip it back to `true`
(or delete the block) before a Flathub submission.

### Local Linux build needs WSL2 + an ext4 path on Windows

`make linux` works directly on Linux and macOS hosts. On Windows, it
needs **WSL2**, not Git Bash:

- Git Bash's MSYS path translation mangles the `docker run -v` flags
  (`-v /etc/passwd:/etc/passwd:ro` becomes
  `-v "C:\Program Files\Git\etc\passwd;C:\Program Files\Git\etc\passwd;ro"`),
  and `docker/dist.sh` blows up at startup with "Access is denied".
- Inside WSL2, build from the **ext4 home directory**, not from
  `/mnt/c/...`. flatpak-builder's `eu-strip` step fails on the 9p mount
  with "No such file or directory" while creating split-debug files.

The working recipe on a Windows dev box:

```bash
# Open a WSL2 shell (Start menu → "Ubuntu", or `wsl` from PowerShell)
cd ~
git clone https://github.com/gesslar/mdv.qt.git
cd mdv.qt
make linux
```

### Action versions are pinned to majors, not patches

Every external action in the workflow is pinned to a major tag
(`actions/checkout@v6`, `actions/upload-artifact@v7`, etc.), not full
SHAs. Authors keep the major tags on the latest patch within the major,
so we get Node-runtime bumps and bug fixes for free — but breaking
changes still require a deliberate `@v6 → @v7` edit, not a silent
auto-upgrade.

If supply-chain pinning ever becomes a hard requirement, swap the major
tags for full commit SHAs. That's a different concern from "we don't
support Node 20 anymore" deprecation warnings, which the major-tag pin
already handles.

## Future improvements

- **Build-once-package-many for the Linux matrix.** The `deb`, `rpm`,
  and `appimage` matrix entries all run `make release` from scratch
  (same cmake build against Fedora's Qt + KSyntaxHighlighting). Splitting
  out a shared "build" job + three trivial "package" jobs that download
  the build artifact would collapse ~3× redundant cmake builds into one.
  `flatpak` stays separate because it builds its own binary inside
  flatpak-builder's sandbox against the **KDE runtime's** Qt — different
  `.so` linkage, can't share with the cpack path.
