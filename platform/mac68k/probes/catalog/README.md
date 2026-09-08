# MoreFiles catalog derivative probe (REUSE-03)

This is a separate host-library feasibility application. It does not mount a
host filesystem into cannedBSD or implement portable directory streams.
`PROVENANCE.md` records the explicitly altered source boundary and licenses.
The unchanged REUSE-02 diagnostic remains a separate failed-SDK experiment.

CI uses the existing pinned Retro68 image and publishes a distinct
`morefiles-probe.*` directory with `MoreFilesProbe.tar.gz`, `CatalogFixture.dsk`,
`fixture.json`, SHA256SUMS, exact commit/pipeline, logs and CODE size. **Compiled
is not guest accepted.** The probe archive's volume/application is named
`CannedBSD:CannedBSD` solely to reuse the existing Finder applet. The window is
clearly titled `MoreFiles catalog probe`; the ordinary CannedBSD artifact is
unchanged and stored separately.

The 2 MiB real HFS fixture is built twice with libhfs in the pinned image and
must have identical bytes/metadata. Fixed UTC timestamps and ordered creation
produce exact names, IDs, data/resource lengths in `fixture.json`; generated
expectations are compiled into the probe. The fixture is chmod 0444 before
publication. Root contains Empty, Subdir, Zero, Eight (`fixture\n`), Forked
(10 data / 17 resource bytes), and a 31-byte name. Subdir contains Sentinel.
Two explicit hidden metadata files, Desktop DB (2048 data bytes) and Desktop DF
(empty), follow the pinned cdrtools initialization for read-only HFS media.
They are included in the eight root entries checked by the probe. Guest boot
must verify that they prevent the earlier desktop-rebuild warning; protection
and full-image hash checks remain mandatory.

## Coordinator guest procedure

Only the coordinator owns emulator input. Reuse a fresh existing MAC-01 staged
run and its exclusive slot, verified clean seed, native prefs, and dedicated
share. Verify the exact probe archive and fixture against SHA256SUMS first.
The existing stager requires the archive filename `CannedBSD.tar.gz` and a
single-entry checksum file. Create a dedicated adapter directory without
changing the original published probe artifact (example on macOS):

```sh
probe=/absolute/path/to/downloaded/morefiles-probe
(cd "$probe" && shasum -a 256 -c SHA256SUMS)
adapter=$(mktemp -d /tmp/morefiles-stage.XXXXXX)
cp "$probe/MoreFilesProbe.tar.gz" "$adapter/CannedBSD.tar.gz"
cp "$probe/commit.txt" "$adapter/commit.txt"
(cd "$adapter" && shasum -a 256 CannedBSD.tar.gz > SHA256SUMS)
cmp "$probe/MoreFilesProbe.tar.gz" "$adapter/CannedBSD.tar.gz"
```

Pass this `$adapter` directory as the existing stager's `--artifact` argument,
with its usual commit/seed/slot/native-config arguments. The bytes still match
the original published MoreFilesProbe checksum. Preserve that original checksum,
fixture checksum and pipeline for the receipt; this naming adapter does not
turn the probe into ordinary CannedBSD acceptance. Copy the fixture into this
run as a separate file. Before boot:

1. Check no Basilisk PID or open disk handles conflict with the slot.
2. Keep the fixture chmod 0444, save its SHA256, and append this native prefs line:
   `disk */absolute/run/path/CatalogFixture.dsk`.
   Basilisk's [official README](https://github.com/cebix/macemu/blob/master/BasiliskII/README.md)
   documents the asterisk as guest write protection. Do not rely on mode bits alone.
3. Precreate an empty `shared/morefiles-result.txt`. Leave it in place for the
   entire guest lifetime; never remove/rename exported files while running.
4. Launch the staged native configuration using the existing process/prefs
   checks. The tested zzz Finder applet launches `CannedBSD:CannedBSD`.

The app resolves the explicit `CatalogFixture:` volume once with PBHGetVInfoSync,
then uses that nonzero vRefNum plus directory IDs. It reports 13 named checks
and the compiled commit to `Unix:morefiles-result.txt`, and stays open for visual
inspection. Capture a fresh guest/window screenshot. Check all 13 PASS lines,
terminal `ALL PASS`, exact commit and successful evidence writing. The access
and capacity cases are explicitly injected through real SDK PB types; they
are not claims of actual AFP denial or an enormous fixture.

After saving the result and screenshot, press Return to close the probe on its
original application stack. The go-away box or verified Cmd-Q also closes it.
Confirm Finder is visible, then use the existing
normal guest shutdown and confirm the dialog. Observe PID exit and closed disk
handles. Verify the entire fixture SHA256 again, equal to its published value.
Preserve result/screenshot hashes and exact commit/pipeline/archive hash. Do not
run the ordinary CannedBSD result validator against these different cases or
claim acceptance merely because the app archive staged successfully.

The eight root entries, fork lengths and exact manifest IDs must match unordered;
the nested sentinel must be absent. Other checks cover empty directories,
independent copied scans, early stop followed by full scan, missing child,
file-CNID rejection matching raw native directory-ID lookup, null callback,
access error propagation, capacity and signed
index/nonzero-volume bounds. A failed check or evidence write leaves the window
visible for diagnosis; there is no automated pass marker or forced shutdown.
