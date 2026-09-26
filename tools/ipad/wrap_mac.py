#!/usr/bin/env python3
"""Wrap a signed iPad development build for local Apple silicon Mac launch."""
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    source, destination = (Path(arg).resolve() for arg in sys.argv[1:])
    if not (source / 'Info.plist').is_file() or not (source / source.stem).is_file():
        raise SystemExit('Expected a built iOS app bundle')
    if destination == source or source in destination.parents or destination in source.parents:
        raise SystemExit('The wrapper must be separate from the source app')
    if destination.exists():
        if not (destination / 'WrappedBundle').is_symlink():
            raise SystemExit('Refusing to replace a directory that is not an iPad wrapper')
        shutil.rmtree(destination)
    wrapper = destination / 'Wrapper'
    wrapper.mkdir(parents=True)
    # ditto preserves signing metadata. The signed inner iOS bundle is unchanged.
    subprocess.run(['ditto', str(source), str(wrapper / source.name)], check=True)
    (destination / 'WrappedBundle').symlink_to(Path('Wrapper') / source.name)


if __name__ == '__main__':
    main()
