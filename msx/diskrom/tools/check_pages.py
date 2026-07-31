#!/usr/bin/env python3
"""Check that each page of the 32K ROM fits in the 16K it has.

Neither limit is enforced by the toolchain. appmake only objects past 32K, and
it writes the page 2 section over ROM offset 4000h after the main binary is
already there -- so page 1 running past 7FFFh produces a ROM that looks
perfectly good and has lost its last few hundred bytes. Page 2 has to stop
four bytes earlier still, because BFFCh-BFFFh is the FujiNet IO window.

Usage: check_pages.py <link map> <page 2 binary> <page 2 limit>
"""

import re
import sys

PAGE1_END = 0x8000

# Section end markers in the link map. Sections in page 1 are addressed
# normally; page 2 is orged into bank 1, which puts it past 0x10000, and RAM
# sections sit at C000h and above.
TAIL = re.compile(r"^__\w+_tail\s+= \$([0-9A-Fa-f]+) ;", re.MULTILINE)


def main(argv):
    map_path, p2_path, p2_limit = argv[1], argv[2], int(argv[3])

    with open(map_path) as handle:
        tails = [int(value, 16) for value in TAIL.findall(handle.read())]

    page1 = [tail for tail in tails if 0x4000 <= tail < 0xC000]
    if not page1:
        sys.exit("check_pages: no page 1 sections in %s" % map_path)

    with open(p2_path, "rb") as handle:
        page2 = len(handle.read())

    end = max(page1)
    failed = False

    if end > PAGE1_END:
        print("page 1 ends at %04Xh, %d bytes past 8000h; the tail would be "
              "overwritten by page 2" % (end, end - PAGE1_END))
        failed = True
    else:
        print("page 1: ends at %04Xh, %d bytes free" % (end, PAGE1_END - end))

    if page2 > p2_limit:
        print("page 2 is %d bytes and would run into the IO window at BFFCh"
              % page2)
        failed = True
    else:
        print("page 2: %d bytes, %d free below the IO window"
              % (page2, p2_limit - page2))

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
