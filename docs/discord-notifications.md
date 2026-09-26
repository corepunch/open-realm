# Discord notifications

[Discord Notify](../.github/workflows/discord-notify.yml) posts one concise webhook embed on
pull-request creation (`pull_request_target`, type `opened`). Reopened, closed, merged,
issue, and release events post nothing. The destination comes from the
`DISCORD_WEBHOOK_URL` repository secret.

Pull requests trigger on `pull_request_target`, not `pull_request`, so fork PRs still see the
secret. This stays safe only because the job never checks out code: it posts
`github.event.pull_request` title/URL/author strings. Do not add a checkout step.

Pull requests use one clickable title and nothing else: `{author} #{number}: {title}`. Do not add an embed
`author` object, description, footer, or extra fields: that creates extra rows. The title retains the PR URL and green
border, is limited to 256 characters, and may wrap on narrow clients. Mentions are disabled.

To verify formatting without posting to Discord, run the workflow's Bash block with fixture
PR variables and prepend `curl() { cat; }`. Inspect the resulting JSON: one embed with
the expected `title`, `url`, and `color`, and no `author` field. Include long/quoted-title
cases. No game build is needed for this presentation-only workflow.

See [Contributing](../CONTRIBUTING.md) for repository validation conventions.
