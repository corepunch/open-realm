#!/usr/bin/env python3
"""Prepare the licensed EOS C SDK and restricted player config for releases."""

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
import tempfile
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, build_opener
from zipfile import BadZipFile, ZipFile


SDK_VERSION = "1.19.2.1-CL58105819"
SDK_SHA256 = "56a3bd805df426606946d74ba662f25223ffe24159e7884a625225ba80b582fb"
NOTICE = "ThirdPartyNotices/ThirdPartySoftwareNotice.txt"
RUNTIMES = {
    "Linux": "libEOSSDK-Linux-Shipping.so",
    "macOS": "libEOSSDK-Mac-Shipping.dylib",
    "Windows": "EOSSDK-Win64-Shipping.dll",
}
CONFIG_KEYS = tuple("OPENREALM_EOS_" + suffix for suffix in (
    "PRODUCT_ID", "SANDBOX_ID", "DEPLOYMENT_ID", "CLIENT_ID", "CLIENT_SECRET"))
MAX_DOWNLOAD = 2 * 1024 * 1024 * 1024
MAX_EXTRACTED = 256 * 1024 * 1024


def config_text(environment):
    values = []
    for key in CONFIG_KEYS:
        value = environment.get(key, "")
        if not value or len(value) >= 128 or any(c.isspace() for c in value):
            raise ValueError("Missing or invalid release secret: " + key)
        values.append(key + "=" + value + "\n")
    return "".join(values)


def require_https(url):
    parts = urlsplit(url)
    if parts.scheme != "https" or not parts.hostname or parts.username or parts.password:
        raise ValueError("EOS_SDK_DOWNLOAD_URL must be an HTTPS archive URL")


class HTTPSRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, url):
        require_https(url)
        return super().redirect_request(request, fp, code, message, headers, url)


def download_sdk(url, output):
    require_https(url)
    try:
        opener = build_opener(HTTPSRedirect())
        with opener.open(url, timeout=60) as response, output.open("wb") as target:
            total = 0
            while chunk := response.read(1024 * 1024):
                total += len(chunk)
                if total > MAX_DOWNLOAD:
                    raise ValueError("EOS SDK download exceeds size limit")
                target.write(chunk)
    except Exception:
        # Signed URLs can contain credentials; never print urllib's exception/URL.
        raise ValueError("EOS SDK download failed; check EOS_SDK_DOWNLOAD_URL and runner access") from None


def extract_sdk(archive, output, platform, expected_hash=SDK_SHA256):
    digest = hashlib.sha256()
    with archive.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != expected_hash:
        raise ValueError("EOS SDK SHA-256 mismatch; expected C SDK " + SDK_VERSION)

    runtime = "SDK/Bin/" + RUNTIMES[platform]
    with ZipFile(archive) as sdk:
        files = [entry for entry in sdk.infolist() if not entry.is_dir() and (
            entry.filename.startswith("SDK/Include/") or entry.filename in (runtime, NOTICE))]
        names = [entry.filename for entry in files]
        required = ("SDK/Include/eos_sdk.h", runtime, NOTICE)
        if not all(name in names for name in required):
            raise ValueError("EOS archive lacks required C headers, platform runtime or notices")
        if len(set(names)) != len(names) or sum(entry.file_size for entry in files) > MAX_EXTRACTED:
            raise ValueError("EOS archive has duplicate files or exceeds extraction limit")
        for entry in files:
            path = PurePosixPath(entry.filename)
            if (path.is_absolute() or ".." in path.parts or "\\" in entry.filename
                    or stat.S_ISLNK(entry.external_attr >> 16)):
                raise ValueError("Unsafe path in EOS archive")
        for entry in files:
            target = output.joinpath(*PurePosixPath(entry.filename).parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with sdk.open(entry) as source, target.open("wb") as destination:
                shutil.copyfileobj(source, destination)


def prepare(archive, output, platform, environment, expected_hash=SDK_SHA256):
    # Validate configuration before downloading or leaving a partially usable SDK.
    config = config_text(environment)
    if output.exists():
        raise ValueError("EOS release output already exists; use a fresh directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="eos-release-", dir=output.parent) as staging:
        staging = Path(staging)
        if archive is None:
            url = environment.get("EOS_SDK_DOWNLOAD_URL", "")
            if not url:
                raise ValueError("Missing release secret: EOS_SDK_DOWNLOAD_URL")
            archive = staging / "sdk.zip"
            download_sdk(url, archive)
        content = staging / "content"
        extract_sdk(archive, content, platform, expected_hash)
        config_path = content / "eos.cfg"
        with config_path.open("w", encoding="utf-8", newline="\n") as target:
            os.chmod(config_path, 0o600)
            target.write(config)
        content.rename(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=RUNTIMES, required=True)
    parser.add_argument("--archive", type=Path, help="Validate an already downloaded official ZIP")
    parser.add_argument("--output", type=Path, default=Path("data/eos"))
    args = parser.parse_args()
    try:
        prepare(args.archive, args.output, args.platform, os.environ)
    except (ValueError, OSError, BadZipFile):
        # Only deliberate validation messages are safe to show. IO exceptions may
        # contain paths/URLs; report the category without leaking secret values.
        error = sys.exc_info()[1]
        message = str(error) if isinstance(error, ValueError) else "Cannot prepare EOS release files"
        print("EOS release: " + message, file=sys.stderr)
        return 1
    print("Prepared EOS C SDK " + SDK_VERSION + " for " + args.platform)
    return 0


if __name__ == "__main__":
    sys.exit(main())
