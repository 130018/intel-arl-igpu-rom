/** @file
  OpRegion 与 VBT 外层结构校验，独立于固件设备访问流程。

  按 Intel IGD OpRegion 和 VBT 的字段格式读取，不将缓冲区强转成未对齐结构体。
  这里的 CopyMem 与普通 C 的 memcpy 相同：把固定宽度字段复制到本地变量。
  本项目只构建 X64，该目标采用小端序。
  字段格式参考 VfioIgdPkg/Include/IndustryStandard/IgdOpRegion.h；
  参考来源与版权声明见项目根目录 NOTICE。
  SPDX-License-Identifier: BSD-2-Clause
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#include "OpRegion.h"
#include "Validate.h"

#define VBT_HEADER_BYTES  48
#define BDB_HEADER_BYTES  22
#define INLINE_VBT_OFFSET 0x400
#define INLINE_VBT_BYTES  0x1800

/** 先检查容器边界，再读取 VBT 及其 BIOS Data Block 头部。

  VBT 头保存总长度与 BDB 的相对偏移；BDB 头再保存自身长度。偏移检查
  使用“长度 <= 总量 - 偏移”，避免把偏移与长度相加造成整数溢出。
**/
STATIC
EFI_STATUS
ValidateVbt (
  IN CONST UINT8 *Vbt,
  IN UINTN       Available
  )
{
  UINT16 HeaderBytes;
  UINT16 TableBytes;
  UINT32 BdbOffset;
  UINT16 BdbHeaderBytes;
  UINT16 BdbBytes;
  UINT8  Sum;
  UINTN  Index;

  if (Available < VBT_HEADER_BYTES || CompareMem (Vbt, "$VBT", 4) != 0) {
    return EFI_COMPROMISED_DATA;
  }

  CopyMem (&HeaderBytes, Vbt + 22, sizeof HeaderBytes);
  CopyMem (&TableBytes, Vbt + 24, sizeof TableBytes);
  CopyMem (&BdbOffset, Vbt + 28, sizeof BdbOffset);
  if (HeaderBytes < VBT_HEADER_BYTES || HeaderBytes > TableBytes || TableBytes > Available) {
    return EFI_COMPROMISED_DATA;
  }

  // 理想的 VBT 字节校验和为 0，但实际平台固件可能提供非零值。
  // Linux 的正式驱动也不以此拒绝 VBT。只记录诊断，保留原始表内容；
  // 辅助 ROM 仍严格检查下方的结构和范围，不自行重写固件的 checksum。
  Sum = 0;
  for (Index = 0; Index < TableBytes; ++Index) {
    Sum = (UINT8)(Sum + Vbt[Index]);
  }

  if (Sum != 0) {
    DEBUG ((DEBUG_INFO, "IgdOpRegion: VBT checksum sum=%02x; firmware data preserved\n", Sum));
  }

  if (BdbOffset < HeaderBytes || BdbOffset > TableBytes ||
      BDB_HEADER_BYTES > TableBytes - BdbOffset) {
    return EFI_COMPROMISED_DATA;
  }

  // 签名首 15 字节固定，尾部填充字节不参与标识比较。
  if (CompareMem (Vbt + BdbOffset, "BIOS_DATA_BLOCK", 15) != 0) {
    return EFI_COMPROMISED_DATA;
  }

  CopyMem (&BdbHeaderBytes, Vbt + BdbOffset + 18, sizeof BdbHeaderBytes);
  CopyMem (&BdbBytes, Vbt + BdbOffset + 20, sizeof BdbBytes);
  if (BdbHeaderBytes < BDB_HEADER_BYTES || BdbHeaderBytes > BdbBytes ||
      BdbBytes > TableBytes - BdbOffset) {
    return EFI_COMPROMISED_DATA;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
IgdOpRegionValidate (
  IN CONST UINT8 *Blob,
  IN UINTN       Size
  )
{
  UINT32 HeaderKiB;
  UINT32 Version;
  UINT32 Mailboxes;
  UINT64 DeclaredBytes;
  UINT64 Rvda;
  UINT32 Rvds;

  if (Blob == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Size < SIZE_8KB || Size > IGD_OPREGION_MAX_BYTES) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (CompareMem (Blob, "IntelGraphicsMem", 16) != 0) {
    return EFI_COMPROMISED_DATA;
  }

  CopyMem (&HeaderKiB, Blob + 0x10, sizeof HeaderKiB);
  CopyMem (&Version, Blob + 0x14, sizeof Version);
  CopyMem (&Mailboxes, Blob + 0x58, sizeof Mailboxes);
  DeclaredBytes = (UINT64)HeaderKiB * 1024;
  if (DeclaredBytes < SIZE_8KB || DeclaredBytes > Size) {
    return EFI_COMPROMISED_DATA;
  }

  // RVDA/RVDS 仅在声明了 Mailbox 3 时才有意义。无扩展描述就检查内嵌 VBT。
  Rvda = 0;
  Rvds = 0;
  if ((Mailboxes & BIT2) != 0) {
    CopyMem (&Rvda, Blob + 0x3ba, sizeof Rvda);
    CopyMem (&Rvds, Blob + 0x3c2, sizeof Rvds);
  }

  if ((Rvda == 0) != (Rvds == 0)) {
    return EFI_COMPROMISED_DATA;
  }

  if (Rvda != 0) {
    // OpRegion 2.0 使用绝对 RVDA，不能在搬运 blob 后直接沿用。
    // 此实现只交接 2.1+ 的相对描述，不进行绝对地址重定位。
    if (Version < 0x02010000) {
      return EFI_UNSUPPORTED;
    }

    if (Rvda < DeclaredBytes || Rvda > Size || Rvds > Size - (UINTN)Rvda) {
      return EFI_COMPROMISED_DATA;
    }

    return ValidateVbt (Blob + (UINTN)Rvda, Rvds);
  }

  if ((Mailboxes & BIT3) == 0) {
    return EFI_UNSUPPORTED;
  }

  // 内嵌 VBT 的 Mailbox 4 固定为 6 KiB，不能让长度延伸进下一个 mailbox。
  return ValidateVbt (Blob + INLINE_VBT_OFFSET, INLINE_VBT_BYTES);
}
