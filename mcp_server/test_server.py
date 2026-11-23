"""Focused test of the cheat-search workflow without starting blueMSX+."""

import unittest
from unittest.mock import patch

import server


class RamSearchTest(unittest.TestCase):
    def setUp(self):
        self.telemetry = patch.object(server, "_telemetry")
        self.telemetry.start()

    def tearDown(self):
        self.telemetry.stop()
        server._previous.clear()
        server._candidates.clear()

    def test_narrows_candidates_after_two_game_changes(self):
        images = [bytes([5, 9, 4]), bytes([4, 9, 3]), bytes([3, 9, 3])]
        block = {"index": 0, "start": 0xC000, "size": 3}
        devices = {"devices": [{"index": 0, "type": 4, "blocks": [block]}]}
        with patch.object(server, "list_devices", return_value=devices), \
             patch.object(server, "read_memory", side_effect=lambda *args: images[0].hex()):
            self.assertEqual(server.capture_ram()["bytes"], 3)
        with patch.object(server, "list_devices", return_value=devices), \
             patch.object(server, "read_memory", side_effect=lambda *args: images[1].hex()):
            first = server.filter_ram("decreased")
        self.assertEqual(first["count"], 2)
        with patch.object(server, "list_devices", return_value=devices), \
             patch.object(server, "read_memory", side_effect=lambda *args: images[2].hex()):
            second = server.filter_ram("decreased")
        self.assertEqual(second["count"], 1)
        self.assertEqual(second["candidates"][0]["address"], 0xC000)


if __name__ == "__main__":
    unittest.main()
