"""Read archive members through the engine's MPQ tool for map catalogs."""

import subprocess


def read_member(tool, archive, member, optional=False):
    result = subprocess.run([str(tool), '-mpq', str(archive), 'cat', member], capture_output=True)
    error = result.stderr.decode(errors='replace').strip()
    if optional and result.returncode == 1 and error.replace('\\', '/') == f'Cannot open MPQ file: {member}':
        return None
    if result.returncode or error or not result.stdout:
        raise RuntimeError(f'{archive}: {member}: {error or "empty/unreadable archive member"}')
    return result.stdout
