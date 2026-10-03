/*
 * 在宿主机调用真实 Validate.c 的缓冲区校验函数。
 * 使用 EDK2 原始类型；只把其两个内存库接口适配到 libc。
 * 合成表仅用于验证容器结构，不能作为可运行 GPU 的 VBT。
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <stdio.h>
#include <string.h>

#include "../src/IgdOpRegionPkg/OpRegion.h"
#include "../src/IgdOpRegionPkg/Validate.h"

#define BASE_BYTES       8192
#define VBT_OFFSET       1024
#define SMALL_VBT_BYTES  70
#define INLINE_CAPACITY  6144

STATIC UINTN mCases;
STATIC UINTN mFailures;

VOID *
EFIAPI
CopyMem (
  OUT VOID       *Destination,
  IN  CONST VOID *Source,
  IN  UINTN      Length
  )
{
  return memcpy (Destination, Source, Length);
}

INTN
EFIAPI
CompareMem (
  IN CONST VOID *Left,
  IN CONST VOID *Right,
  IN UINTN      Length
  )
{
  return (INTN)memcmp (Left, Right, Length);
}

// 用逐字节编码构造小端输入，不依赖被测代码的读取方式。
STATIC VOID
Put16 (UINT8 *Buffer, UINTN Offset, UINT16 Value)
{
  Buffer[Offset] = (UINT8)Value;
  Buffer[Offset + 1] = (UINT8)(Value >> 8);
}

STATIC VOID
Put32 (UINT8 *Buffer, UINTN Offset, UINT32 Value)
{
  UINTN Index;
  for (Index = 0; Index < 4; ++Index) {
    Buffer[Offset + Index] = (UINT8)(Value >> (Index * 8));
  }
}

STATIC VOID
Put64 (UINT8 *Buffer, UINTN Offset, UINT64 Value)
{
  UINTN Index;
  for (Index = 0; Index < 8; ++Index) {
    Buffer[Offset + Index] = (UINT8)(Value >> (Index * 8));
  }
}

STATIC VOID
FixChecksum (UINT8 *Vbt, UINTN Length)
{
  UINTN Index;
  unsigned int Sum = 0;

  Vbt[26] = 0;
  for (Index = 0; Index < Length; ++Index) {
    Sum += Vbt[Index];
  }
  Vbt[26] = (UINT8)(0U - Sum);
}

STATIC VOID
MakeVbt (UINT8 *Vbt, UINT16 Length)
{
  memset (Vbt, 0, Length);
  memcpy (Vbt, "$VBT SYNTHETIC", 14);
  Put16 (Vbt, 20, 241);             // 表格式版本。
  Put16 (Vbt, 22, 48);              // VBT 头长度。
  Put16 (Vbt, 24, Length);          // 包含头与 BDB 的总长度。
  Put32 (Vbt, 28, 48);              // BDB 紧接 VBT 头。
  memcpy (Vbt + 48, "BIOS_DATA_BLOCK", 15);
  Put16 (Vbt, 48 + 16, 241);
  Put16 (Vbt, 48 + 18, 22);
  Put16 (Vbt, 48 + 20, (UINT16)(Length - 48));
  FixChecksum (Vbt, Length);
}

STATIC VOID
MakeInline (UINT8 *Blob, UINTN Capacity)
{
  memset (Blob, 0, Capacity);
  memcpy (Blob, "IntelGraphicsMem", 16);
  Put32 (Blob, 0x10, 8);
  Put32 (Blob, 0x14, 0x02010000);
  Put32 (Blob, 0x58, BIT3);
  MakeVbt (Blob + VBT_OFFSET, SMALL_VBT_BYTES);
}

STATIC VOID
MakeExternal (UINT8 *Blob, UINTN Capacity)
{
  memset (Blob, 0, Capacity);
  memcpy (Blob, "IntelGraphicsMem", 16);
  Put32 (Blob, 0x10, 8);
  Put32 (Blob, 0x14, 0x02010000);
  Put32 (Blob, 0x58, BIT2);
  Put64 (Blob, 0x3ba, BASE_BYTES);
  Put32 (Blob, 0x3c2, SMALL_VBT_BYTES);
  MakeVbt (Blob + BASE_BYTES, SMALL_VBT_BYTES);
  // 0x400 处故意保持全零，验证外置 VBT 不依赖内嵌区域。
}

STATIC VOID
Check (CONST CHAR8 *Name, CONST UINT8 *Blob, UINTN Size, EFI_STATUS Expected)
{
  EFI_STATUS Actual = IgdOpRegionValidate (Blob, Size);
  ++mCases;
  if (Actual != Expected) {
    ++mFailures;
    printf ("FAIL %s: expected=0x%llx actual=0x%llx\n", Name,
      (unsigned long long)Expected, (unsigned long long)Actual);
  }
}

int
main (void)
{
  UINT8 Blob[BASE_BYTES + SMALL_VBT_BYTES];
  UINT8 *Vbt;

  MakeInline (Blob, sizeof Blob);
  Check ("valid inline container", Blob, BASE_BYTES, EFI_SUCCESS);
  MakeVbt (Blob + VBT_OFFSET, INLINE_CAPACITY);
  Check ("inline container ends at mailbox boundary", Blob, BASE_BYTES, EFI_SUCCESS);

  MakeExternal (Blob, sizeof Blob);
  Check ("valid external container with zero inline area", Blob, sizeof Blob, EFI_SUCCESS);
  Check ("truncated external container", Blob, sizeof Blob - 1, EFI_COMPROMISED_DATA);
  Check ("truncated base OpRegion", Blob, BASE_BYTES - 1, EFI_BAD_BUFFER_SIZE);
  Check ("null input", NULL, BASE_BYTES, EFI_INVALID_PARAMETER);
  Check ("unbounded advertised allocation", Blob, IGD_OPREGION_MAX_BYTES + 1, EFI_BAD_BUFFER_SIZE);

  MakeInline (Blob, sizeof Blob);
  Blob[0] ^= 1;
  Check ("invalid OpRegion signature", Blob, BASE_BYTES, EFI_COMPROMISED_DATA);

  MakeInline (Blob, sizeof Blob);
  Put32 (Blob, 0x10, 9);
  Check ("declared OpRegion exceeds buffer", Blob, BASE_BYTES, EFI_COMPROMISED_DATA);

  MakeInline (Blob, sizeof Blob);
  Vbt = Blob + VBT_OFFSET;
  Vbt[26] ^= 1;
  Check ("nonzero firmware checksum does not block handoff", Blob, BASE_BYTES, EFI_SUCCESS);

  MakeInline (Blob, sizeof Blob);
  Vbt = Blob + VBT_OFFSET;
  Vbt[SMALL_VBT_BYTES - 1] ^= 1;
  Check ("damaged BDB length remains rejected", Blob, BASE_BYTES, EFI_COMPROMISED_DATA);

  MakeInline (Blob, sizeof Blob);
  Vbt = Blob + VBT_OFFSET;
  Put16 (Vbt, 22, 47);
  FixChecksum (Vbt, SMALL_VBT_BYTES);
  Check ("VBT header shorter than fixed fields", Blob, BASE_BYTES, EFI_COMPROMISED_DATA);

  MakeInline (Blob, sizeof Blob);
  Put16 (Blob + VBT_OFFSET, 24, INLINE_CAPACITY + 1);
  Check ("inline VBT spills into next mailbox", Blob, BASE_BYTES, EFI_COMPROMISED_DATA);

  MakeInline (Blob, sizeof Blob);
  Vbt = Blob + VBT_OFFSET;
  Put32 (Vbt, 28, 0xffffffffU);
  FixChecksum (Vbt, SMALL_VBT_BYTES);
  Check ("BDB offset would wrap addition", Blob, BASE_BYTES, EFI_COMPROMISED_DATA);

  MakeInline (Blob, sizeof Blob);
  Vbt = Blob + VBT_OFFSET;
  Put32 (Vbt, 28, 49);
  FixChecksum (Vbt, SMALL_VBT_BYTES);
  Check ("BDB header truncated by one byte", Blob, BASE_BYTES, EFI_COMPROMISED_DATA);

  MakeInline (Blob, sizeof Blob);
  Vbt = Blob + VBT_OFFSET;
  Put16 (Vbt, 48 + 18, 23);
  FixChecksum (Vbt, SMALL_VBT_BYTES);
  Check ("BDB header exceeds BDB length", Blob, BASE_BYTES, EFI_COMPROMISED_DATA);

  MakeInline (Blob, sizeof Blob);
  Vbt = Blob + VBT_OFFSET;
  Put16 (Vbt, 48 + 20, 23);
  FixChecksum (Vbt, SMALL_VBT_BYTES);
  Check ("BDB data exceeds VBT length", Blob, BASE_BYTES, EFI_COMPROMISED_DATA);

  MakeExternal (Blob, sizeof Blob);
  Put64 (Blob, 0x3ba, 0xffffffffffffffffULL);
  Check ("external offset exceeds integer address space", Blob, sizeof Blob, EFI_COMPROMISED_DATA);

  MakeExternal (Blob, sizeof Blob);
  Put32 (Blob, 0x3c2, SMALL_VBT_BYTES + 1);
  Check ("external VBT overruns blob by one byte", Blob, sizeof Blob, EFI_COMPROMISED_DATA);

  MakeExternal (Blob, sizeof Blob);
  Put32 (Blob, 0x3c2, 47);
  Check ("external VBT too short for header", Blob, sizeof Blob, EFI_COMPROMISED_DATA);

  MakeExternal (Blob, sizeof Blob);
  Put32 (Blob, 0x3c2, 0);
  Check ("external address without length", Blob, sizeof Blob, EFI_COMPROMISED_DATA);

  MakeExternal (Blob, sizeof Blob);
  Put32 (Blob, 0x14, 0x02000000);
  Check ("unsupported absolute RVDA version", Blob, sizeof Blob, EFI_UNSUPPORTED);

  MakeInline (Blob, sizeof Blob);
  Put32 (Blob, 0x58, 0);
  Check ("no declared VBT mailbox", Blob, BASE_BYTES, EFI_UNSUPPORTED);

  printf ("Validate: %llu cases, %llu failures\n",
    (unsigned long long)mCases, (unsigned long long)mFailures);
  return mFailures == 0 ? 0 : 1;
}
