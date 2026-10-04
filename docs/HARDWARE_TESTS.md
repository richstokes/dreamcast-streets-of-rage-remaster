# Retail Dreamcast test sheet

Initial real Dreamcast tests were performed on 2026-10-04 using a GDEMU boot
into dcload-ip and an embedded-ROM development ELF uploaded over the LAN.
Framebuffer captures confirmed severe missing rectangular portions of the title
screen. Increasing the PowerVR object-pointer overflow allocation from 0 to 3
restored the title in the same replay (see Result log). The broader checklist
below remains unverified.

Record console model/region, video cable/mode, controller/VMU models, disc or GDEMU
version, artifact SHA-256, git commit and toolchain revision for each run.

## LAN upload and repeatable cold boot

From the repository root:

```sh
python3 tools/run-dreamcast.py --power-cycle --discover
```

This builds `dist/sor-test.elf` with `tools/build-test-elf.sh`, verifies the
Shelly plug's identity, switches it off for one second and back on, waits 60
seconds for GDEMU/openMenu to launch dcload-ip, then checks the loader and
uploads the ELF. The default Dreamcast address is `192.168.1.123` (discovered
during the initial tests; the earlier address was `192.168.1.150`). The Shelly
is `192.168.1.173`, with verified device ID
`shellyplugusg4-e8f60a7dd06c`. Only the plug matching that ID is switched.

`--discover` permits discovery on the target's private /24 network if an
address changed; an explicit network such as `--discover 192.168.1.0/24` is
also accepted. `--target`, `--shelly`, `--shelly-id`, `--boot-wait` and
`--ready-timeout` override those settings. Loader discovery requests version
information without uploading or executing code. The default uploader is
`~/Dropbox/Games/ROMs/DREAMCAST/dcload-ip/dc-tool-ip`; use `--dc-tool` to change it.
The final upload on 2026-10-04 deliberately supplied the old `.150` address;
discovery found the same dcload/Broadband Adapter at `192.168.1.74` and uploaded
successfully. Do not assume the default address is still current. Unreachable
addresses/ICMP errors are skipped during scanning (including macOS errno 65).

The console and host filesystem remain attached after upload. Each run maps
its own `build/hardware/<UTC timestamp>/` directory to the Dreamcast's `/pc`
filesystem and records `console.log`, `manifest.json`, the ELF and its SHA-256,
build settings and the tracked source diff. Builds also produce `build.log`.
Keep this host process running while the console needs `/pc` or diagnostic
output. Ctrl+C stops the host session; the next upload may need another power
cycle. `--duration SECONDS` bounds the session, including upload time, and
`--dry-run` shows the plan without building or contacting the network.

To build separately and upload that exact ELF:

```sh
tools/build-test-elf.sh
python3 tools/run-dreamcast.py --no-build --power-cycle --discover
```

The build helper accepts a ROM path, or uses `SOR_ROM` and then the repository's
local ROM default. It embeds the available `SOR_ART` package (default
`build/art/SORART.PAK`) and starts in enhanced graphics unless
`SOR_ENHANCED=0`. `--elf PATH` selects an existing self-contained ELF and also
skips building. Build-time environment changes require rebuilding; they do not
change an existing ELF. All ROM-derived binaries, assets and captures stay in
`build/` or `dist/` and must not be committed.

## Scripted input and real framebuffer captures

`--replay` accepts a repository scenario JSON or compiled SRP1/SRP2 replay and
serves it as `/pc/REPLAY.BIN`. The game tries `/cd/REPLAY.BIN` first, so a valid
replay on the mounted disc takes precedence over the host replay. Omit
`--replay` for controller input and ensure the disc does not supply a replay.

For sparse screenshots spanning the title and the boot-movement scenario:

```sh
SOR_HW_CAPTURE=1 SOR_HW_CAPTURE_FIRST=480 \
SOR_HW_CAPTURE_INTERVAL=450 SOR_HW_CAPTURE_COUNT=4 \
  python3 tools/run-dreamcast.py --power-cycle --discover \
  --replay reference/scenarios/boot-movement.json
```

`SOR_HW_CAPTURE` defaults to 0. With it enabled, the defaults are first frame
600, interval 600 and four captures; the count may be 1-16. The first frame and
interval must be positive integers. Each capture drains the submitted PVR work,
waits for the displayed framebuffer, and writes a 640x480 RGB565-to-RGB PPM as
`/pc/frame-NNNNNN.ppm`. No video-capture device is needed to inspect these
framebuffers; the physical television's signal and scaling are outside the
capture. `HW_CAPTURE` log lines record the game mode, previous presented frame,
vertex-buffer usage and output path.

Captures pause the game while transferring pixels and can cause audio underruns.
Frame-time, missed-refresh and audio measurements from a capture run are not
performance results. Rebuild with `SOR_HW_CAPTURE=0` and repeat the same replay
for performance measurements. The host fileserver must stay attached until the
last screenshot has finished writing.

## Boot and media

- [ ] Fresh source + pinned tools + locally supplied ROM rebuild successfully.
- [ ] CDI boots on a retail console capable of MIL-CD, with no extra RAM.
- [ ] GDEMU reads all assets from the image's ISO9660 `/cd` filesystem.
- [ ] VGA and NTSC television output show the same 4:3 gameplay area.
- [ ] Missing/wrong game data gives a visible useful error; no silent hang.

## Controllers / VMU

- [ ] Port A and B connect, disconnect and reconnect safely.
- [ ] Two-player join, independent buttons, friendly fire, grabs/throws work.
- [ ] The game runs with no VMU and with any VMU present (nothing is saved).

## Fidelity (verified in emulation for Round 1 only; FIDELITY_GATE.md)

- [ ] Compare recorded movement/jump/combo/grab/throw/special scenarios with Genesis.
- [ ] Compare damage/invulnerability/knockdown/recovery timings.
- [ ] Verify all eight rounds, every boss, both ending branches and two-player progression.
- [ ] Verify scores, extra lives, continues and records.
- [ ] Original and enhanced graphics use identical game-state traces.
- [ ] Every replacement animation has correct pivots, feet and active-frame alignment.

## Performance / endurance

- [ ] Log real hardware presentation and simulation frame times separately.
- [ ] Report p50/p95/p99/max and missed 16.67 ms deadlines; don't use average FPS alone.
- [ ] Measure code/data/BSS, heap peak, each stack's high-water mark and free main RAM.
- [ ] Measure PVR allocation peaks, upload bytes/time and vertex-list overflow.
- [ ] Measure AICA memory use and stream underruns; listen for clicks and missing channels.
- [ ] Stress two players, maximum enemies, police effects and boss encounters.
- [ ] Repeat stage loads 50 times; verify allocations return to the same baseline.
- [ ] Finish an uninterrupted full playthrough and both endings.

## Result log

| Date / tester | Console + media | Commit / artifact hash | Result + evidence | Issue |
| --- | --- | --- | --- | --- |
| 2026-10-04 / local hardware session | Retail Dreamcast, television, GDEMU/openMenu launching dcload-ip; model/region not recorded | ELF hashes and tracked source diffs in the evidence directories | Same replay, frame 480: `build/hardware/capture-before/frame-000480.ppm` has large missing rectangular areas; `build/hardware/capture-opb/frame-000480.ppm` restores title characters, logo and text after changing `opb_overflow_count` from 0 to 3. This comparison predates synchronization changes and costs 337.5 KiB more PVR memory. | Title corruption reproduced and corrected; remaining checklist open |

The final diagnostic run is `build/hardware/20261004T160920.874170Z/`:
ELF SHA-256 `1557597b2f84c1ecd80729356214a40cf29a0280b33b068d69e02280cc0d39a2`
(base commit `4ea0b82`, with the run's recorded source patch).
The workflow verified the plug, cold-booted the console, identified
`dcload-ip 2.0.3 using Broadband Adapter (HIT-0400)` at `192.168.1.123`,
uploaded the ELF and served the boot-movement replay. The console reported
640x480 VGA mode, a controller, VMU and Puru Puru Pack on port A. Model/region,
physical video conversion and GDEMU/openMenu versions were not established.

With OPB reserve and render-completion waits before actual texture/palette
updates, captures 480, 930, 1380 and 1830 match the OPB-only run byte for byte.
The title and Round 1 HUD have their missing sections restored. Observed peak
vertex use was 204,120 bytes of the 1 MiB vertex buffer, distinguishing the
OPB shortage from vertex-buffer capacity. Round 1 with Adam retained 582,440
bytes of free VRAM after loading art. Other stages and crowded scenes still
need testing.

An unconditional per-frame render-completion wait was rejected: before the
first capture, it produced 35 `SLOW` records in frames 400-479, versus none
in the OPB-only run. Waiting only before actual writes reduced that to 3.
These observations detect that regression; they are not a full performance
benchmark. Capture transfers and console log drains disturb timing/audio,
and the runs reported audio underruns. Do not infer stable 60 Hz or clean
audio on hardware from these visual checks.

The final playable run (`build/hardware/20261004T161457.018691Z/`) was uploaded
to the discovered `.74` address without replay or capture. ELF SHA-256:
`6442761a8dfda5adf22747753f5c51846e7d7fa37591a3f9f514d5192089654f`.
It advanced through frame 1800 with enhanced graphics enabled. This run also
reported missed refreshes and AICA underruns with capture disabled; live LAN
log-drain overhead and synthesis spikes require separate measurement. The
graphics correction does not establish full-speed, dropout-free hardware play.

## PowerVR renderer checks

- [ ] Compare the PowerVR renderer with the software renderer (a build with
      `SOR_SOFTWARE_TOGGLE=1`; Dreamcast B toggles it) in the same scene.
- [ ] Check foreground poles, player overlap, sprite limits, palette flashes and window HUD.
- [ ] Check two-player scrolling and every stage's line-scroll/environment effects.
- [ ] Record fallback cases (shadow/highlight, interlace, two-cell vertical scrolling).
- [ ] Run the replay procedure in TOOLCHAIN.md and capture hardware serial output.
- [ ] Compare FRAME_STATS `vblanks`/`flips`; record missed refreshes separately from
      CPU-loop p50/p95/p99/worst (loop intervals can vary without missing a refresh).
- [ ] Collect GPU_STATS phase timings and partial sprite upload bytes.
- [ ] Check disappearing/moving sprites leave no stale rows and background tiles
      becoming visible invalidate cached commands correctly.
- [ ] Confirm new frame/list allocation and 1.75 MiB explicit texture allocation fit retail VRAM.

Use a CDI built without SOR_REPLAY for ordinary disc play. The embedded-ROM ELF
has been uploaded and run on retail hardware through dcload-ip; that test does
not establish CDI boot or GDEMU game-asset loading from `/cd`.

## Original audio

Default builds play original audio (AUDIO.md). Flycast holds 60 Hz in the
recorded replays; retail performance and AICA output are unverified. Record the
serial `AICA` lines (level, underruns, dropped/repeated frames) and any
`AICA_UNDERRUN` frames.

- [ ] Record AICA available memory, queued frames, underruns and overruns from serial.
- [ ] Compare original FM melody, PSG effects and sampled drums/voices against Genesis.
- [ ] Check stereo channels, mute/pause, music changes and repeated stage loads.
- [ ] Capture frame times with audio enabled and disabled using the same replay.
- [ ] Run for 30 minutes and check stream clock drift.

## Optional AICA stems (`SOR_DAC_AICA=1`, NATIVE_DAC.md)

- [ ] Compare combined and four-channel output using the same replay; listen for
      stereo phase, changed clipping, missing samples and drum/voice timing.
- [ ] Record AICA_DAC underruns, resyncs, overruns and transfer mean/max timings.
- [ ] Exercise pause, resets and repeated loads; check synchronized recovery after stalls.
- [ ] Measure stack/heap peaks and sound RAM usage on unmodified retail hardware.
