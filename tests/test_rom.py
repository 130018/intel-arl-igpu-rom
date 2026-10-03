#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""用实际构建产物测试文件校验器；坏输入只在内存中构造，不修改 ROM。"""
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('verify_rom', PROJECT / 'tools/verify_rom.py')
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)
OUTPUT = Path(sys.argv.pop(1)) if len(sys.argv) > 1 else PROJECT / 'build'


class RomValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = (OUTPUT / 'intel-arl-igpu.rom').read_bytes()
        cls.efi = (OUTPUT / 'IgdOpRegion.efi').read_bytes()

    def test_real_build(self):
        self.assertEqual(verifier.verify(self.rom, self.efi)['架构'], 'X64')

    def test_truncated_files(self):
        # 容器头、PCIR 前后、完整 EFI 之前的不同截断位置。
        for size in (0, 2, 26, 28, 50, len(self.rom) - 1):
            with self.subTest(size=size), self.assertRaises(ValueError):
                verifier.verify(self.rom[:size], self.efi)

    def test_pcir_outside_file(self):
        bad = bytearray(self.rom)
        struct.pack_into('<H', bad, 24, 0xFFFF)
        with self.assertRaises(ValueError):
            verifier.verify(bad)

    def test_architecture_mismatch(self):
        bad = bytearray(self.rom)
        payload = struct.unpack_from('<H', bad, 22)[0]
        pe = payload + struct.unpack_from('<I', bad, payload + 0x3C)[0]
        struct.pack_into('<H', bad, pe + 4, 0xAA64)  # AArch64 不可充当 X64 固件。
        with self.assertRaises(ValueError):
            verifier.verify(bad)

    def test_id_is_parameter_not_whitelist(self):
        bad = bytearray(self.rom)
        pcir = struct.unpack_from('<H', bad, 24)[0]
        struct.pack_into('<HH', bad, pcir + 4, 0x1234, 0x5678)
        self.assertEqual(verifier.verify(bad, self.efi, (0x1234, 0x5678))['PCI_ID'], '1234:5678')
        with self.assertRaises(ValueError):
            verifier.verify(bad, self.efi, (0x1234, 0x5679))

    def test_section_data_outside_file(self):
        bad = bytearray(self.rom)
        payload = struct.unpack_from('<H', bad, 22)[0]
        pe = payload + struct.unpack_from('<I', bad, payload + 0x3C)[0]
        table = pe + 24 + struct.unpack_from('<H', bad, pe + 20)[0]
        struct.pack_into('<II', bad, table + 16, 4096, 0xFFFFFF00)
        with self.assertRaises(ValueError):
            verifier.verify(bad)

    def test_changed_payload(self):
        bad = bytearray(self.efi)
        bad[-1] ^= 1
        with self.assertRaises(ValueError):
            verifier.verify(self.rom, bad)

    def test_no_fake_wildcard(self):
        for pci_id in ('', '8086:0', '8086:0000', 'ffff:1234', 'xyz', '8086:7d67;echo'):
            with self.subTest(pci_id=pci_id), self.assertRaises(ValueError):
                verifier.parse_pci_id(pci_id)


if __name__ == '__main__':
    unittest.main()
