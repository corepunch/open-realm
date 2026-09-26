# Discord notifications

[Discord Notify](../.github/workflows/discord-notify.yml) posts one concise webhook embed on
pull-request creation (`pull_request_target`, type `opened`). Reopened, closed, merged,
issue, and release events post nothing. The destination comes from the
`DISCORD_WEBHOOK_URL` repository secret.

Pull requests trigger on `pull_request_target`, not `pull_request`, so fork PRs still see the
secret. This stays safe only because the job never checks out code: it posts
`github.event.pull_request` title/URL/author strings. Do not add a checkout step.

Embed layout: the author gets its own row via the embed `author` object
(`name` = PR author login, `icon_url` = author avatar URL). The clickable title holds
only `#{number}: {title}` with the PR URL. No description, footer, or extra fields.
The title is limited to 256 characters and may wrap on narrow clients. The embed keeps
the green border. Mentions are disabled.

To verify formatting without posting to Discord, run the workflow's Bash block with fixture
PR variables (including `PR_AVATAR`) and prepend `curl() { cat; }`. Inspect the resulting JSON:
one embed with the expected `author.name`, `author.icon_url`, `title`, `url`, and `color`.
Include long/quoted-title cases. No game build is needed for this presentation-only workflow.

See [Contributing](../CONTRIBUTING.md) for repository validation conventions.
