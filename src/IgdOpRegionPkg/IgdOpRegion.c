/** @file
  PCI Option ROM 的入口与设备定位。

  普通 C 程序的 main() 接收命令行参数；这里的入口接收 ImageHandle，
  即固件给当前 EFI 镜像的标识。LoadedImage 协议可据此找到加载来源。
  标准 PCI Option ROM 路径下，DeviceHandle 就是承载本 ROM 的 PCI 控制器。

  使用者保证本 ROM 绑定到目标 IGD；本模块直接给所属控制器交接 OpRegion。
  参考来源与版权声明见项目根目录 NOTICE。
  SPDX-License-Identifier: BSD-2-Clause
**/

#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PciIo.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "OpRegion.h"

/** 获取本 ROM 所属控制器的 PCI_IO 接口。

  协议是固件提供的一组函数指针接口。HandleProtocol 只查询给定 handle，
  使用者已负责绑定目标设备，代码只取得所属控制器的访问接口。
  获取接口仍可能失败，因此保留固件 API 的错误返回。
**/
STATIC
EFI_STATUS
GetRomPciIo (
  IN  EFI_HANDLE          ImageHandle,
  OUT EFI_PCI_IO_PROTOCOL **PciIo
  )
{
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  EFI_STATUS                Status;

  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return gBS->HandleProtocol (
                  LoadedImage->DeviceHandle,
                  &gEfiPciIoProtocolGuid,
                  (VOID **)PciIo
                  );
}

/** 同步入口：取得所属设备接口 → 准备数据并交接。

  正常 PCI Option ROM 加载顺序已经安装 PCI_IO，故无需创建等待事件。
  入口返回前完成全部处理，不留下通知回调或驱动全局状态。
**/
EFI_STATUS
EFIAPI
IgdOpRegionEntry (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_PCI_IO_PROTOCOL *PciIo;
  EFI_STATUS         Status;

  Status = GetRomPciIo (ImageHandle, &PciIo);
  if (!EFI_ERROR (Status)) {
    Status = IgdOpRegionAssign (PciIo);
  }

  DEBUG ((EFI_ERROR (Status) ? DEBUG_ERROR : DEBUG_INFO,
    "IgdOpRegion: result %r\n", Status));
  return Status;
}
