#!/usr/bin/env python3
"""Build, optionally cold boot, and run the embedded ELF on a real Dreamcast.

Keep this process running: dc-tool serves console output and /pc file requests.
All run files (including the ROM-containing ELF) stay under build/hardware/.
The Shelly is only switched after its configured device identity is verified.
"""
import argparse
import concurrent.futures
from datetime import datetime, timezone
import errno
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import pty
import select
import shlex
import shutil
import signal
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TOOL = Path.home() / 'Dropbox/Games/ROMs/DREAMCAST/dcload-ip/dc-tool-ip'
DEFAULT_SHELLY_ID = 'shellyplugusg4-e8f60a7dd06c'
# Protocol: KallistiOS/dcload-ip target-src/dcload/commands.{c,h}, cmd_version.
# V2 accepts an ephemeral sender port. Advertise v2 so its reply preserves it.
VERSION_REQUEST = struct.pack('!4sII', b'VERS', 0x020003, 0)
DCLOAD_PORT = 53535
# BSD/macOS can report an earlier host's asynchronous ICMP error on either
# sendto or recvfrom of a shared UDP socket during subnet discovery.
UNREACHABLE_ERRORS = {errno.EHOSTUNREACH, errno.ENETUNREACH, errno.ECONNREFUSED,
                      errno.ECONNRESET, getattr(errno, 'EHOSTDOWN', errno.EHOSTUNREACH)}


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def log(message):
    print(message, flush=True)


def ipv4(value):
    try:
        return str(ipaddress.IPv4Address(value))
    except ipaddress.AddressValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


def nonnegative(value):
    number = float(value)
    if not 0 <= number < float('inf'):
        raise argparse.ArgumentTypeError('must be a finite nonnegative number')
    return number


def discovery_network(value, target):
    network = ipaddress.ip_network(target + '/24' if value == 'auto' else value, strict=False)
    if network.version != 4 or network.prefixlen < 24 or not network.is_private:
        raise ValueError('discovery must be a private IPv4 /24 or smaller network')
    return network


def shelly_rpc(host, method, params=None, timeout=3):
    body = {'id': 1, 'method': method}
    if params is not None:
        body['params'] = params
    request = urllib.request.Request('http://' + host + '/rpc',
                                     data=json.dumps(body).encode(),
                                     headers={'Content-Type': 'application/json'})
    # A local LAN device must not be routed through an HTTP proxy.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request, timeout=timeout) as response:
        result = json.load(response)
    if 'error' in result:
        raise RuntimeError(f'Shelly {method}: {result["error"]}')
    if not isinstance(result.get('result'), dict):
        raise RuntimeError(f'Invalid Shelly {method} response')
    return result['result']


def verify_shelly(host, expected_id, rpc=shelly_rpc):
    info = rpc(host, 'Shelly.GetDeviceInfo')
    if info.get('id') != expected_id:
        raise RuntimeError(f'Refusing to switch {host}: expected {expected_id}, got {info.get("id")!r}')
    return info


def discover_shelly(network, expected_id):
    def identify(host):
        try:
            info = shelly_rpc(str(host), 'Shelly.GetDeviceInfo', timeout=0.7)
            return str(host) if info.get('id') == expected_id else None
        except (OSError, ValueError, RuntimeError):
            return None
    log(f'Looking for Shelly {expected_id} on {network} (read-only).')
    with concurrent.futures.ThreadPoolExecutor(max_workers=24) as pool:
        matches = [host for host in pool.map(identify, network.hosts()) if host]
    if len(matches) != 1:
        raise RuntimeError(f'Expected one matching Shelly on {network}; found {matches}')
    return matches[0]


def power_cycle(host, expected_id, rpc=shelly_rpc, sleep=time.sleep):
    info = verify_shelly(host, expected_id, rpc)
    before = rpc(host, 'Switch.GetStatus', {'id': 0})
    log(f'Power cycling verified {info["id"]} at {host}; automatic power-on in 1 second.')
    request_error = None
    try:
        # Device-side restoration survives an interrupted host or lost response.
        rpc(host, 'Switch.Set', {'id': 0, 'on': False, 'toggle_after': 1})
    except (OSError, RuntimeError, ValueError) as error:
        request_error = str(error)
    sleep(1.5)
    last_error = None
    after = None
    for _ in range(6):
        try:
            after = rpc(host, 'Switch.GetStatus', {'id': 0})
            if after.get('output') is True:
                break
        except (OSError, RuntimeError, ValueError) as error:
            last_error = str(error)
        sleep(0.5)
    if not after or after.get('output') is not True:
        # Restore only the same identified device, even if DHCP changed meanwhile.
        verify_shelly(host, expected_id, rpc)
        rpc(host, 'Switch.Set', {'id': 0, 'on': True})
        after = rpc(host, 'Switch.GetStatus', {'id': 0})
        if after.get('output') is not True:
            raise RuntimeError(f'Cannot confirm Dreamcast power on: {last_error or after}')
    log('Shelly confirms Dreamcast power is on.')
    if request_error:
        raise RuntimeError('Power is on, but the off request was not acknowledged; '
                           f'cannot confirm a cold boot: {request_error}')
    return {'host': host, 'device': info, 'before': before, 'after': after}


def parse_version(packet):
    if len(packet) < 13:
        return None
    command, adapter, size = struct.unpack('!4sII', packet[:12])
    if command != b'VERS' or not 1 <= size <= len(packet) - 12:
        return None
    payload = packet[12:12 + size]
    if not payload.startswith(b'dcload-ip ') or not payload.endswith(b'\0'):
        return None
    try:
        version = payload[:-1].decode('ascii')
    except UnicodeDecodeError:
        return None
    if not all(character.isprintable() for character in version):
        return None
    return {'version': version, 'adapter': adapter}


def probe_dcload(hosts, timeout=1.0):
    """Only request version data; never upload, execute, or reset in discovery."""
    allowed = {str(host) for host in hosts}
    found = {}
    deadline = time.monotonic() + timeout
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('', 0))
        for host in sorted(allowed):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(remaining)
            try:
                sock.sendto(VERSION_REQUEST, (host, DCLOAD_PORT))
            except socket.timeout:
                break
            except OSError as error:
                if error.errno not in UNREACHABLE_ERRORS:
                    raise
        while time.monotonic() < deadline:
            sock.settimeout(max(0.001, deadline - time.monotonic()))
            try:
                packet, source = sock.recvfrom(2048)
            except socket.timeout:
                break
            except OSError as error:
                if error.errno not in UNREACHABLE_ERRORS:
                    raise
                continue
            if source[0] not in allowed or source[1] != DCLOAD_PORT:
                continue
            version = parse_version(packet)
            if version:
                found[source[0]] = version
                if len(found) == len(allowed):
                    break
    return found


def wait_dcload(target, timeout, network=None):
    deadline = time.monotonic() + timeout
    next_scan = 0
    log(f'Waiting up to {timeout:g}s for dcload-ip at {target}.')
    while True:
        remaining = max(0.05, deadline - time.monotonic())
        found = probe_dcload([target], min(1.0, remaining))
        if target in found:
            return target, found[target]
        if network and time.monotonic() >= next_scan:
            log(f'Looking for dcload-ip on {network}.')
            found = probe_dcload(network.hosts(), min(1.5, max(0.05, deadline - time.monotonic())))
            if len(found) > 1:
                raise RuntimeError(f'Multiple dcload consoles found: {list(found)}; select one with --target')
            if found:
                return next(iter(found.items()))
            next_scan = time.monotonic() + 15
        if time.monotonic() >= deadline:
            raise RuntimeError(f'dcload-ip did not answer at {target}; use --power-cycle or --discover')
        time.sleep(min(2.0, deadline - time.monotonic()))


def git_output(*args, cwd=ROOT):
    result = subprocess.run(['git', '-C', str(cwd), *args], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return result.stdout.strip() if result.returncode == 0 else None


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def stage_replay(source, run_dir):
    """Compile repository replay JSON, or copy a checked SRP1/SRP2 binary."""
    source = source.expanduser().resolve()
    target = run_dir / 'REPLAY.BIN'
    if source.suffix.lower() == '.json':
        subprocess.run([sys.executable, str(ROOT / 'tools/replay.py'), str(source), str(target)], check=True)
    else:
        shutil.copy2(source, target)
    data = target.read_bytes()
    if len(data) < 8 or data[:4] not in (b'SRP1', b'SRP2'):
        raise RuntimeError(f'Invalid replay header: {source}')
    count, = struct.unpack('<I', data[4:8])
    stride = 8 if data[:4] == b'SRP1' else 16
    if not 1 <= count <= 4096 or len(data) != 8 + count * stride:
        raise RuntimeError(f'Invalid replay segment count or length: {source}')
    for offset in range(8, len(data), stride):
        frames, p1, p2 = struct.unpack_from('<IHH', data, offset)
        if not 1 <= frames <= 60000:
            raise RuntimeError(f'Invalid replay frame count: {source}')
        if p1 > 1023 or p2 > 1023:
            raise RuntimeError(f'Invalid replay button bits: {source}')
        if stride == 16:
            _, mask, value, flags = struct.unpack_from('<HBBI', data, offset + 8)
            if flags > 1 or (flags and (p1 or p2 or value & mask != value)):
                raise RuntimeError(f'Invalid replay state gate: {source}')
    return {'source': str(source), 'path': str(target), 'sha256': sha256(target),
            'segments': count, 'format': data[:4].decode()}


def run_console(command, logfile, duration=0):
    """PTY makes libc line-buffer console output even when this tool is piped."""
    master, slave = pty.openpty()
    process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL,
                               stdout=slave, stderr=slave, start_new_session=True)
    os.close(slave)
    deadline = time.monotonic() + duration if duration else None
    reason = 'exit'
    try:
        with logfile.open('wb') as output:
            while True:
                if deadline is not None and time.monotonic() >= deadline:
                    reason = 'duration'
                    break
                ready, _, _ = select.select([master], [], [], 0.25)
                if ready:
                    try:
                        data = os.read(master, 65536)
                    except OSError as error:
                        if error.errno != errno.EIO:
                            raise
                        break
                    if not data:
                        break
                    output.write(data)
                    output.flush()
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                elif process.poll() is not None:
                    break
    except KeyboardInterrupt:
        reason = 'interrupt'
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGINT)
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        os.close(master)
    return {'returncode': process.returncode, 'stop_reason': reason}


def parser():
    result = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    result.add_argument('--target', type=ipv4, default='192.168.1.123')
    result.add_argument('--dc-tool', type=Path, default=DEFAULT_TOOL)
    result.add_argument('--rom', type=Path, help='ROM path (otherwise SOR_ROM or repository default)')
    result.add_argument('--no-build', action='store_true', help='use dist/sor-test.elf as it exists')
    result.add_argument('--elf', type=Path, help='use this existing self-contained ELF; implies --no-build')
    result.add_argument('--replay', type=Path, help='replay JSON or SRP1/SRP2 binary to serve as /pc/REPLAY.BIN')
    result.add_argument('--power-cycle', action='store_true', help='cold boot using the identified Shelly plug')
    result.add_argument('--shelly', type=ipv4, default='192.168.1.173')
    result.add_argument('--shelly-id', default=DEFAULT_SHELLY_ID, help='exact device ID allowed to switch')
    result.add_argument('--boot-wait', type=nonnegative, default=60, help='seconds after power-on before probing (default 60)')
    result.add_argument('--ready-timeout', type=nonnegative, default=60, help='seconds allowed for loader readiness after boot wait')
    result.add_argument('--discover', nargs='?', const='auto', metavar='CIDR', help='allow bounded private /24 discovery if an IP changed')
    result.add_argument('--duration', type=nonnegative, default=0, help='stop dc-tool after this many seconds, including upload; 0 stays attached')
    result.add_argument('--fast', action='store_true', help='dc-tool -f (optional faster, less reliable upload)')
    result.add_argument('--dry-run', action='store_true', help='show plan; no build, network, power, or file writes')
    return result


def main(argv=None):
    args = parser().parse_args(argv)
    build = not (args.no_build or args.elf)
    source = (args.elf or ROOT / 'dist/sor-test.elf').expanduser().resolve()
    tool = args.dc_tool.expanduser().resolve()
    network = discovery_network(args.discover, args.target) if args.discover else None
    command = [str(tool), '-t', args.target, '-m', '<run directory>']
    if args.fast:
        command.append('-f')
    command += ['-x', '<run directory>/sor-test.elf']
    build_command = [str(ROOT / 'tools/build-test-elf.sh')]
    if args.rom:
        build_command.append(str(args.rom.expanduser().resolve()))
    if args.dry_run:
        print(json.dumps({'build': build_command if build else None, 'source_elf': str(source),
                          'replay': str(args.replay) if args.replay else None,
                          'power_cycle': args.power_cycle, 'shelly': args.shelly,
                          'required_shelly_id': args.shelly_id,
                          'boot_wait': args.boot_wait if args.power_cycle else 0,
                          'readiness_timeout': args.ready_timeout,
                          'discovery': str(network) if network else None,
                          'command': command, 'duration': args.duration,
                          'logs': str(ROOT / 'build/hardware/<UTC timestamp>/'),
                          'environment': {k: v for k, v in os.environ.items() if k.startswith('SOR_')}}, indent=2))
        return 0
    if not tool.is_file() or not os.access(tool, os.X_OK):
        raise RuntimeError(f'dc-tool-ip is not executable: {tool}')
    if not build and not source.is_file():
        raise RuntimeError(f'ELF does not exist: {source}')
    if args.replay and not args.replay.expanduser().is_file():
        raise RuntimeError(f'Replay does not exist: {args.replay}')
    run_dir = ROOT / 'build/hardware' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    run_dir.mkdir(parents=True)
    log(f'Run files: {run_dir}')
    manifest_path = run_dir / 'manifest.json'
    manifest = {'started_at': timestamp(), 'source_elf': str(source),
                'built': build, 'git_commit': git_output('rev-parse', 'HEAD'),
                'git_status': git_output('status', '--short'),
                'environment': {k: v for k, v in os.environ.items() if k.startswith('SOR_')},
                'options': {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
                'dc_tool': str(tool), 'dc_tool_sha256': sha256(tool), 'status': 'preparing'}
    def save():
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    save()
    try:
        diff = git_output('diff', 'HEAD', '--binary')
        if diff:
            (run_dir / 'source.patch').write_text(diff + '\n')
        if build:
            log('Building the self-contained Dreamcast ELF.')
            with (run_dir / 'build.log').open('wb') as build_log:
                subprocess.run(build_command, cwd=ROOT, stdout=build_log, stderr=subprocess.STDOUT, check=True)
        with source.open('rb') as handle:
            header = handle.read(20)
        if len(header) < 20 or header[:6] != b'\x7fELF\x01\x01' or struct.unpack('<H', header[18:20])[0] != 42:
            raise RuntimeError(f'Expected a little-endian SH-4 ELF: {source}')
        elf = run_dir / 'sor-test.elf'
        shutil.copy2(source, elf)
        manifest.update(elf=str(elf), elf_sha256=sha256(elf), elf_bytes=elf.stat().st_size)
        if args.replay:
            manifest['replay'] = stage_replay(args.replay, run_dir)
        config = ROOT / 'build/native/upstream/sor_audio_config.hpp'
        if config.exists():
            shutil.copy2(config, run_dir / config.name)
            manifest['build_config'] = config.read_text()
            manifest['build_config_provenance'] = ('generated by this build' if build else
                                                   'current workspace; may not match existing ELF')
        save()
        if args.power_cycle:
            shelly = args.shelly
            try:
                verify_shelly(shelly, args.shelly_id)
            except (OSError, RuntimeError, ValueError):
                if not network:
                    raise
                shelly = discover_shelly(network, args.shelly_id)
            manifest['power_cycle'] = power_cycle(shelly, args.shelly_id)
            save()
            log(f'Waiting {args.boot_wait:g}s for GDEMU/openMenu to boot dcload-ip.')
            time.sleep(args.boot_wait)
        target, version = wait_dcload(args.target, args.ready_timeout, network)
        log(f'{target}: {version["version"]}')
        command[command.index('-t') + 1] = target
        command[command.index('-m') + 1] = str(run_dir)
        command[-1] = str(elf)
        manifest.update(target=target, loader=version, command=command, status='running',
                        console_started_at=timestamp())
        save()
        log(shlex.join(command))
        log('Console/fileserver stays attached. Ctrl+C stops the host session; next run may need --power-cycle.')
        manifest['console'] = run_console(command, run_dir / 'console.log', args.duration)
        stopped = manifest['console']['stop_reason']
        code = manifest['console']['returncode']
        manifest['status'] = 'stopped' if stopped != 'exit' else ('finished' if code == 0 else 'failed')
        return (128 - code if code < 0 else code) if stopped == 'exit' else 0
    except KeyboardInterrupt:
        manifest['status'] = 'interrupted'
        return 130
    except Exception as error:
        manifest.update(status='failed', error=str(error))
        raise
    finally:
        manifest['ended_at'] = timestamp()
        save()
        log(f'Run manifest: {manifest_path}')


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(f'run-dreamcast: {error}')
