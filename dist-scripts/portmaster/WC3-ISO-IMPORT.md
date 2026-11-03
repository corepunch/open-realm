# Original Warcraft III ISO import (PortMaster)

Place your original **Reign of Chaos** and, optionally, **The Frozen Throne**
`.iso` images directly in `open-realm-wc3/gamefiles/` and launch OpenRealm
from PortMaster. `.ISO` is also accepted; names can contain spaces. Do **not**
create an `isos/` subdirectory.

The first launch may take time and needs free space for a temporary
`Setup.mpq` and the recovered game archives. Both discs can be imported
in one launch. Do not remove or rename an ISO during installation.
Existing extracted MPQs in `gamefiles/warcraft-3/` continue to work without
ISOs.

The native importer copies explicitly named MPQs from the disc, then opens
`Setup.mpq` if present. It recovers named embedded game archives, including
compressed entries supported by OpenRealm's MPQ reader. For **anonymous**
embedded archives, it accepts only unencrypted, uncompressed, complete MPQs
that match multiple known paths inside the archive; it never guesses based on
file sizes or block order. Other installer contents are not extracted.

When an RoC installer includes `SetupDat/maps.lst` or
`SetupDat/Scenario.lst`, its `Setup.mpq` is kept for OpenRealm's in-game
map browser. The TFT `Setup.mpq` remains temporary. The game archives are
installed in `gamefiles/warcraft-3/`.

**The ISO files are deleted automatically after successful import.** The
launcher verifies the installed MPQ structures and checks that each supplied
ISO contributed recognizable Warcraft III data before recording success and
removing any disc images. If extraction, archive verification, or installation
fails, *all* ISOs are left untouched for retry. If the system cannot delete an
ISO (for example, a read-only card), it reports the problem and leaves it in
place; the game can still launch. The successful-import marker enables a later
launch to clean up unchanged ISOs that remained after an interrupted cleanup.
Keep your own backups if you want to retain the original disc images.

If identification fails, see `open-realm-wc3/log.txt`. An unrecognised retail
edition may need a newer fingerprint or separately supplied **legally owned**
installed-game MPQs. Neither the initial importer nor its synthetic tests
prove support for every original retail ISO layout. The original Blizzard
installer is never executed and no CD-key validation or DRM bypass is added.

Troubleshooting: Check `open-realm-wc3/log.txt` after a failed import.
The installer never replaces existing game archives. To retry importing a
previously successful ISO, restore the ISO in `gamefiles/` and remove
`gamefiles/warcraft-3/.portmaster-iso-import-v1` first.

### Developer checks (run on your machine)

After building `build/bin/isoextract` and `build/bin/wc3setup`, run the
synthetic fixtures with:

```sh
python3 tools/test_wc3setup_import.py build/bin/isoextract build/bin/wc3setup
```

These fixtures check recursive ISO extraction, missing members, map manifest
recognition, named and anonymous nested MPQ recovery, and duplicate rejection.
Then use the actual RoC and TFT discs on a PortMaster device and verify that
campaign/maps load. No retail disc content is bundled in the package or tests.
