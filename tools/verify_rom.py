#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""检查单图像、未压缩的 X64 EFI PCI Option ROM，不绑定某个设备型号。

这是文件格式与载荷校验，不会执行EFI，也不代表客体初始化已成功。
只用Python标准库，可在Mac/Linux运行。
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_pe(payload):
    """读取EFI PE/COFF头；目标架构来自文件内容，与打包工具架构无关。"""
    require(len(payload) >= 64 and payload[:2] == b'MZ', 'EFI缺少有效MZ头')
    pe = struct.unpack_from('<I', payload, 0x3C)[0]
    require(pe + 24 <= len(payload), 'PE头超出EFI边界')
    require(payload[pe:pe + 4] == b'PE\0\0', 'EFI缺少PE签名')
    machine, sections = struct.unpack_from('<HH', payload, pe + 4)
    optional_size = struct.unpack_from('<H', payload, pe + 20)[0]
    optional = pe + 24
    require(machine == 0x8664, 'EFI目标不是X64')
    require(sections > 0, 'EFI缺少PE节')
    require(optional_size >= 112 and optional + optional_size <= len(payload), 'PE可选头不完整')
    require(struct.unpack_from('<H', payload, optional)[0] == 0x20B, 'X64 EFI必须使用PE32+头')
    require(struct.unpack_from('<H', payload, optional + 68)[0] == 11, 'EFI不是boot service driver')
    table = optional + optional_size
    require(table + sections * 40 <= len(payload), 'PE节表超出EFI边界')
    for index in range(sections):
        entry = table + index * 40
        raw_size, raw_offset = struct.unpack_from('<II', payload, entry + 16)
        require(raw_size == 0 or raw_offset + raw_size <= len(payload), 'PE节数据超出EFI边界')


def parse_pci_id(value):
    """PCI ID 使用十六进制 VVVV:DDDD 写法；零不是通配符。"""
    if not re.fullmatch(r'[0-9a-fA-F]{4}:[0-9a-fA-F]{4}', value):
        raise ValueError('PCI ID 格式应为十六进制 VVVV:DDDD')
    result = tuple(int(part, 16) for part in value.split(':'))
    if any(part in (0, 0xFFFF) for part in result):
        raise ValueError('请填真实 PCI ID，0000/ffff 不能作为通配符')
    return result


def verify(rom, expected_efi=None, expected_id=None):
    """所有偏移先检查边界再读取；拒绝截断文件和与容器不匹配的EFI。"""
    require(len(rom) >= 28, 'ROM头被截断')
    u16 = lambda offset: struct.unpack_from('<H', rom, offset)[0]
    require(u16(0) == 0xAA55, 'ROM签名不是AA55')
    require(struct.unpack_from('<I', rom, 4)[0] == 0x0EF1, 'ROM缺少EFI签名')
    require(u16(2) * 512 == len(rom), 'EFI ROM初始化长度与文件长度不一致')
    require(u16(8) == 11 and u16(10) == 0x8664, 'ROM的EFI子系统或架构不匹配')
    require(u16(12) == 0, '本项目只接受未压缩EFI镜像')
    payload_offset, pcir = u16(22), u16(24)
    require(pcir >= 26 and pcir + 28 <= len(rom), 'PCIR位置越界或覆盖ROM头')
    require(rom[pcir:pcir + 4] == b'PCIR', '缺少PCIR签名')
    pci_id = (u16(pcir + 4), u16(pcir + 6))
    if expected_id is not None:
        require(pci_id == expected_id, 'ROM中的PCI ID与指定值不符')
    pcir_length = u16(pcir + 10)
    require(pcir_length >= 24 and pcir + pcir_length <= len(rom), 'PCIR结构长度无效')
    require(u16(pcir + 16) * 512 == len(rom), 'ROM镜像长度与512字节块数不一致')
    require(rom[pcir + 20] == 3 and rom[pcir + 21] & 0x80, '需要单个最终EFI图像')
    require(payload_offset >= pcir + pcir_length and payload_offset < len(rom), 'EFI偏移越界或与PCIR重叠')
    embedded = rom[payload_offset:]
    check_pe(embedded)
    if expected_efi is not None:
        check_pe(expected_efi)
        require(embedded.startswith(expected_efi), '嵌入EFI与指定EFI不一致')
        padding = embedded[len(expected_efi):]
        require(len(padding) < 512 and all(byte in (0, 0xFF) for byte in padding), 'EFI尾部不是合法对齐填充')
    return {
        '结果': '通过', 'PCI_ID': '%04x:%04x' % pci_id, '架构': 'X64',
        'ROM字节数': len(rom), 'EFI偏移': payload_offset,
        'ROM_SHA256': hashlib.sha256(rom).hexdigest(),
        '指定EFI逐字节匹配': expected_efi is not None,
        '范围': '仅静态结构/载荷校验，未执行客体测试',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    parser.add_argument('--efi', type=Path, help='同时核对ROM内嵌的原始EFI')
    parser.add_argument('--pci-id', help='同时核对ROM头中的十六进制 VVVV:DDDD')
    args = parser.parse_args()
    try:
        pci_id = parse_pci_id(args.pci_id) if args.pci_id else None
        result = verify(args.rom.read_bytes(), args.efi.read_bytes() if args.efi else None, pci_id)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'校验失败：{error}\n')
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
