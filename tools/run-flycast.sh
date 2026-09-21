#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
flycast=${FLYCAST_BIN:-"$HOME/.local/share/dreamcast/flycast/Flycast.app/Contents/MacOS/Flycast"}
if [ ! -x "$flycast" ]; then flycast=/Applications/Flycast.app/Contents/MacOS/Flycast; fi
mkdir -p "$root/build/logs"
image=${1:-$root/dist/sor.cdi}
case "$image" in /*) ;; *) image="$PWD/$image" ;; esac
if [ "$(uname -s)" = Darwin ]; then
    # Best effort only: Flycast may override these flags and show its window.
    app=${flycast%/Contents/MacOS/*}
    if [ "$app" = "$flycast" ]; then
        echo 'On macOS FLYCAST_BIN must point inside a Flycast.app bundle.' >&2
        exit 1
    fi
    # Replace only this binary running this exact image, not unrelated emulator sessions.
    python3 - "$flycast" "$image" "$root" <<'PY'
import os,signal,subprocess,sys,time
from pathlib import Path
binary,image,root=sys.argv[1:]
root=Path(root)
images={image,str(root/'build/benchmark-running.cdi')}
images.update(str(root/'dist'/name) for name in ('sor.cdi','sor-test.elf'))
snapshot=root/'build/flycast-run'
images.update(str(p) for p in snapshot.glob('*') if p.suffix in ('.cdi','.elf'))
pids=[]
for line in subprocess.check_output(['ps','-axo','pid=,command='],text=True).splitlines():
    fields=line.strip().split(None,1)
    if len(fields)==2 and fields[1].startswith(binary+' ') and any(fields[1].endswith(' '+p) for p in images):
        try:
            pid=int(fields[0]);os.kill(pid,signal.SIGTERM);pids.append(pid)
        except ProcessLookupError: pass
if pids:
    time.sleep(1)
    for pid in pids:
        try: os.kill(pid,signal.SIGKILL)
        except ProcessLookupError: pass
PY
    # Flycast reads CD sectors on demand. Keep its image immutable while the
    # next build rewrites dist/sor.cdi, and record the launched image's hash.
    image=$(python3 - "$image" "$root" <<'PY'
import hashlib,json,shutil,sys
from pathlib import Path
source=Path(sys.argv[1]);root=Path(sys.argv[2]);target=root/'build/flycast-run'/source.name
target.parent.mkdir(parents=True,exist_ok=True)
if source!=target:shutil.copyfile(source,target)
(root/'build/logs/flycast-run.json').write_text(json.dumps({'source':str(source),'image':str(target),'sha256':hashlib.sha256(target.read_bytes()).hexdigest()},indent=2)+'\n')
print(target)
PY
    )
    # FLYCAST_VSYNC=0 disables host vsync; presentation otherwise blocks while the
    # host display sleeps. Guest timing (all FRAME_STATS/AICA counters) is emulated.
    # FLYCAST_MUTE=0 plays sound on the host; muting sets the output gain only
    # (aica.Volume), so the emulated AICA and its counters are unchanged.
    : > "$root/build/logs/flycast.log"
    : > "$root/build/logs/flycast-errors.log"
    exec /usr/bin/open -g -j -n -a "$app" \
        --stdout "$root/build/logs/flycast.log" --stderr "$root/build/logs/flycast-errors.log" \
        --args -config 'config:Debug.SerialConsoleEnabled=yes' \
        -config "config:aica.Volume=$( [ "${FLYCAST_MUTE:-1}" = 0 ] && echo 100 || echo 0 )" \
        ${FLYCAST_VSYNC:+-config "config:rend.vsync=$( [ "$FLYCAST_VSYNC" = 0 ] && echo no || echo yes )"} "$image"
fi
echo 'Automatic launch is configured only for background macOS app bundles.' >&2
exit 1
