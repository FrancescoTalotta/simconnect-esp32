# SPDX-License-Identifier: LGPL-3.0-or-later
"""Exercise the actual C client over localhost, not a Python reimplementation.

Fixtures are synthetic packets authored for these tests, using the layouts
documented in the local prototype and pinned node-simconnect reference (NOTICE).
No simulator captures or SDK binaries are bundled.
"""
import ctypes as C
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
OK, TIMEOUT, IO, PROTOCOL, INVALID, QUIT, EXCEPTION = 0, -1, -2, -3, -4, -6, -7


class Config(C.Structure):
    _fields_ = [("host", C.c_char_p), ("port", C.c_char_p), ("name", C.c_char_p),
                ("protocol", C.c_uint32), ("timeout_ms", C.c_uint32)]


class Item(C.Structure):
    _fields_ = [("name", C.c_char_p), ("units", C.c_char_p),
                ("type", C.c_uint32), ("epsilon", C.c_float)]


class Value(C.Union):
    _fields_ = [("number", C.c_double), ("string", C.c_char * 256)]


class Message(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in (
        "type", "id", "request_id", "definition_id", "object_id", "flags",
        "count", "exception", "send_id", "index", "event_id", "event_data")]
    _fields_ += [("data", C.c_void_p), ("data_size", C.c_size_t)]


def frame(kind, payload=b""):
    return struct.pack("<III", 12 + len(payload), 5, kind) + payload


def data_frame(values, req=7, definition=9, count=1, flags=0, obj=0):
    return frame(8, struct.pack("<7I", req, obj, definition, flags, 1, 1, count) + values)


def receive_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError("client closed socket")
        data += chunk
    return data


def receive_packet(sock):
    size = receive_exact(sock, 4)
    return size + receive_exact(sock, struct.unpack("<I", size)[0] - 4)


class Peer:
    def __init__(self, handler, opening=None):
        self.handler = handler
        self.opening = frame(2, b"Test simulator".ljust(296, b"\0")) if opening is None else opening
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(2)
        self.port = str(self.listener.getsockname()[1]).encode()
        self.errors = []

    def __enter__(self):
        def run():
            try:
                conn, _ = self.listener.accept()
                with conn:
                    conn.settimeout(2)
                    opening = receive_packet(conn)
                    assert len(opening) == 296
                    assert struct.unpack_from("<I", opening, 8)[0] == 0xf0000001
                    conn.sendall(self.opening)
                    self.handler(conn, opening)
            except Exception as error:
                self.errors.append(error)
        self.thread = threading.Thread(target=run, daemon=True)
        self.thread.start()
        return self

    def __exit__(self, *args):
        self.thread.join(3)
        self.listener.close()
        if self.thread.is_alive():
            raise AssertionError("mock peer did not terminate")
        if not args[0] and self.errors:
            raise self.errors[0]


class ClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "tests" / "build"
        build.mkdir(exist_ok=True)
        binary = build / ("libsimconnect.dylib" if sys.platform == "darwin" else "libsimconnect.so")
        subprocess.run([
            "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
            "-pedantic", "-g", "-fPIC", "-shared", "-fsanitize=undefined",
            "-fno-sanitize-recover=all",
            "-I", str(ROOT / "include"), str(ROOT / "src" / "simconnect_client.c"),
            "-o", str(binary),
        ], check=True)
        cls.lib = lib = C.CDLL(str(binary))
        signatures = {
            "simc_open": [C.POINTER(C.c_void_p), C.POINTER(Config)],
            "simc_close": [C.c_void_p],
            "simc_poll": [C.c_void_p, C.c_uint32, C.POINTER(Message)],
            "simc_add_definition": [C.c_void_p, C.c_uint32, C.POINTER(Item), C.c_size_t],
            "simc_clear_definition": [C.c_void_p, C.c_uint32],
            "simc_request_data": [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32],
            "simc_set_double": [C.c_void_p, C.c_uint32, C.c_double, C.POINTER(C.c_uint32)],
            "simc_map_event": [C.c_void_p, C.c_uint32, C.c_char_p],
            "simc_send_event": [C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)],
            "simc_subscribe_system_event": [C.c_void_p, C.c_uint32, C.c_char_p],
            "simc_decode_data": [C.POINTER(Message), C.POINTER(Item), C.c_size_t, C.POINTER(Value)],
        }
        for name, arguments in signatures.items():
            function = getattr(lib, name)
            function.argtypes = arguments
            function.restype = None if name == "simc_close" else C.c_int

    def open_client(self, peer, timeout=500, protocol=5):
        session = C.c_void_p()
        config = Config(b"127.0.0.1", peer.port, b"Test", protocol, timeout)
        result = self.lib.simc_open(C.byref(session), C.byref(config))
        self.assertEqual(result, OK)
        self.addCleanup(self.lib.simc_close, session)
        return session

    def poll(self, session, ms=200):
        message = Message()
        result = self.lib.simc_poll(session, ms, C.byref(message))
        return result, message

    def test_header_fragments_survive_timeouts(self):
        for split in range(1, 12):
            with self.subTest(split=split):
                packet = data_frame(struct.pack("<d", 42.5))

                def handler(conn, _):
                    conn.sendall(packet[:split])
                    time.sleep(.07)
                    conn.sendall(packet[split:])

                with Peer(handler) as peer:
                    session = self.open_client(peer)
                    self.assertEqual(self.poll(session, 20)[0], TIMEOUT)
                    result, message = self.poll(session)
                    self.assertEqual(result, OK)
                    self.assertEqual((message.request_id, message.definition_id), (7, 9))
                    value = Value()
                    item = Item(b"PLANE ALTITUDE", b"feet", 4, 0)
                    self.assertEqual(self.lib.simc_decode_data(C.byref(message), C.byref(item),
                                                             1, C.byref(value)), OK)
                    self.assertEqual(value.number, 42.5)

    def test_payload_fragment_survives_timeout(self):
        packet = data_frame(struct.pack("<d", -123.5))

        def handler(conn, _):
            conn.sendall(packet[:44])
            time.sleep(.07)
            conn.sendall(packet[44:])

        with Peer(handler) as peer:
            session = self.open_client(peer)
            self.assertEqual(self.poll(session, 20)[0], TIMEOUT)
            self.assertEqual(self.poll(session)[0], OK)

    def test_coalesced_frames_and_exception_correlation(self):
        def handler(conn, _):
            conn.sendall(frame(1, struct.pack("<III", 7, 123, 2)) +
                         data_frame(struct.pack("<d", 1)))

        with Peer(handler) as peer:
            session = self.open_client(peer)
            result, message = self.poll(session)
            self.assertEqual(result, OK)
            self.assertEqual((message.type, message.exception, message.send_id, message.index),
                             (2, 7, 123, 2))
            self.assertEqual(self.poll(session)[1].type, 1)

    def test_malformed_and_oversized_lengths_close_session(self):
        for size in (0, 4, 11, 8193, 0xffffffff):
            with self.subTest(size=size):
                def handler(conn, _):
                    conn.sendall(struct.pack("<III", size, 5, 8))
                with Peer(handler) as peer:
                    session = self.open_client(peer)
                    self.assertEqual(self.poll(session)[0], PROTOCOL)
                    self.assertEqual(self.poll(session)[0], IO)

    def test_truncated_data_and_exception(self):
        for packet in (frame(8, bytes(27)), frame(1, bytes(8)), frame(4, bytes(4)),
                       frame(6, bytes(8))):
            with self.subTest(packet=packet):
                with Peer(lambda conn, _: conn.sendall(packet)) as peer:
                    session = self.open_client(peer)
                    self.assertEqual(self.poll(session)[0], PROTOCOL)

    def test_stalled_partial_frame_has_absolute_deadline(self):
        def handler(conn, _):
            conn.sendall(b"\x30")
            time.sleep(.3)
        with Peer(handler) as peer:
            session = self.open_client(peer, timeout=100)
            result = TIMEOUT
            while result == TIMEOUT:
                result, _ = self.poll(session, 20)
            self.assertEqual(result, PROTOCOL)

    def test_eof_mid_frame_is_fatal(self):
        with Peer(lambda conn, _: conn.sendall(data_frame(bytes(8))[:20])) as peer:
            session = self.open_client(peer)
            self.assertEqual(self.poll(session)[0], IO)

    def test_quit_is_fatal(self):
        with Peer(lambda conn, _: conn.sendall(frame(3))) as peer:
            session = self.open_client(peer)
            self.assertEqual(self.poll(session)[0], QUIT)
            self.assertEqual(self.poll(session)[0], IO)

    def test_unknown_frame_does_not_break_following_data(self):
        with Peer(lambda conn, _: conn.sendall(frame(999, bytes(5)) +
                                             data_frame(bytes(8)))) as peer:
            session = self.open_client(peer)
            self.assertEqual(self.poll(session)[1].type, 0)
            self.assertEqual(self.poll(session)[1].type, 1)

    def test_open_rejection_and_short_open(self):
        for opening, expected in ((frame(1, struct.pack("<3I", 4, 0, 0)), EXCEPTION),
                                  (frame(2, b"too short"), PROTOCOL)):
            with self.subTest(expected=expected):
                with Peer(lambda *_: None, opening=opening) as peer:
                    session = C.c_void_p()
                    config = Config(b"127.0.0.1", peer.port, b"Test", 5, 500)
                    self.assertEqual(self.lib.simc_open(C.byref(session), C.byref(config)), expected)
                    self.assertFalse(session.value)

    def test_open_aliases(self):
        for protocol, alias, version in ((4, b"\0XSF", (10, 0, 61259, 0)),
                                         (5, b"\0HK\0", (11, 0, 62651, 3)),
                                         (6, b"\0RS\0", (12, 2, 282174, 999))):
            def handler(conn, opening):
                self.assertEqual(struct.unpack_from("<I", opening, 4)[0], protocol)
                self.assertEqual(opening[276:280], alias)
                self.assertEqual(struct.unpack_from("<4I", opening, 280), version)
            with Peer(handler) as peer:
                self.open_client(peer, protocol=protocol)

    def test_definition_request_write_and_event_serialization(self):
        def handler(conn, _):
            packets = [receive_packet(conn) for _ in range(8)]
            self.assertEqual([struct.unpack_from("<I", p, 12)[0] for p in packets],
                             list(range(1, 9)))
            definition, request, write, mapping, event, system, cancel, clear = packets
            self.assertEqual(len(definition), 544)
            self.assertEqual(struct.unpack_from("<I", definition, 8)[0], 0xf000000c)
            self.assertEqual(definition[20:24], b"L:X\0")
            self.assertEqual(struct.unpack_from("<IfI", definition, 532), (4, .25, 0))
            self.assertEqual(struct.unpack_from("<8I", request, 16), (7, 9, 0, 4, 1, 0, 0, 0))
            self.assertEqual(struct.unpack_from("<5Id", write, 16), (9, 0, 0, 1, 8, 1.0))
            self.assertEqual(struct.unpack_from("<I", mapping, 8)[0], 0xf0000004)
            self.assertEqual(struct.unpack_from("<9I", event, 16), (0, 2, 1, 16, 1, 2, 3, 4, 5))
            self.assertEqual(struct.unpack_from("<I", system, 8)[0], 0xf0000017)
            self.assertEqual(struct.unpack_from("<I", cancel, 28)[0], 0)
            self.assertEqual(struct.unpack_from("<I", clear, 8)[0], 0xf000000d)
            conn.sendall(frame(999))
        with Peer(handler) as peer:
            session = self.open_client(peer)
            item = Item(b"L:X", b"number", 4, .25)
            self.assertEqual(self.lib.simc_add_definition(session, 9, C.byref(item), 1), OK)
            self.assertEqual(self.lib.simc_request_data(session, 7, 9, 4, 1), OK)
            serial = C.c_uint32()
            self.assertEqual(self.lib.simc_set_double(session, 9, 1, C.byref(serial)), OK)
            self.assertEqual(serial.value, 3)
            self.assertEqual(self.lib.simc_map_event(session, 2, b"HEADING_BUG_INC"), OK)
            self.assertEqual(self.lib.simc_send_event(session, 2, (C.c_uint32 * 5)(1, 2, 3, 4, 5),
                                                    C.byref(serial)), OK)
            self.assertEqual(self.lib.simc_subscribe_system_event(session, 100, b"AircraftLoaded"), OK)
            self.assertEqual(self.lib.simc_request_data(session, 7, 9, 0, 0), OK)
            self.assertEqual(self.lib.simc_clear_definition(session, 9), OK)
            self.assertEqual(self.poll(session)[0], OK)

    def test_mixed_string_double_snapshot(self):
        payload = b"FlyByWire A32NX".ljust(256, b"\0") + struct.pack("<dd", 0, 1)
        with Peer(lambda conn, _: conn.sendall(data_frame(payload, count=3))) as peer:
            session = self.open_client(peer)
            result, message = self.poll(session)
            self.assertEqual(result, OK)
            items = (Item * 3)(Item(b"TITLE", None, 9, 0), Item(b"L:ON", b"number", 4, 0),
                               Item(b"L:AVAIL", b"number", 4, 0))
            values = (Value * 3)()
            self.assertEqual(self.lib.simc_decode_data(C.byref(message), items, 3, values), OK)
            self.assertEqual(values[0].string, b"FlyByWire A32NX")
            self.assertEqual((values[1].number, values[2].number), (0, 1))
            for field, wrong in (("count", 2), ("flags", 2), ("data_size", 271), ("data_size", 273)):
                original = getattr(message, field)
                setattr(message, field, wrong)
                self.assertEqual(self.lib.simc_decode_data(C.byref(message), items, 3, values), PROTOCOL)
                setattr(message, field, original)

    def test_nonzero_response_object_id(self):
        payload = b"FlyByWire A32NX".ljust(256, b"\0") + struct.pack("<dd", -1, 1)
        with Peer(lambda conn, _: conn.sendall(data_frame(payload, count=3, obj=42))) as peer:
            session = self.open_client(peer)
            result, message = self.poll(session)
            self.assertEqual(result, OK)
            self.assertEqual(message.object_id, 42)
            items = (Item * 3)(Item(b"TITLE", None, 9, 0), Item(b"L:ON", b"number", 4, 0),
                               Item(b"L:AVAIL", b"number", 4, 0))
            values = (Value * 3)()
            self.assertEqual(self.lib.simc_decode_data(C.byref(message), items, 3, values), OK)
            self.assertEqual(values[0].string, b"FlyByWire A32NX")
            self.assertEqual((values[1].number, values[2].number), (-1, 1))

    def test_running_and_aircraft_loaded_notifications(self):
        running = frame(4, struct.pack("<3I", 0, 101, 1))
        stopped = frame(4, struct.pack("<3I", 0, 101, 0))
        loaded = frame(6, struct.pack("<2I", 0, 100) + b"aircraft.cfg".ljust(260, b"\0") +
                       struct.pack("<I", 0))
        with Peer(lambda conn, _: conn.sendall(running + loaded + stopped)) as peer:
            session = self.open_client(peer)
            for event_id, event_data in ((101, 1), (100, 0), (101, 0)):
                result, message = self.poll(session)
                self.assertEqual(result, OK)
                self.assertEqual((message.type, message.event_id, message.event_data),
                                 (3, event_id, event_data))

    def test_probe_heat_read_subscription(self):
        names = (b"L:A32NX_MAN_PITOT_HEAT", b"L:A32NX_ELEC_AC_2_BUS_IS_POWERED",
                 b"L:A32NX_ELEC_DC_2_BUS_IS_POWERED", b"L:A32NX_OVHD_INTLT_ANN")
        snapshots = ((1, 1, 1, 1), (0, 1, 1, 0), (-1, 0, 1, 2))
        def handler(conn, _):
            for name in names:
                definition = receive_packet(conn)
                self.assertEqual(struct.unpack_from("<I", definition, 16)[0], 3)
                self.assertEqual(definition[20:276].split(b"\0")[0], name)
                self.assertEqual(struct.unpack_from("<I", definition, 532)[0], 4)
            request = receive_packet(conn)
            self.assertEqual(struct.unpack_from("<8I", request, 16), (2, 3, 0, 4, 0, 0, 0, 0))
            conn.sendall(b"".join(data_frame(struct.pack("<4d", *snapshot), req=2,
                                            definition=3, obj=42, count=4)
                                  for snapshot in snapshots))
        with Peer(handler) as peer:
            session = self.open_client(peer)
            items = (Item * 4)(*(Item(name, b"number", 4, 0) for name in names))
            self.assertEqual(self.lib.simc_add_definition(session, 3, items, 4), OK)
            self.assertEqual(self.lib.simc_request_data(session, 2, 3, 4, 0), OK)
            for expected in snapshots:
                result, message = self.poll(session)
                self.assertEqual(result, OK)
                self.assertEqual((message.request_id, message.definition_id), (2, 3))
                values = (Value * 4)()
                self.assertEqual(self.lib.simc_decode_data(C.byref(message), items,
                                                         4, values), OK)
                self.assertEqual(tuple(value.number for value in values), expected)

    def test_invalid_inputs(self):
        session = C.c_void_p(123)
        for host, port in ((b"localhost", b"5111"), (b"1.2.3.4", b"0"),
                           (b"1.2.3.4", b"65536"), (b"1.2.3.4", b"+5111")):
            config = Config(host, port, b"Test", 5, 500)
            self.assertEqual(self.lib.simc_open(C.byref(session), C.byref(config)), INVALID)
            self.assertFalse(session.value)
        self.assertEqual(self.lib.simc_poll(None, 1, C.byref(Message())), INVALID)
        self.assertEqual(self.lib.simc_request_data(None, 1, 1, 99, 0), INVALID)
        self.assertEqual(self.lib.simc_request_data(None, 1, 1, 4, 2), INVALID)
        self.assertEqual(self.lib.simc_set_double(None, 1, float("nan"), None), INVALID)
        self.assertEqual(self.lib.simc_map_event(None, 1, b"X" * 256), INVALID)
        item = Item(b"X", b"number", 4, -1)
        self.assertEqual(self.lib.simc_add_definition(None, 1, C.byref(item), 1), INVALID)
        self.lib.simc_close(None)

    def test_boot_debounce_logic(self):
        binary = ROOT / "tests" / "build" / "test_button"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        str(ROOT / "tests" / "test_button.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_loading_and_action_gate_logic(self):
        binary = ROOT / "tests" / "build" / "test_flight_gate"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        str(ROOT / "tests" / "test_flight_gate.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_probe_indicator_and_led_encoding(self):
        binary = ROOT / "tests" / "build" / "test_probe_indicator"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        str(ROOT / "tests" / "test_probe_indicator.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
