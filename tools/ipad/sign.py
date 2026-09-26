#!/usr/bin/env python3
"""Sign an iPad app using a matching installed development identity/profile."""
import argparse
from datetime import datetime, timezone
import hashlib
from pathlib import Path
import plistlib
import re
import shutil
import subprocess


def matches(pattern, identifier):
    return pattern == identifier or (pattern.endswith('.*') and identifier.startswith(pattern[:-1]))


def sign_binary(identity, entitlement_path, target):
    subprocess.run(['codesign', '--force', '--sign', identity, '--entitlements', str(entitlement_path),
                    '--generate-entitlement-der', str(target)], check=True)


def adhoc(target):
    """Ad-hoc sign every framework plus the app (simulator/local use)."""
    for framework in sorted((target / 'Frameworks').glob('*.framework')):
        subprocess.run(['codesign', '--force', '--sign', '-', str(framework)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(target)], check=True)
    print('Ad-hoc signed ' + str(target))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path, nargs='?')
    parser.add_argument('--adhoc', type=Path)
    parser.add_argument('--team')
    parser.add_argument('--profile', type=Path)
    args = parser.parse_args()
    if args.adhoc:
        adhoc(args.adhoc.resolve())
        return
    if not args.app:
        raise SystemExit('Pass the .app bundle, or --adhoc for simulator signing.')
    app = args.app.resolve()
    bundle_id = plistlib.loads((app / 'Info.plist').read_bytes())['CFBundleIdentifier']
    identities = subprocess.check_output(['security', 'find-identity', '-v', '-p', 'codesigning'], text=True)
    valid = set()
    for line in identities.splitlines():
        match = re.search(r'\b([A-F0-9]{40})\s+"', line)
        if match and 'CSSMERR_' not in line:
            valid.add(match[1])
    if args.profile:
        paths = [args.profile]
    else:
        paths = []
        for folder in ('Library/Developer/Xcode/UserData/Provisioning Profiles',
                       'Library/MobileDevice/Provisioning Profiles'):
            paths.extend((Path.home() / folder).glob('*.mobileprovision'))
    candidates = []
    for path in paths:
        decoded = subprocess.run(['security', 'cms', '-D', '-i', str(path)], capture_output=True)
        if decoded.returncode:
            continue
        profile = plistlib.loads(decoded.stdout)
        entitlements = profile.get('Entitlements', {})
        team = entitlements.get('com.apple.developer.team-identifier')
        prefix = profile.get('ApplicationIdentifierPrefix', [''])[0]
        identifier = prefix + '.' + bundle_id
        if (args.team and args.team != team) or not team:
            continue
        if 'iOS' not in profile.get('Platform', []):
            continue
        if profile['ExpirationDate'] <= datetime.now(timezone.utc).replace(tzinfo=None) or not entitlements.get('get-task-allow'):
            continue
        if not matches(entitlements.get('application-identifier', ''), identifier):
            continue
        certificates = {hashlib.sha1(cert).hexdigest().upper() for cert in profile.get('DeveloperCertificates', [])}
        available = sorted(valid & certificates)
        if available:
            candidates.append((profile['ExpirationDate'], str(path), available[0], identifier, team))
    if not candidates:
        raise SystemExit('No matching development profile and signing identity for ' + bundle_id +
                         '. Install a valid profile/certificate or pass PROFILE=/path/to/profile.mobileprovision'
                         ' (and TEAM=... if needed). Simulator builds need neither.')
    _, profile, identity, identifier, team = max(candidates)
    shutil.copy2(profile, app / 'embedded.mobileprovision')
    entitlement_path = app.parent / (app.stem + '.entitlements.plist')
    entitlement_path.write_bytes(plistlib.dumps({
        'application-identifier': identifier,
        'com.apple.developer.team-identifier': team,
        'get-task-allow': True,
    }))
    # Frameworks first: the app signature covers (and requires) signed content.
    for framework in sorted((app / 'Frameworks').glob('*.framework')):
        sign_binary(identity, entitlement_path, framework)
    sign_binary(identity, entitlement_path, app)
    subprocess.run(['codesign', '--verify', '--strict', str(app)], check=True)
    print('Signed ' + bundle_id + ' with team ' + team)


if __name__ == '__main__':
    main()
