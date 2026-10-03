/** @file
  将 QEMU 的 OpRegion 数据搬入客体内存，再把地址发布到 ASLS。

  gBS 是固件的 Boot Services 表，作用类似平台服务接口；AllocatePages
  分配的是客体物理内存页，不是宿主机内存，也不是普通 malloc 缓冲区。
  数据的格式检查位于 Validate.c，入口和所属设备定位位于 IgdOpRegion.c。
  参考来源与版权声明见项目根目录 NOTICE。
  SPDX-License-Identifier: BSD-2-Clause
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/QemuFwCfgLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "OpRegion.h"
#include "Validate.h"

#define IGD_ASLS_OFFSET  0xfc

EFI_STATUS
IgdOpRegionAssign (
  IN EFI_PCI_IO_PROTOCOL *PciIo
  )
{
  FIRMWARE_CONFIG_ITEM Item;
  RETURN_STATUS        ReturnStatus;
  EFI_STATUS           Status;
  EFI_PHYSICAL_ADDRESS Address;
  UINTN                Size;
  UINTN                Pages;
  UINT8                *Blob;
  UINT32               OldAsls;
  UINT32               NewAsls;
  UINT32               ReadBack;

  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, IGD_ASLS_OFFSET, 1, &OldAsls);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (OldAsls != 0) {
    // 不解引用未知的现有地址，不覆盖其他固件已完成的配置。
    DEBUG ((DEBUG_INFO, "IgdOpRegion: existing ASLS=%08x preserved; no assignment\n", OldAsls));
    return EFI_ALREADY_STARTED;
  }

  // fw_cfg 是 QEMU 向客体固件提供数据的接口；该名字不是宿主机文件路径。
  // QEMU 这项接口只描述单个 IGD，ROM 必须附属于提供该 blob 的那个 IGD。
  ReturnStatus = QemuFwCfgFindFile ("etc/igd-opregion", &Item, &Size);
  if (RETURN_ERROR (ReturnStatus)) {
    return (EFI_STATUS)ReturnStatus;
  }

  // 提前限定大小，既保护后续固定字段读取，也避免分配无界数据。
  if (Size < SIZE_8KB || Size > IGD_OPREGION_MAX_BYTES) {
    return EFI_BAD_BUFFER_SIZE;
  }

  // ASLS 只有 32 位。AllocateMaxAddress 保证整个分配位于 4 GiB 以下。
  // NVS 表示这段内容要保留给操作系统，不能作为入口临时缓冲区回收。
  Pages = EFI_SIZE_TO_PAGES (Size);
  Address = BASE_4GB - 1;
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiACPIMemoryNVS, Pages, &Address);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Blob = (UINT8 *)(UINTN)Address;
  ZeroMem (Blob, EFI_PAGES_TO_SIZE (Pages));
  QemuFwCfgSelectItem (Item);
  QemuFwCfgReadBytes (Size, Blob);

  // 必须复制整个 blob，保留 2.1+ 外置 VBT 相对于 OpRegion 起点的位置。
  Status = IgdOpRegionValidate (Blob, Size);
  if (EFI_ERROR (Status)) {
    // 尚未发布地址，设备不会引用这段内存，可以安全释放。
    gBS->FreePages (Address, Pages);
    return Status;
  }

  NewAsls = (UINT32)Address;
  Status = PciIo->Pci.Write (PciIo, EfiPciIoWidthUint32, IGD_ASLS_OFFSET, 1, &NewAsls);
  if (EFI_ERROR (Status)) {
    // 写调用失败不保证设备完全没有接受地址。写尝试之后都保留 NVS，
    // 防止设备指向以后被复用的内存；这也是下面读回失败时不释放的原因。
    return Status;
  }

  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, IGD_ASLS_OFFSET, 1, &ReadBack);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (ReadBack != NewAsls) {
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((DEBUG_INFO, "IgdOpRegion: ASLS=%08x, blob=%Lu bytes, readback OK\n",
    NewAsls, (UINT64)Size));
  return EFI_SUCCESS;
}
