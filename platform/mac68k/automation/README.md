# Fast System 7 guest test driver

The Hammerspoon driver boots one staged native Basilisk II guest, launches the
`zzz-run-tests` desktop applet, requires the shell image and fresh startup test
result, saves guest-window evidence, types `exit`, and selects Special → Shut
Down with a held mouse drag. Success requires the guest process to disappear,
`guest.py check` to accept its evidence, and `guest.py release` to verify both
disks closed. It never force-terminates the emulator.

## Set up once

Install Hammerspoon with its `hs` command and enable Accessibility and Screen
Recording for it. Keep the Python virtual environment, private configuration,
staged guest state, and temporary screenshots on **local, non-synchronized
storage**, for example `~/Library/Caches/bsdinacan`. Do not use iCloud-managed
Documents/Desktop paths. Evicted (`dataless`) NumPy files blocked Python imports
and an evicted scratch JSON blocked Hammerspoon's main thread during a measured
failure. A runner timer cannot interrupt a filesystem call blocked in the kernel.
Reinstall the environment in local storage rather than copying an evicted one;
leave live guest exports and mounted disks untouched until clean shutdown.

Install the host matcher in a Python 3.14 virtual environment:

```sh
python3.14 -m venv "$HOME/Library/Caches/bsdinacan/mac-test-venv"
"$HOME/Library/Caches/bsdinacan/mac-test-venv/bin/python" -m pip install -r platform/mac68k/automation/requirements.txt
"$HOME/Library/Caches/bsdinacan/mac-test-venv/bin/python" -c 'import cv2; print(cv2.__version__)'
```

Copy `config.example.json` to a private file outside the repository. Set `app`
to the stable installed BasiliskII.app path, `python` to that virtual environment,
and `state` to the same canonical absolute directory used by `guest.py stage`.
Do not put local paths or config files into the repository. No Hammerspoon
`init.lua` modification is needed.

The clean System 7 boot seed must have the compiled `zzz-run-tests` applet on
the desktop. Its exact tested source is `zzz-run-tests.applescript`: Finder opens
`CannedBSD:CannedBSD` on the attached volume. The calibrated seed used this source
saved as a Compiled Script and packed into Apple's applet stub, preserving the
`scpt` resource 128; its Finder type/creator were APPL/aplt. A seed containing
this verified applet is a prerequisite; host automation does not rebuild it. The supplied templates match the calibrated
System 7 layout, fonts, and 2x template scale. Recalibrate explicitly if these
change; a new dialog is not automatically dismissed.

## Run each artifact

First reserve the coordinator's shared guest slot and stage an exact Woodpecker
artifact using the parent directory's runner:

```sh
python3 platform/mac68k/guest.py stage \
  --artifact /path/to/downloaded-ci-artifact \
  --commit FULL_40_CHARACTER_COMMIT_SHA \
  --state /path/to/shared-guest-state \
  --boot-seed /path/to/clean-shutdown-System7.dsk \
  --native-template /path/to/current-basilisk-prefs \
  --rom /path/to/Mac.ROM
```

Then execute the driver, giving both paths explicitly:

```sh
hs -c 'macTestConfigPath="/path/to/private-config.json"; dofile("/path/to/bsdinacan/platform/mac68k/automation/run.lua")'
```

The driver waits for the matcher's JSON readiness handshake before launching the
guest. Imports must finish within `startup_timeout` (default 30 seconds); a
startup failure leaves the guest unlaunched. Overall guest timeout starts after
readiness. Failures terminate only the runner's own matcher helper and retain
the staged slot. This does not make cloud-backed filesystem access safe: keep
all operational paths local, including the driver source and its templates.

The driver derives all disk, preferences, and result paths from the active MAC-01
slot. It refuses an already running Basilisk II, reused evidence, a mismatching
configuration, or a process whose command does not use those staged preferences.
It pins the launched PID and stops on lost focus or a changed window frame.
Use a dedicated desktop during the roughly seconds-long input sequence.

The staging command precreates the empty result **before boot**. Native extfs
successfully rewrote that file in the measured run, while creating an absent
full-path file failed in prior trials. The driver never deletes, renames, or
truncates exported files while the emulator runs.

Results remain in the staged run: `acceptance.json` binds the exact archive and
result hashes; `automation-<timestamp>/` contains `test.png`, copied guest output,
and `run.json` with action timestamps. Only the emulator window is captured.
Failures retain `failure.png` when available and leave the guest and slot for
inspection. Stop the driver without killing the guest:

```sh
hs -c 'if macTestRun and macTestRun.active then macTestRun.stop() end'
```

After inspecting a failure, shut down the guest cleanly and release its slot
with `guest.py release`. Start from a newly staged run for another attempt.

## Close a failed guest application after inspection

If MAC-03 is showing a setup/evidence failure, use the separate manual recovery
action after inspecting it. The main driver must be stopped, the same staged
Basilisk II process must remain frontmost, and its PID must be supplied explicitly:

```sh
hs -c 'macTestConfigPath="/path/to/local-config.json"; macClosePID=INSPECTED_PID; dofile("/path/to/bsdinacan/platform/mac68k/automation/close-window.lua")'
```

This action matches a crop containing both the cannedBSD title and its classic
Mac go-away box, then posts one mouseMoved/down/up sequence at the crop's
calibrated hotspot. It does not send Command-Q: the coordinator observed
`hs.eventtap.keyStroke({'cmd'}, 'q', ...)` reaching this native Basilisk build as
bare `q`. It does not shut down or terminate the emulator, release the guest
slot, overwrite failed evidence, or alter the normal driver's happy path.

The action verifies PID/preferences ownership and stable frontmost window
geometry before input. Low-confidence or ambiguous images cause no click. Each
attempt creates `recovery-close-<timestamp>/` in the staged run with a fresh
`before.png`, an `after.png` following input, and `action.json`. **Inspect the
after screenshot to verify the application closed**; `click_sent` means input
was delivered, not proof that Finder is ready. A confirmation dialog or another
unexpected state requires inspection. The emulator and slot remain owned by the
coordinator for the ordinary, separately verified shutdown procedure.

The supplied 842×36 pixel crop is normalized to the matcher's 2x point scale;
its hotspot is (28,18) pixels inside the crop. It comes from the active System 7
cannedBSD title bar in the 2026-09-07 failure-inspection capture. It contains no
host desktop or guest output. Window movement is supported through image
matching; title/layout/font changes require recalibration. The synthetic test
checks translated targets and rejects duplicates. It does not simulate UI
behavior. The coordinator separately verified one live failure-inspection close
on 2026-09-07: match score 0.99935, followed by an after screenshot showing Finder
without the cannedBSD window. This is one observed recovery, not a repeated-run
reliability claim.

## Calibration evidence and bounds

The source driver completed one measured cold cycle in **12.6957 seconds** on
2026-09-07: boot, desktop matching, applet launch, shell and eight-PASS/ALL PASS
output, guest screenshot, typed exit, menu selection, and observed process exit.
That timed trial preceded repository staging/provenance integration; it tested
the locally configured disk and does not identify an exact CI artifact. The
saved project driver needs its own exact-artifact integration run. This single
trial is not evidence of hundreds of successful repetitions.

Mouse delivery explicitly posts `mouseMoved`, then down/up after short delays.
Menus retain left-button state and post `leftMouseDragged` before releasing it.
Replacing these with host pointer movement or convenience clicks left the guest
cursor stale during calibration. Those proven event sequences are preserved.

The persistent Python matcher normalizes live window snapshots to 2x point
scale and uses the supplied crops. It requires correlation at least 0.97 and
rejects a spatially distinct second peak at 0.95 or higher. Flat, missing, or
oversized templates, malformed frames, and out-of-window targets fail closed.
The menu crop excludes hover-dependent highlighting; its Shut Down offset is
calibrated from the same System 7 menu image. A different menu layout requires
new calibration. Overall timeout is 60 seconds; polling and event delays are
40–150 milliseconds. No template match is treated as permission to dismiss an
unknown confirmation dialog.

Focused tests run with:

```sh
/path/to/mac-test-venv/bin/python tests/test_mac_image_match.py
lua tests/test_mac_runner_startup.lua
luac -p platform/mac68k/automation/run.lua
```

Woodpecker's separate `mac-automation` workflow runs the synthetic matcher tests
using the pinned Python packages. Linux `ci` and the Retro68 `mac68k` workflow
remain separate gates; none of these CI workflows drives the user's desktop.
