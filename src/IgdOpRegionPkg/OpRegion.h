/** @file
  OpRegion 交接接口与内存策略。参考来源与版权声明见项目根目录 NOTICE。
  SPDX-License-Identifier: BSD-2-Clause
**/
#ifndef IGD_OPREGION_H_
#define IGD_OPREGION_H_

#include <Uefi.h>
#include <Protocol/PciIo.h>

// 限制固件为单个 blob 分配的内存量；这是项目策略，不是硬件格式上限。
// 有明确需求时可修改此值，或以同名编译宏覆盖。
#ifndef IGD_OPREGION_MAX_BYTES
#define IGD_OPREGION_MAX_BYTES  SIZE_1MB
#endif

/** 为 ROM 所属目标 IGD 交接 QEMU 提供的 OpRegion。

  调用方传入从目标控制器成功取得的有效 PCI_IO 接口。

  EFI_SUCCESS：写入 ASLS 并读回一致。
  EFI_ALREADY_STARTED：已有非零 ASLS，保持原值，没有验证或新交接。
  其他状态：数据来源、分配、校验或 PCI 访问失败。
  一旦尝试写 ASLS，分配的 NVS 就保留，错误返回不意味着这块内存已释放。
**/
EFI_STATUS
IgdOpRegionAssign (
  IN EFI_PCI_IO_PROTOCOL *PciIo
  );

#endif
