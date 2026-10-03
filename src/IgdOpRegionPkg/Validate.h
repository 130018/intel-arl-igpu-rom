/** @file
  不访问设备的缓冲区校验接口。参考来源与版权声明见项目根目录 NOTICE。
  SPDX-License-Identifier: BSD-2-Clause
**/
#ifndef IGD_OPREGION_VALIDATE_H_
#define IGD_OPREGION_VALIDATE_H_

#include <Uefi.h>

/** 检查 OpRegion、VBT/BDB 的外层结构；VBT 字节校验和只用于诊断。

  不解析每个 BDB 业务数据块，也不判断表中描述的端口是否适合实际硬件。
  函数只读取 [Blob, Blob+Size) 范围，不写入数据、不分配内存。
**/
EFI_STATUS
IgdOpRegionValidate (
  IN CONST UINT8 *Blob,
  IN UINTN       Size
  );

#endif
