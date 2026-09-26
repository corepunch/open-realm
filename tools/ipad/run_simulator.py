#!/usr/bin/env python3
"""Install and launch an app on one explicitly selected available iPad simulator."""
import argparse
import json
import os
from pathlib import Path
import plistlib
import subprocess


def simctl(*args):
    subprocess.run(['xcrun', 'simctl', *args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('--device', help='iPad simulator name or UDID; defaults to a booted iPad, then the first available iPad')
    args = parser.parse_args()
    devices = json.loads(subprocess.check_output(['xcrun', 'simctl', 'list', 'devices', 'available', '--json']))
    ipads = [device for group in devices['devices'].values() for device in group
             if device.get('isAvailable') and device['name'].startswith('iPad')]
    if args.device:
        ipads = [device for device in ipads if args.device in (device['name'], device['udid'])]
        if len(ipads) > 1:
            raise SystemExit('More than one simulator has that name; use DEVICE=<UDID>')
    if not ipads:
        raise SystemExit('No matching available iPad simulator. Install an iOS simulator runtime or set DEVICE=<iPad UDID>.')
    device = sorted(ipads, key=lambda device: device['state'] != 'Booted')[0]
    udid = device['udid']
    print('Launching on ' + device['name'] + ' (' + udid + ')', flush=True)
    if device['state'] != 'Booted':
        simctl('boot', udid)
    developer = os.environ.get('DEVELOPER_DIR') or subprocess.check_output(['xcode-select', '-p'], text=True).strip()
    candidates = [Path(developer) / 'Applications/Simulator.app',
                  Path('/Applications/Simulator.app')]
    simulator_app = next((path for path in candidates if path.is_dir()), None)
    if simulator_app:
        subprocess.run(['open', '-a', str(simulator_app),
                        '--args', '-CurrentDeviceUDID', udid], check=True)
    else:
        # Stripped/headless Xcode installs have no Simulator.app; install and
        # launch below still work, the GUI just never opens.
        print('No Simulator.app found; continuing headless', flush=True)
    simctl('bootstatus', udid, '-b')
    simctl('install', udid, str(args.app.resolve()))
    bundle_id = plistlib.loads((args.app / 'Info.plist').read_bytes())['CFBundleIdentifier']
    simctl('launch', '--terminate-running-process', udid, bundle_id)


if __name__ == '__main__':
    main()
