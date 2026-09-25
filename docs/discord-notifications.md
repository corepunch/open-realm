# Discord notifications

[Discord Notify](../.github/workflows/discord-notify.yml) posts custom webhook embeds for
opened/reopened/ready-for-review pull requests, opened/reopened issues, and published releases.
The destination comes from the `DISCORD_WEBHOOK_URL` repository secret.

Pull requests use one clickable title: `{author} #{number}: {title}`. Do not add an embed
`author` object: that creates a separate author row. The title retains the PR URL and green
border, is limited to 256 characters, and may wrap on narrow clients. Issues and releases
use their own title formats in the workflow's event switch. Mentions are disabled.

To verify formatting without posting to Discord, run the workflow's Bash block with fixture
event variables and prepend `curl() { cat; }`. Inspect the resulting JSON: one embed with
the expected `title`, `url`, and `color`, and no `author` field. Include PR, issue, release,
and long/quoted-title cases. No game build is needed for this presentation-only workflow.

See [Contributing](../CONTRIBUTING.md) for repository validation conventions.
