# crop43.py RUNS OUT: T386MON's own window cut out of the v4.3 harness runs' pictures (as the
# T386MON step's checks/fix/crop.py: only T386MON's window leaves the run folders)
import sys
from PIL import Image
runs, out = sys.argv[1], sys.argv[2]
cuts = [
    ("t43-p16", "0076", (0, 0, 680, 452), "live-800x480x16-pointer-change.png"),
    ("t43-p16", "0103", (0, 0, 680, 452), "tests-800x480x16-pointer-change.png"),
    ("t43-f11", "0079", (0, 0, 680, 452), "f11-before-blocks-on-800x480x16.png"),
    ("t43-f11", "0082", (0, 0, 680, 452), "f11-after-blocks-off-800x480x16.png"),
]
for run, n, box, name in cuts:
    im = Image.open("%s/%s/snap%s.png" % (runs, run, n)).convert("RGB").crop(box)
    im.save("%s/%s" % (out, name), optimize=True)
    print(name, run, "snap" + n, box)
