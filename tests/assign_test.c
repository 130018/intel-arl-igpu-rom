/*
 * 调用真实 OpRegion.c 和 Validate.c，只替代固件的页、fw_cfg 与 PCI 接口。
 * 本机指针可能超过 4 GiB；fake AllocatePages 使用本机指针承载数据，
 * PCI mock 将 ASLS 视为不解引用的 32 位寄存器值。测试检查分配请求的
 * 4 GiB 上限，不模拟真实 EFI 内存图、PCI reset 或 Windows 驱动行为。
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/QemuFwCfgLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "OpRegion.h"

#define SOURCE_BYTES     (8192 + 72)
#define ALLOCATION_BYTES EFI_PAGES_TO_SIZE (EFI_SIZE_TO_PAGES (SOURCE_BYTES))
#define OLD_ASLS         0xdead0001U
#define TEST_ITEM        0x20

STATIC UINTN mFailures;
STATIC UINTN mCases;
STATIC UINT8 *mMemory;
STATIC EFI_PCI_IO_PROTOCOL mPci;
STATIC EFI_BOOT_SERVICES mBootServices;
EFI_BOOT_SERVICES *gBS = &mBootServices;

STATIC struct {
  UINT8         Source[SOURCE_BYTES];
  UINTN         SourceSize;
  UINT32        Asls;
  EFI_STATUS    AllocateStatus;
  EFI_STATUS    WriteStatus;
  EFI_STATUS    ReadbackStatus;
  RETURN_STATUS FindStatus;
  BOOLEAN       Mismatch;
  BOOLEAN       Selected;
  UINTN         Allocations;
  UINTN         Frees;
  UINTN         Writes;
  UINTN         Reads;
  UINTN         FwReads;
  UINTN         Finds;
  EFI_PHYSICAL_ADDRESS LastAddress;
} m;

#define CHECK(Name, Condition) do { \
  if (!(Condition)) { \
    ++mFailures; \
    printf ("FAIL %s (line %d)\n", Name, __LINE__); \
  } \
} while (0)

VOID * EFIAPI
CopyMem (VOID *Destination, CONST VOID *Source, UINTN Length)
{
  return memcpy (Destination, Source, Length);
}

VOID * EFIAPI
ZeroMem (VOID *Buffer, UINTN Length)
{
  return memset (Buffer, 0, Length);
}

INTN EFIAPI
CompareMem (CONST VOID *Left, CONST VOID *Right, UINTN Length)
{
  return (INTN)memcmp (Left, Right, Length);
}

// 构造带外置 VBT 的输入，尾部也有数据，防止只复制前 8 KiB。
STATIC VOID
PutLe (UINT8 *Buffer, UINTN Offset, UINT64 Value, UINTN Width)
{
  UINTN Index;
  for (Index = 0; Index < Width; ++Index) {
    Buffer[Offset + Index] = (UINT8)(Value >> (Index * 8));
  }
}

STATIC VOID
ResetCase (VOID)
{
  UINT8 *Vbt;

  ++mCases;
  memset (&m, 0, sizeof m);
  memset (mMemory, 0xa5, 2 * ALLOCATION_BYTES);
  m.Asls = OLD_ASLS;  // 不是本次分配的地址；被测函数不得访问或释放它。
  m.SourceSize = sizeof m.Source;
  memcpy (m.Source, "IntelGraphicsMem", 16);
  PutLe (m.Source, 0x10, 8, 4);
  PutLe (m.Source, 0x14, 0x02010000, 4);
  PutLe (m.Source, 0x58, BIT2, 4);
  PutLe (m.Source, 0x3ba, 8192, 8);
  PutLe (m.Source, 0x3c2, 72, 4);
  Vbt = m.Source + 8192;
  memcpy (Vbt, "$VBT TEST", 9);
  PutLe (Vbt, 22, 48, 2);
  PutLe (Vbt, 24, 72, 2);
  PutLe (Vbt, 28, 48, 4);
  memcpy (Vbt + 48, "BIOS_DATA_BLOCK", 15);
  PutLe (Vbt, 48 + 18, 22, 2);
  PutLe (Vbt, 48 + 20, 24, 2);
  Vbt[70] = 0x5a;
  Vbt[71] = 0xc3;
}

STATIC EFI_STATUS EFIAPI
AllocatePages (
  EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType,
  UINTN Pages, EFI_PHYSICAL_ADDRESS *Address
  )
{
  CHECK ("allocation requests bounded NVS", Type == AllocateMaxAddress &&
    MemoryType == EfiACPIMemoryNVS && *Address == BASE_4GB - 1 &&
    EFI_PAGES_TO_SIZE (Pages) == ALLOCATION_BYTES);
  if (EFI_ERROR (m.AllocateStatus)) {
    return m.AllocateStatus;
  }

  CHECK ("allocation remains within mock storage", m.Allocations < 2);
  if (m.Allocations >= 2) {
    return EFI_OUT_OF_RESOURCES;
  }

  *Address = (EFI_PHYSICAL_ADDRESS)(UINTN)(mMemory + m.Allocations * ALLOCATION_BYTES);
  m.LastAddress = *Address;
  ++m.Allocations;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
FreePages (EFI_PHYSICAL_ADDRESS Address, UINTN Pages)
{
  CHECK ("only the new unpublished allocation is released",
    Address == m.LastAddress && Address != OLD_ASLS &&
    EFI_PAGES_TO_SIZE (Pages) == ALLOCATION_BYTES && m.Writes == 0);
  ++m.Frees;
  return EFI_SUCCESS;
}

RETURN_STATUS EFIAPI
QemuFwCfgFindFile (CONST CHAR8 *Name, FIRMWARE_CONFIG_ITEM *Item, UINTN *Size)
{
  CHECK ("canonical fw_cfg name", strcmp (Name, "etc/igd-opregion") == 0);
  ++m.Finds;
  *Item = TEST_ITEM;
  *Size = m.SourceSize;
  return m.FindStatus;
}

VOID EFIAPI
QemuFwCfgSelectItem (FIRMWARE_CONFIG_ITEM Item)
{
  CHECK ("select located fw_cfg item", Item == TEST_ITEM && m.Allocations != 0);
  m.Selected = TRUE;
}

VOID EFIAPI
QemuFwCfgReadBytes (UINTN Size, VOID *Buffer)
{
  CHECK ("read the entire blob into new pages", m.Selected &&
    Size == sizeof m.Source && Buffer == (VOID *)(UINTN)m.LastAddress);
  memcpy (Buffer, m.Source, Size);
  ++m.FwReads;
}

STATIC EFI_STATUS EFIAPI
PciRead (EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_WIDTH Width,
  UINT32 Offset, UINTN Count, VOID *Buffer)
{
  CHECK ("readback follows a successful write", m.Writes == m.Reads + 1 &&
    !EFI_ERROR (m.WriteStatus));
  CHECK ("read target ASLS register", This == &mPci &&
    Width == EfiPciIoWidthUint32 && Offset == 0xfc && Count == 1);
  ++m.Reads;
  if (!EFI_ERROR (m.ReadbackStatus)) {
    *(UINT32 *)Buffer = m.Asls ^ (m.Mismatch ? 1U : 0U);
  }
  return m.ReadbackStatus;
}

STATIC EFI_STATUS EFIAPI
PciWrite (EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_WIDTH Width,
  UINT32 Offset, UINTN Count, VOID *Buffer)
{
  CHECK ("publish the new allocation after copying the blob", This == &mPci &&
    Width == EfiPciIoWidthUint32 && Offset == 0xfc && Count == 1 &&
    *(UINT32 *)Buffer == (UINT32)m.LastAddress && m.FwReads == m.Allocations &&
    memcmp ((VOID *)(UINTN)m.LastAddress, m.Source, sizeof m.Source) == 0);
  ++m.Writes;
  // A failing PCI write can still accept the address: retention must be safe.
  m.Asls = *(UINT32 *)Buffer;
  return m.WriteStatus;
}

int
main (void)
{
  UINTN Index;
  UINT32 FirstAsls;
  UINT8 FirstTail;
  BOOLEAN TailIsZero;

  if (posix_memalign ((VOID **)&mMemory, EFI_PAGE_SIZE, 2 * ALLOCATION_BYTES) != 0) {
    fprintf (stderr, "Cannot allocate host test storage\n");
    return 2;
  }
  mBootServices.AllocatePages = AllocatePages;
  mBootServices.FreePages = FreePages;
  mPci.Pci.Read = PciRead;
  mPci.Pci.Write = PciWrite;

  ResetCase ();
  m.Asls = 0;
  CHECK ("initial zero ASLS receives a handoff", IgdOpRegionAssign (&mPci) == EFI_SUCCESS);
  CHECK ("initial handoff retains published pages", m.Allocations == 1 &&
    m.FwReads == 1 && m.Writes == 1 && m.Reads == 1 && m.Frees == 0);

  ResetCase ();
  CHECK ("existing nonzero ASLS receives a new handoff", IgdOpRegionAssign (&mPci) == EFI_SUCCESS);
  CHECK ("nonzero ASLS did not skip the canonical source", m.Finds == 1 &&
    m.Allocations == 1 && m.FwReads == 1 && m.Writes == 1 && m.Reads == 1 && m.Frees == 0);
  CHECK ("copied external VBT including its tail", memcmp (mMemory, m.Source, sizeof m.Source) == 0);
  TailIsZero = TRUE;
  for (Index = sizeof m.Source; Index < ALLOCATION_BYTES; ++Index) {
    TailIsZero = (BOOLEAN)(TailIsZero && mMemory[Index] == 0);
  }
  CHECK ("unused page tail is zero", TailIsZero);

  FirstAsls = m.Asls;
  FirstTail = mMemory[SOURCE_BYTES - 1];
  m.Source[SOURCE_BYTES - 1] ^= 0x80;
  CHECK ("a subsequent invocation refreshes the handoff", IgdOpRegionAssign (&mPci) == EFI_SUCCESS);
  CHECK ("fresh pages replace the preceding ASLS", m.Allocations == 2 &&
    m.FwReads == 2 && m.Writes == 2 && m.Reads == 2 && m.Frees == 0 && m.Asls != FirstAsls);
  CHECK ("old pages remain untouched", mMemory[SOURCE_BYTES - 1] == FirstTail);
  CHECK ("fresh pages receive the refreshed source",
    memcmp (mMemory + ALLOCATION_BYTES, m.Source, sizeof m.Source) == 0);

  ResetCase ();
  m.FindStatus = RETURN_NOT_FOUND;
  CHECK ("missing canonical source", IgdOpRegionAssign (&mPci) == EFI_NOT_FOUND);
  CHECK ("missing source preserves existing ASLS", m.Asls == OLD_ASLS &&
    m.Allocations == 0 && m.Writes == 0 && m.Reads == 0 && m.Frees == 0);

  ResetCase ();
  m.SourceSize = IGD_OPREGION_MAX_BYTES + 1;
  CHECK ("unbounded source is rejected", IgdOpRegionAssign (&mPci) == EFI_BAD_BUFFER_SIZE);
  CHECK ("size rejection precedes allocation", m.Allocations == 0 && m.FwReads == 0 && m.Writes == 0 && m.Reads == 0);

  ResetCase ();
  m.AllocateStatus = EFI_OUT_OF_RESOURCES;
  CHECK ("allocation failure", IgdOpRegionAssign (&mPci) == EFI_OUT_OF_RESOURCES);
  CHECK ("failed allocation is not accessed or released", m.FwReads == 0 && m.Writes == 0 && m.Reads == 0 && m.Frees == 0);

  ResetCase ();
  m.Source[0] ^= 1;
  CHECK ("real validator rejects malformed data", IgdOpRegionAssign (&mPci) == EFI_COMPROMISED_DATA);
  CHECK ("unpublished pages are released", m.Allocations == 1 && m.Frees == 1 &&
    m.Writes == 0 && m.Reads == 0 && m.Asls == OLD_ASLS);

  ResetCase ();
  m.WriteStatus = EFI_DEVICE_ERROR;
  CHECK ("write failure is reported", IgdOpRegionAssign (&mPci) == EFI_DEVICE_ERROR);
  CHECK ("pages survive a write that accepted data but returned failure",
    m.Allocations == 1 && m.Writes == 1 && m.Reads == 0 && m.Frees == 0 && m.Asls == (UINT32)m.LastAddress);

  ResetCase ();
  m.ReadbackStatus = EFI_DEVICE_ERROR;
  CHECK ("readback failure is reported", IgdOpRegionAssign (&mPci) == EFI_DEVICE_ERROR);
  CHECK ("pages survive readback failure", m.Allocations == 1 && m.Writes == 1 && m.Reads == 1 && m.Frees == 0);

  ResetCase ();
  m.Mismatch = TRUE;
  CHECK ("readback mismatch is reported", IgdOpRegionAssign (&mPci) == EFI_DEVICE_ERROR);
  CHECK ("pages survive readback mismatch", m.Allocations == 1 && m.Writes == 1 && m.Reads == 1 && m.Frees == 0);

  printf ("Assign: %llu scenarios, %llu failures\n",
    (unsigned long long)mCases, (unsigned long long)mFailures);
  free (mMemory);
  return mFailures == 0 ? 0 : 1;
}
