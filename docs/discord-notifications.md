# Discord notifications

[Discord Notify](../.github/workflows/discord-notify.yml) posts concise webhook embeds for
new PRs and published releases. PRs use `pull_request_target`, types `opened` and
`ready_for_review`. Draft PRs post only when marked ready; bot PRs never post.
Releases use `release`, type `published`, including published prereleases. Draft
release creation, edits, PR reopen/close/merge and issue events post nothing. This workflow is the
only Discord integration: do not add a repository webhook pointing at Discord's `/github`
endpoint, or every PR is announced twice. The destination comes from the
`DISCORD_WEBHOOK_URL` repository secret.

Pull requests trigger on `pull_request_target`, not `pull_request`, so fork PRs still see the
secret. This stays safe only because the notification workflow never checks out
code: event/API title, URL, author and body strings are only jq data. Do not add
a checkout step. Fixture tests run in the separate, unprivileged `CI` workflow.

Both cards put the author on its own row with login, profile URL and avatar.
PR titles are `#{number}: {title}` linked to the PR, with a green border and the
first nonempty body paragraph summarized to about 200 characters. Release titles
are `Release: {name}` (tag when unnamed), linked to the release, with a blue
border and a first-paragraph summary of at most 501 characters. Comments and
headings are removed from summaries; both titles are capped at 256 characters.
Mentions are disabled. Complete notes and downloads are available at the release
link rather than copied into the card.

### Manual catch-up

Publishing happened before release notifications were configured for v0.0.10-alpha.
The new trigger is not retroactive. Announce a missed published release explicitly:

```sh
gh workflow run discord-notify.yml --ref main -f release_tag=v0.0.10-alpha
```

The manual job fetches that tag from this repository with a read-only GitHub
token and rejects draft/unpublished releases. Use the workflow deployed on
`main`. Repeating a successful manual run posts another card; edits to release
notes do not repost automatically. Verify the previous job result before retrying.
Release delivery fails on a missing webhook or failed HTTP request; existing PR
notifications retain their skip behavior when the secret is absent.

### Offline validation

```sh
python3 tests/test_discord_notifications.py
```

These fixtures execute the actual workflow Bash/jq scripts with intercepted
curl/API calls: published and manual cards, draft/unpublished rejection,
missing webhook and failed delivery, long/quoted/shell-like text, empty notes,
and existing PR cards. They require Bash/jq and never post to Discord. `CI`
runs them on the same Ubuntu runner type used for delivery. No game build is
needed for this workflow change.

See [Contributing](../CONTRIBUTING.md) for repository validation conventions.
