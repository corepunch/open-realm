# GitHub Releases

## Release contract

Use a tested, explicit commit from `corepunch/open-realm`; do not let a moving
`main` decide the release contents. Inspect the latest release and compare its
tag with the intended commit. Credit commit/merged-PR authors by GitHub login,
link the relevant PRs, and describe observable changes rather than copying commit
subjects. Check network and save versions separately.

Published releases trigger [release.yml](../.github/workflows/release.yml).
The native Linux, macOS Intel and Windows jobs build optimized EOS-enabled
packages and run EOS session regressions. The Flatpak build passes its bundle
and installer scripts to a separate publication job. See
[EOS packaging](architecture/epic-online-services.md#github-release-builds)
and [Flatpak setup](flatpak-steam-deck.md) for their runtime contracts.

## Publication and verification

```sh
gh release list --repo corepunch/open-realm --limit 5
gh api repos/corepunch/open-realm/commits/main --jq .sha
gh run list --repo corepunch/open-realm --workflow c-cpp.yml --limit 5
git log PREVIOUS_TAG..COMMIT --format='%h %an %s'
gh pr list --repo corepunch/open-realm --state merged --base main --limit 20
gh release create TAG --repo corepunch/open-realm --target COMMIT \
  --title 'Release title' --notes-file /tmp/release-notes.md --latest
gh run list --repo corepunch/open-realm --workflow release.yml --limit 5
gh run view RUN_ID --repo corepunch/open-realm --json headSha,conclusion,jobs,url
gh release view TAG --repo corepunch/open-realm --json assets,url,isDraft
```

Replace placeholders with verified values. Keep the notes in a file to preserve
newlines and prevent shell interpretation. Existing alpha releases are published
with `isPrerelease=false` and marked latest; preserve that convention unless the
requested release policy changes.

Publication initially creates a release with no download assets. Completion
requires every release job to succeed and all five assets to have `uploaded`
state, nonzero sizes, and SHA-256 digests:

| Asset | Contents |
| --- | --- |
| `openwarcraft3-linux-x64.tar.gz` | `bin/`, `lib/`, `share/`, EOS notices; FFmpeg decoder/runtime closure |
| `openwarcraft3-macos-x64.tar.gz` | Intel executable, modules, share tree and EOS runtime/notices |
| `openwarcraft3-windows-x64.zip` | Executable, game modules, MinGW dependency closure, share tree and EOS runtime/notices |
| `io.github.corepunch.OpenRealm.flatpak` | x86_64 application bundle |
| `openrealm-flatpak-TAG.zip` | Same Flatpak bundle plus `install.sh` and `uninstall.sh` |

Download assets to a fresh directory with `gh release download TAG --dir DIR`.
Compare their hashes against GitHub's asset `digest` metadata, verify native
archives can be read, and check the installer ZIP embeds exactly the separately
published Flatpak bundle. Retail game assets must not be packaged. Update the
release's Validation paragraph with the completed run URL and verification result.

If a job fails, inspect its failed logs and rerun only the failed jobs once the
cause is addressed. A manual recovery must check out the release tag:

```sh
gh workflow run release.yml --repo corepunch/open-realm --ref TAG \
  -f tag=TAG -f publish=true
```

For publishing dispatches, checkout explicitly uses the `tag` input; `--ref`
selects the workflow definition. This permits recovery from a branch containing
workflow fixes while still building the original release commit. Validation
dispatches with `publish=false` build `--ref` without uploads.

Windows regression builds keep their DLLs under `build/eos-tests/lib`, outside
the executable directory. The regression step must put that directory on `PATH`:
Windows does not honor the Unix rpath layout. Otherwise `mpqtool.exe` exits with
127 while packing fixtures, before the EOS tests run. MSYS2 also needs `diffutils`
for the generated-header `cmp` checks. Both gaps were exposed by v0.0.11-alpha's
initial Windows release job; retain all fixture and EOS checks during recovery.

Published releases also trigger the existing
[Discord notification workflow](discord-notifications.md). Notes edits do not
repost; do not manually dispatch a second announcement for a successful automatic
notification.

## v0.0.11-alpha

Released from `c46f0486c9c07adf144e98e389d6ef63bc5aa935`, with 13 commits since
v0.0.10-alpha. [CI run 37191766306](https://github.com/corepunch/open-realm/actions/runs/37191766306)
passed all five jobs, including the full EOS-enabled suite and live gameplay,
relay, departure and process-crash checks. Contributor attribution is
`@sookyboo` for [#575](https://github.com/corepunch/open-realm/pull/575) and
`@corepunch` for the other changes. Save version remains 64; protocol remains 17.
The opt-in executable-version diagnostic is `+set wc3_report_data_version 1`.

[Release](https://github.com/corepunch/open-realm/releases/tag/v0.0.11-alpha)
and [platform build run](https://github.com/corepunch/open-realm/actions/runs/37192189744).
