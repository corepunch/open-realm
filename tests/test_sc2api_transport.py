#!/usr/bin/env python3
"""Exercise the optional endpoint over a real masked WebSocket, without dependencies."""
import argparse
import base64
import hashlib
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest


def varint(value):
    out = bytearray()
    while value > 127:
        out.append((value & 127) | 128)
        value >>= 7
    return bytes(out) + bytes([value])


def integer(field, value):
    return varint(field << 3) + varint(value)


def message(field, payload=b""):
    return varint((field << 3) | 2) + varint(len(payload)) + payload


def fields(data):
    pos = 0
    def read_int():
        nonlocal pos
        out = 0
        for shift in range(0, 70, 7):
            value = data[pos]
            pos += 1
            out |= (value & 127) << shift
            if not value & 128:
                return out
        raise ValueError("oversized varint")
    result = {}
    while pos < len(data):
        key = read_int()
        field, wire = key >> 3, key & 7
        if wire == 0:
            value = read_int()
        elif wire == 2:
            size = read_int()
            value = data[pos:pos + size]
            pos += size
        elif wire in (1, 5):
            size = 8 if wire == 1 else 4
            value = data[pos:pos + size]
            pos += size
        else:
            raise ValueError(f"unsupported wire {wire}")
        result.setdefault(field, []).append(value)
    return result


class WebSocket:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        key = base64.b64encode(os.urandom(16))
        self.sock.sendall(b"GET /sc2api HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
                          b"Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " + key + b"\r\n\r\n")
        response = bytearray()
        while b"\r\n\r\n" not in response:
            response.extend(self.sock.recv(4096))
        accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
        assert b"101 Switching Protocols" in response and accept in response

    def send(self, payload, opcode=2, final=True, masked=True, reserved=0):
        size = len(payload)
        frame = bytes([(128 if final else 0) | reserved | opcode])
        if size < 126:
            frame += bytes([(128 if masked else 0) | size])
        elif size <= 65535:
            frame += bytes([(128 if masked else 0) | 126]) + struct.pack("!H", size)
        else:
            frame += bytes([(128 if masked else 0) | 127]) + struct.pack("!Q", size)
        if masked:
            mask = os.urandom(4)
            frame += mask + bytes(value ^ mask[i & 3] for i, value in enumerate(payload))
        else:
            frame += payload
        self.sock.sendall(frame)

    def receive(self):
        def exact(size):
            data = bytearray()
            while len(data) < size:
                part = self.sock.recv(size - len(data))
                if not part:
                    raise EOFError("WebSocket closed")
                data.extend(part)
            return bytes(data)
        first, second = exact(2)
        assert not second & 128, "server frames must not be masked"
        size = second & 127
        if size == 126:
            size, = struct.unpack("!H", exact(2))
        elif size == 127:
            size, = struct.unpack("!Q", exact(8))
        return first & 15, exact(size)

    def request(self, field, payload=b"", request_id=0):
        self.send(message(field, payload) + integer(97, request_id))
        opcode, response = self.receive()
        assert opcode == 2
        response = fields(response)
        assert response[97] == [request_id]
        return response

    def close(self):
        self.sock.close()


class EndpointTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="openrealm-sc2api-")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            cls.port = probe.getsockname()[1]
        cls.log = open(Path(cls.directory.name) / "server.log", "w+")
        cls.process = subprocess.Popen([ARGS.binary, "-data", ARGS.data, "+dedicated", "1",
            "+fs_homepath", cls.directory.name, "+set", "sc2_api", "1", "+set", "sc2_api_port", str(cls.port),
            "+com_maxfps", "0"], stdout=cls.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if cls.process.poll() is not None:
                cls.log.seek(0)
                raise RuntimeError(cls.log.read())
            try:
                cls.ws = WebSocket(cls.port)
                return
            except OSError:
                time.sleep(0.05)
        raise TimeoutError("SC2API endpoint did not become ready")

    @classmethod
    def tearDownClass(cls):
        cls.ws.close()
        if cls.process.poll() is None:
            cls.process.terminate()
        cls.process.wait(timeout=10)
        cls.log.close()
        cls.directory.cleanup()

    def test_01_ping_fragmentation_order_and_explicit_zero_id(self):
        ws = self.ws
        response = ws.request(19)
        self.assertEqual(response[99], [1])
        payload = message(19) + integer(97, 42)
        ws.send(payload[:2], final=False)
        ws.send(b"keepalive", opcode=9)
        ws.send(payload[2:], opcode=0)
        self.assertEqual(ws.receive(), (10, b"keepalive"))
        opcode, data = ws.receive()
        self.assertEqual((opcode, fields(data)[97]), (2, [42]))
        ws.send(message(19) + integer(97, 43))
        ws.send(message(19) + integer(97, 44))
        self.assertEqual(fields(ws.receive()[1])[97], [43])
        self.assertEqual(fields(ws.receive()[1])[97], [44])

    def test_02_lifecycle_raw_metadata_and_exact_stepping(self):
        ws = self.ws
        self.assertIn(98, ws.request(12))
        # A valid map header reaches the real map transition, then loading
        # fails because terrain is absent. The endpoint must survive teardown.
        broken = Path(self.directory.name) / "Broken.w3m"
        subprocess.run([str(Path(ARGS.binary).with_name("mpqtool")), "-mpq", str(broken),
            "pack", str(Path(ARGS.data) / "resources/TransportMap/war3map.w3i"),
            "war3map.w3i"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        failed = ws.request(1, message(1, message(1, os.fsencode(broken))))
        self.assertTrue(98 in failed or 1 in fields(failed[1][0]))
        self.assertEqual(ws.request(19)[99], [1])
        create = message(1, message(1, b"Maps/Transport.w3m")) + message(3, integer(1, 1))
        response = ws.request(1, create, 50)
        self.assertNotIn(98, response)
        self.assertNotIn(1, fields(response[1][0]), "CreateGame returned an error")
        join = integer(1, 4) + message(3, integer(1, 1))
        response = ws.request(2, join, 51)
        self.assertNotIn(98, response)
        self.assertEqual(response[99], [3])
        self.assertGreater(fields(response[2][0])[1][0], 0)
        game_info = ws.request(9, request_id=52)
        self.assertIn(5, fields(game_info[9][0]))
        data = ws.request(13, integer(1, 1) + integer(2, 1) + integer(3, 1) + integer(4, 1), 53)
        self.assertIn(2, fields(data[13][0]))
        before = fields(fields(ws.request(10)[10][0])[3][0])[9][0]
        after = fields(ws.request(12, integer(1, 513), 54)[12][0])[1][0]
        self.assertEqual(after - before, 513)
        observed = fields(fields(ws.request(10)[10][0])[3][0])
        self.assertEqual(observed[9][0], after)
        self.assertIn(5, observed)
        raw = fields(observed[5][0])
        own = next(fields(unit) for unit in raw[2] if fields(unit)[5] == [1])
        query = ws.request(14, message(2, integer(1, own[3][0])))
        abilities = fields(fields(query[14][0])[2][0])
        self.assertIn(851986, [fields(item)[1][0] for item in abilities[1]],
                      "built-in Move must expose its numeric order ID, not a UI key")
        # No simulation work is scheduled by an ordinary repeated observation.
        self.assertEqual(fields(fields(ws.request(10)[10][0])[3][0])[9][0], after)
        self.assertIn(98, ws.request(2, join))
        self.assertNotIn(98, ws.request(5))

    def test_03_clean_close_and_reconnect(self):
        self.ws.send(struct.pack("!H", 1000), opcode=8)
        self.assertEqual(self.ws.receive(), (8, struct.pack("!H", 1000)))
        self.ws.close()
        type(self).ws = WebSocket(self.port)
        self.assertEqual(self.ws.request(19)[99], [1])

    def test_04_reject_unmasked_and_reserved_frames(self):
        for options in ({"masked": False}, {"reserved": 64}):
            self.ws.send(message(19), **options)
            with self.assertRaises((EOFError, ConnectionResetError)):
                self.ws.receive()
            self.ws.close()
            type(self).ws = WebSocket(self.port)
        self.assertEqual(self.ws.request(19)[99], [1])

    def test_05_independent_generated_protobuf_client(self):
        if not ARGS.proto_python:
            self.skipTest("official generated Python bindings were not requested")
        sys.path.insert(0, ARGS.proto_python)
        from s2clientprotocol import sc2api_pb2 as protocol
        def request(kind, configure=None, request_id=100):
            envelope = protocol.Request(id=request_id)
            payload = getattr(envelope, kind)
            payload.SetInParent()
            if configure:
                configure(payload)
            self.ws.send(envelope.SerializeToString())
            opcode, data = self.ws.receive()
            self.assertEqual(opcode, 2)
            response = protocol.Response.FromString(data)
            self.assertTrue(response.HasField("id"))
            self.assertEqual(response.id, request_id)
            self.assertEqual(list(response.error), [])
            return getattr(response, kind)
        request("ping", request_id=0)
        request("available_maps")
        def create(payload):
            payload.local_map.map_path = "Maps/Transport.w3m"
            payload.player_setup.add(type=1)
        self.assertFalse(request("create_game", create).HasField("error"))
        def join(payload):
            payload.race = 4
            payload.options.raw = True
        player = request("join_game", join).player_id
        self.assertGreater(player, 0)
        self.assertGreater(request("game_info").start_raw.map_size.x, 0)
        data = request("data", lambda payload: setattr(payload, "unit_type_id", True))
        self.assertGreater(len(data.units), 0)
        observation = request("observation").observation
        own = next(unit for unit in observation.raw_data.units if unit.owner == player)
        def action(payload):
            command = payload.actions.add().action_raw.unit_command
            command.ability_id = 851986
            command.unit_tags.append(own.tag)
            command.target_world_space_pos.x = own.pos.x + 1
            command.target_world_space_pos.y = own.pos.y
        self.assertEqual(list(request("action", action).result), [1])
        def query(payload):
            path = payload.pathing.add(unit_tag=own.tag)
            path.end_pos.x = own.pos.x + 1
            path.end_pos.y = own.pos.y
            payload.abilities.add(unit_tag=own.tag)
        result = request("query", query)
        self.assertGreater(result.pathing[0].distance, 0)
        self.assertEqual(result.abilities[0].unit_tag, own.tag)
        self.assertIn(851986, [ability.ability_id for ability in result.abilities[0].abilities])
        after = request("step", lambda payload: setattr(payload, "count", 3)).simulation_loop
        self.assertEqual(after, observation.game_loop + 3)
        self.assertEqual(request("observation").observation.game_loop, after)
        request("leave_game")
        request("quit")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="build/bin/openwarcraft3-tests")
    parser.add_argument("--data", default="build/tests")
    parser.add_argument("--proto-python", help="Python bindings generated from official s2client-proto schemas")
    ARGS, rest = parser.parse_known_args()
    ARGS.binary = str(Path(ARGS.binary).resolve())
    ARGS.data = str(Path(ARGS.data).resolve())
    unittest.main(argv=[__file__] + rest)
