#!/usr/bin/env python3
"""Generate small, redistributable W3E ramp regressions sampled from campaign maps."""
from pathlib import Path
import struct
import sys

# SW-first 6x6 crops: Human03 (16,20), NightElfX04 (53,40) and (58,32).
# Only numerical terrain fields are retained; no retail models or textures.
SAMPLES = (
    (
        [8192,8192,8192,8192,8268,8192,
         8192,8192,8192,8268,8268,8268,
         8268,8192,8268,8268,8268,8268,
         8268,8268,8268,8268,8268,8268,
         8268,8268,8268,8268,8268,8268,
         8268,8268,8268,8268,8268,8268],
        [2,2,2,3,3,3,
         2,2,2,3,3,3,
         2,2,2,3,3,3,
         2,2,2,3,3,3,
         2,2,2,2,2,2,
         2,2,2,2,2,2],
        [0,0,0,0,0,0,
         0,0,0,0,0,0,
         0,0,0,0,0,0,
         0,1,1,1,1,1,
         0,1,1,1,1,1,
         0,1,1,1,1,1],
    ),
    (
        [8217,8299,8342,8320,8292,8280,
         8280,8377,8439,8428,8391,8376,
         8346,8479,8559,8568,8527,8487,
         8447,8582,8723,8653,8605,8592,
         8636,8683,8719,8670,8598,8540,
         8605,8616,8638,8612,8530,8444],
        [7,6,6,6,7,7,
         6,6,7,7,7,7,
         6,6,7,7,7,7,
         6,6,7,7,7,7,
         6,6,6,6,6,6,
         6,6,6,6,6,6],
        [0,0,0,0,0,0,
         0,0,0,0,0,0,
         0,0,0,0,0,0,
         1,1,1,1,1,1,
         1,1,1,1,1,1,
         1,1,1,1,1,1],
    ),
    (
        [8408,8442,8462,8455,8421,8191,
         8397,8410,8406,8380,8315,8176,
         8397,8385,8366,8324,8249,8159,
         8381,8356,8330,8281,8214,8150,
         8341,8319,8289,8249,8200,8161,
         8297,8283,8256,8224,8194,8162],
        [6,6,6,6,6,6,
         6,6,6,6,6,6,
         7,7,7,7,6,6,
         7,7,7,7,6,6,
         7,7,7,7,6,6,
         7,7,7,6,6,6],
        [1,1,1,1,1,1,
         1,1,1,1,1,1,
         1,1,1,1,1,1,
         0,0,0,0,0,0,
         0,0,0,0,0,0,
         0,0,0,0,0,0],
    ),
)


def generate(directory, samples):
    directory.mkdir(parents=True, exist_ok=True)
    pack = lambda fmt, *values: struct.pack("<" + fmt, *values)
    width = 6 * len(samples)
    terrain = b"W3E!" + pack("IcI", 11, b"L", 0)
    terrain += pack("I", 1) + b"Ldrt" + pack("I", 1) + b"CLdi"
    terrain += pack("2I2f", width, 6, 0, 0)
    for y in range(6):
        for heights, levels, ramps in samples:
            for x in range(6):
                i = x + y * 6
                ramp = ramps[i]
                # Nonzero authored cliff variation verifies that computed markers replace it.
                terrain += pack("2H3B", heights[i], 8192, 0x10 if ramp else 0, 0x60, levels[i])
    (directory / "war3map.w3e").write_bytes(terrain)


if __name__ == "__main__":
    output = Path(sys.argv[1])
    # Transpose the Human crop as well to exercise the independent north/south midpoint branch.
    transposed = tuple([values[y + x * 6] for y in range(6) for x in range(6)]
                       for values in SAMPLES[0])
    generate(output / "Human03", (SAMPLES[0], transposed))
    generate(output / "NightElfX04", SAMPLES[1:])
