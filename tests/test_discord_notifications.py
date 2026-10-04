#!/usr/bin/env python3
"""Execute the workflow scripts with local fixtures; never contact Discord."""

import copy
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = (ROOT / ".github/workflows/discord-notify.yml").read_text()
SCRIPTS = [textwrap.dedent(block) for block in re.findall(
    r"^        run: \|\n((?:^          .*\n|^\n)+)", WORKFLOW, re.MULTILINE)]
RELEASE = {
    "name": "OpenRealm v0.0.10-alpha", "tag_name": "v0.0.10-alpha",
    "html_url": "https://github.com/corepunch/open-realm/releases/tag/v0.0.10-alpha",
    "body": "<!-- hidden -->\n\n# Changes\n\nHero XP and large-map fixes.\n\nMore details.",
    "draft": False, "published_at": "2026-10-03T14:46:39Z",
    "author": {"login": "corepunch", "html_url": "https://github.com/corepunch",
               "avatar_url": "https://avatars.githubusercontent.com/fixture"},
}


class DiscordNotificationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.event_path = self.root / "event.json"
        self.api_call = self.root / "api-call.txt"

    def execute(self, script, release=None, event="release", **overrides):
        release = RELEASE if release is None else release
        self.event_path.write_text(json.dumps({"release": release}))
        environment = dict(os.environ, DISCORD_WEBHOOK_URL="https://example.invalid/webhook",
                           GITHUB_EVENT_NAME=event, GITHUB_EVENT_PATH=str(self.event_path),
                           GITHUB_REPOSITORY="corepunch/open-realm", GH_TOKEN="fixture-token",
                           RELEASE_TAG=release["tag_name"], FAKE_RELEASE_JSON=json.dumps(release),
                           TEST_API_CALL=str(self.api_call))
        environment.update(overrides)
        # Stub only external delivery/API calls. The actual Bash/jq workflow
        # formatter and eligibility checks run unchanged against each fixture.
        prefix = '''
curl() { cat; return "${TEST_CURL_STATUS:-0}"; }
gh() { printf '%s\\n' "$*" > "$TEST_API_CALL"; printf '%s' "$FAKE_RELEASE_JSON"; }
'''
        return subprocess.run(["bash", "-eo", "pipefail", "-c", prefix + script],
                              cwd=self.root, env=environment, text=True, capture_output=True)

    def card(self, result):
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        payload = json.loads(result.stdout)
        self.assertEqual(payload["allowed_mentions"], {"parse": []})
        self.assertEqual(len(payload["embeds"]), 1)
        return payload["embeds"][0]

    def test_published_release_card_has_author_link_and_first_paragraph(self):
        self.assertEqual(len(SCRIPTS), 2)
        card = self.card(self.execute(SCRIPTS[1]))
        self.assertEqual(card["title"], "Release: " + RELEASE["name"])
        self.assertEqual(card["url"], RELEASE["html_url"])
        self.assertEqual(card["author"]["name"], "corepunch")
        self.assertEqual(card["description"], "Hero XP and large-map fixes.")
        self.assertFalse(self.api_call.exists())

    def test_manual_catch_up_fetches_published_release_by_encoded_tag(self):
        release = dict(RELEASE, tag_name="release/1.0")
        self.card(self.execute(SCRIPTS[1], release, event="workflow_dispatch"))
        self.assertEqual(self.api_call.read_text().strip(),
                         "api repos/corepunch/open-realm/releases/tags/release%2F1.0")

    def test_draft_or_unpublished_release_is_rejected(self):
        for fields in ({"draft": True}, {"published_at": None}):
            with self.subTest(fields=fields):
                result = self.execute(SCRIPTS[1], dict(RELEASE, **fields))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Only a published release", result.stdout)
                self.assertNotIn('"embeds"', result.stdout)

    def test_missing_webhook_or_failed_delivery_fails_release_job(self):
        result = self.execute(SCRIPTS[1], DISCORD_WEBHOOK_URL="")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot be sent", result.stdout)
        self.assertEqual(self.execute(SCRIPTS[1], TEST_CURL_STATUS="22").returncode, 22)

    def test_empty_name_and_notes_use_tag_without_description(self):
        card = self.card(self.execute(SCRIPTS[1], dict(RELEASE, name="", body=None)))
        self.assertEqual(card["title"], "Release: v0.0.10-alpha")
        self.assertNotIn("description", card)

    def test_long_quoted_text_and_shell_syntax_stay_data(self):
        release = copy.deepcopy(RELEASE)
        release["name"] = 'Quoted "name" $(touch injected) `touch injected` @everyone ' + "x" * 300
        release["body"] = "word " * 300
        card = self.card(self.execute(SCRIPTS[1], release))
        self.assertLessEqual(len(card["title"]), 256)
        self.assertLessEqual(len(card["description"]), 501)
        self.assertIn("$(touch injected)", card["title"])
        self.assertFalse((self.root / "injected").exists())

    def test_existing_pr_card_retains_summary_and_author_row(self):
        card = self.card(self.execute(SCRIPTS[0], PR_NUMBER="572", PR_TITLE='Fix "release" notifications',
                                     PR_URL="https://github.com/corepunch/open-realm/pull/572",
                                     PR_AUTHOR="corepunch", PR_AVATAR=RELEASE["author"]["avatar_url"],
                                     PR_AUTHOR_URL=RELEASE["author"]["html_url"],
                                     PR_BODY="# Summary\n\nPost new releases.\n\nDetails."))
        self.assertEqual(card["title"], '#572: Fix "release" notifications')
        self.assertEqual(card["author"]["name"], "corepunch")
        self.assertEqual(card["description"], "Post new releases.")


if __name__ == "__main__":
    unittest.main()
