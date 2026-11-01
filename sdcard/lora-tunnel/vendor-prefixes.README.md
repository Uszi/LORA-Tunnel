# Vendor evidence reference

`vendor-prefixes.csv` is a copy of Dragon Detector's 201-row public-fact table
from revision `7a09930ee8ab112b9b87e94c1c9e47e5e6faaa21`, checked against the
2026-10-13 IEEE-derived vendor registry snapshot described in its source.
The compiled equivalent is `lib/recon/src/dragon/vendor_prefixes.h`.

This CSV is an editable reference and is not automatically imported by the
firmware. Runtime additions go in `detection_rules.json`, limited to 64 rules.
Generic or multi-product vendor matches cannot establish camera, ALPR or
recording activity. Preserve the evidence and grade when extending the list.

Dragon Detector original work: copyright (c) 2026 Valleytech Custom Solutions
(Your Pal Kal / @valleytechsolutions), MIT. The full notice is bundled as
`vendor-prefixes.LICENSE.txt`. Source and research credit details are retained
in the source tree under `lib/recon/UPSTREAM.md` and `DRAGON-CREDITS.md`.
