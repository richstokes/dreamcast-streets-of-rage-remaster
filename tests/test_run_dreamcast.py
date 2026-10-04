#!/usr/bin/env python3
"""Offline tests for the real-console runner's network and power boundaries."""
import contextlib
import errno
import importlib.util
import io
from pathlib import Path
import socket
import struct
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('run_dreamcast', ROOT / 'tools/run-dreamcast.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def version_packet(text=b'dcload-ip 2.0.3 using Broadband Adapter\0'):
    return struct.pack('!4sII', b'VERS', 0o400, len(text)) + text


class DreamcastRunnerTests(unittest.TestCase):
    def test_power_refuses_another_plug_without_mutation(self):
        rpc = Mock(return_value={'id': 'shellyplugusg4-another'})
        with self.assertRaisesRegex(RuntimeError, 'Refusing to switch'):
            runner.power_cycle('192.168.1.173', runner.DEFAULT_SHELLY_ID, rpc, Mock())
        self.assertEqual([call.args[1] for call in rpc.call_args_list], ['Shelly.GetDeviceInfo'])

    def test_power_cycle_arms_device_timer_and_confirms_power_on(self):
        rpc = Mock(side_effect=[{'id': runner.DEFAULT_SHELLY_ID}, {'output': True},
                               {'was_on': True}, {'output': False}, {'output': True}])
        sleep = Mock()
        with contextlib.redirect_stdout(io.StringIO()):
            result = runner.power_cycle('192.168.1.173', runner.DEFAULT_SHELLY_ID, rpc, sleep)
        writes = [call for call in rpc.call_args_list if call.args[1] == 'Switch.Set']
        self.assertEqual(len(writes), 1)
        self.assertEqual(writes[0].args[2], {'id': 0, 'on': False, 'toggle_after': 1})
        self.assertTrue(result['after']['output'])
        self.assertEqual(sleep.call_args_list[0].args, (1.5,))

    def test_uncertain_power_request_does_not_retry_off(self):
        rpc = Mock(side_effect=[{'id': runner.DEFAULT_SHELLY_ID}, {'output': True},
                               OSError('lost response'), {'output': True}])
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(RuntimeError, 'cannot confirm a cold boot'):
            runner.power_cycle('192.168.1.173', runner.DEFAULT_SHELLY_ID, rpc, Mock())
        self.assertEqual(sum(call.args[1] == 'Switch.Set' for call in rpc.call_args_list), 1)

    def test_recovery_rechecks_identity_before_power_on(self):
        rpc = Mock(side_effect=[{'id': runner.DEFAULT_SHELLY_ID}, {'output': True},
                               {'was_on': True}] + [{'output': False}] * 6 +
                              [{'id': 'a-different-plug'}])
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(RuntimeError, 'Refusing to switch'):
            runner.power_cycle('192.168.1.173', runner.DEFAULT_SHELLY_ID, rpc, Mock())
        self.assertEqual(sum(call.args[1] == 'Switch.Set' for call in rpc.call_args_list), 1)

    def test_version_requires_complete_dcload_response(self):
        result = runner.parse_version(version_packet())
        self.assertEqual(result['version'], 'dcload-ip 2.0.3 using Broadband Adapter')
        for packet in [b'VERS', version_packet()[:-1], b'EXEC' + version_packet()[4:],
                       version_packet(b'unrelated service\0'), version_packet(b'dcload-ip bad\n\0')]:
            self.assertIsNone(runner.parse_version(packet), repr(packet))

    def test_probe_ignores_wrong_host_port_and_malformed_packet(self):
        sock = Mock()
        sock.__enter__ = Mock(return_value=sock)
        sock.__exit__ = Mock(return_value=False)
        sock.recvfrom.side_effect = [(version_packet(), ('192.168.1.99', 53535)),
                                    (version_packet(), ('192.168.1.123', 80)),
                                    (b'garbage', ('192.168.1.123', 53535)),
                                    (version_packet(), ('192.168.1.123', 53535))]
        with patch.object(runner.socket, 'socket', return_value=sock):
            result = runner.probe_dcload(['192.168.1.123'])
        self.assertEqual(list(result), ['192.168.1.123'])
        sock.sendto.assert_called_once_with(struct.pack('!4sII', b'VERS', 0x020003, 0),
                                            ('192.168.1.123', 53535))

    def test_probe_timeout_is_not_readiness(self):
        sock = Mock()
        sock.__enter__ = Mock(return_value=sock)
        sock.__exit__ = Mock(return_value=False)
        sock.recvfrom.side_effect = socket.timeout()
        with patch.object(runner.socket, 'socket', return_value=sock):
            self.assertEqual(runner.probe_dcload(['192.168.1.123']), {})

    def test_probe_continues_after_unreachable_send_and_async_receive_errors(self):
        sock = Mock()
        sock.__enter__ = Mock(return_value=sock)
        sock.__exit__ = Mock(return_value=False)
        sock.sendto.side_effect = [OSError(errno.EHOSTUNREACH, 'No route to host'), len(runner.VERSION_REQUEST)]
        sock.recvfrom.side_effect = [OSError(errno.EHOSTUNREACH, 'No route to host'),
                                    OSError(errno.ECONNREFUSED, 'Connection refused'),
                                    (version_packet(), ('192.168.1.123', 53535)), socket.timeout()]
        with patch.object(runner.socket, 'socket', return_value=sock):
            result = runner.probe_dcload(['192.168.1.100', '192.168.1.123'])
        self.assertEqual(list(result), ['192.168.1.123'])
        self.assertEqual(sock.sendto.call_count, 2)

    def test_probe_preserves_unexpected_socket_errors(self):
        for operation in ('sendto', 'recvfrom'):
            with self.subTest(operation=operation):
                sock = Mock()
                sock.__enter__ = Mock(return_value=sock)
                sock.__exit__ = Mock(return_value=False)
                getattr(sock, operation).side_effect = OSError(errno.EBADF, 'Bad file descriptor')
                with patch.object(runner.socket, 'socket', return_value=sock), self.assertRaises(OSError) as raised:
                    runner.probe_dcload(['192.168.1.123'])
                self.assertEqual(raised.exception.errno, errno.EBADF)

    def test_probe_repeated_async_errors_remain_time_bounded(self):
        sock = Mock()
        sock.__enter__ = Mock(return_value=sock)
        sock.__exit__ = Mock(return_value=False)
        sock.recvfrom.side_effect = OSError(errno.EHOSTUNREACH, 'No route to host')
        times = iter([0, 0, 0.1, 0.1, 0.5, 0.5, 1.1])
        with patch.object(runner.socket, 'socket', return_value=sock), \
             patch.object(runner.time, 'monotonic', side_effect=lambda: next(times)):
            self.assertEqual(runner.probe_dcload(['192.168.1.123'], timeout=1), {})
        self.assertEqual(sock.recvfrom.call_count, 2)

    def test_discovery_is_bounded_to_local_subnet(self):
        self.assertEqual(str(runner.discovery_network('auto', '192.168.1.123')), '192.168.1.0/24')
        for cidr in ['192.168.0.0/16', '8.8.8.0/24', '::1/128']:
            with self.assertRaises(ValueError):
                runner.discovery_network(cidr, '192.168.1.123')

    def test_replay_json_is_staged_for_pc_filesystem(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / 'input.json'
            source.write_text('{"segments":[{"frames":60,"p1":["START"]}]}')
            result = runner.stage_replay(source, directory)
            data = (directory / 'REPLAY.BIN').read_bytes()
            self.assertEqual(data, b'SRP1' + struct.pack('<IIHH', 1, 60, 128, 0))
            self.assertEqual(result['segments'], 1)
            self.assertEqual(result['sha256'], runner.sha256(directory / 'REPLAY.BIN'))

    def test_replay_rejects_truncation_and_zero_length_segments(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / 'input.bin'
            for data in [b'SRP1' + struct.pack('<I', 1),
                         b'SRP1' + struct.pack('<IIHH', 1, 0, 0, 0), b'not a replay']:
                source.write_bytes(data)
                with self.assertRaisesRegex(RuntimeError, 'Invalid replay'):
                    runner.stage_replay(source, directory)

    def test_replay_rejects_button_bits_and_malformed_state_gates(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / 'input.bin'
            header = b'SRP2' + struct.pack('<I', 1)
            segments = [struct.pack('<IHHHBBI', 60, 1024, 0, 0, 0, 0, 0),
                        struct.pack('<IHHHBBI', 60, 0, 1024, 0, 0, 0, 0),
                        struct.pack('<IHHHBBI', 60, 0, 0, 0, 255, 0, 2),
                        struct.pack('<IHHHBBI', 60, 128, 0, 0, 255, 0, 1),
                        struct.pack('<IHHHBBI', 60, 0, 128, 0, 255, 0, 1),
                        struct.pack('<IHHHBBI', 60, 0, 0, 0, 1, 2, 1)]
            for segment in segments:
                with self.subTest(segment=segment.hex()):
                    source.write_bytes(header + segment)
                    with self.assertRaisesRegex(RuntimeError, 'Invalid replay'):
                        runner.stage_replay(source, directory)
            source.write_bytes(b'SRP1' + struct.pack('<IIHH', 1, 60, 0, 1024))
            with self.assertRaisesRegex(RuntimeError, 'Invalid replay button bits'):
                runner.stage_replay(source, directory)

    def test_replay_accepts_valid_state_gate_and_ignores_disabled_gate_fields(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / 'input.bin'
            source.write_bytes(b'SRP2' + struct.pack('<I', 2) +
                               struct.pack('<IHHHBBI', 60, 0, 0, 65535, 3, 2, 1) +
                               struct.pack('<IHHHBBI', 60, 1023, 1023, 65535, 1, 2, 0))
            result = runner.stage_replay(source, directory)
            self.assertEqual(result['segments'], 2)
            self.assertEqual(result['format'], 'SRP2')

    def test_dry_run_never_touches_network_build_or_files(self):
        with patch.object(runner, 'shelly_rpc') as rpc, \
             patch.object(runner.socket, 'socket') as network, \
             patch.object(runner.subprocess, 'run') as process, \
             patch.object(runner.Path, 'mkdir') as mkdir, \
             patch.object(runner.Path, 'write_text') as write, \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(runner.main(['--dry-run', '--power-cycle', '--discover']), 0)
        for action in (rpc, network, process, mkdir, write):
            action.assert_not_called()


if __name__ == '__main__':
    unittest.main()
