# SPDX-License-Identifier: BSD-2-Clause
# build.sh 通过 PACKAGES_PATH 引用项目 src 目录，无需复制到 EDK2 源码树。
# 包装脚本默认选择 RELEASE；DEBUG 仅用于需要固件日志的开发构建。
# 参考来源与版权声明见项目根目录 NOTICE。
[Defines]
  PLATFORM_NAME           = IgdOpRegion
  PLATFORM_GUID           = 4BB71A24-09B6-4AC0-833D-F4D64389096B
  PLATFORM_VERSION        = 0.2
  DSC_SPECIFICATION       = 0x00010005
  OUTPUT_DIRECTORY        = Build/IgdOpRegion
  SUPPORTED_ARCHITECTURES = X64
  BUILD_TARGETS           = RELEASE|DEBUG
  SKUID_IDENTIFIER        = DEFAULT

[SkuIds]
  0|DEFAULT

# INF 声明库接口，DSC 为直接和间接依赖选择实现。
[LibraryClasses]
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLibRepStr/BaseMemoryLibRepStr.inf
  HobLib|MdeModulePkg/Library/BaseHobLibNull/BaseHobLibNull.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  QemuFwCfgLib|OvmfPkg/Library/QemuFwCfgLib/QemuFwCfgDxeLib.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
  StackCheckLib|MdePkg/Library/StackCheckLibNull/StackCheckLibNull.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf

!if $(TARGET) == DEBUG
  DebugLib|OvmfPkg/Library/PlatformDebugLibIoPort/PlatformDebugLibIoPort.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  MemDebugLogLib|OvmfPkg/Library/MemDebugLogLib/MemDebugLogLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
!else
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
!endif

[Components]
  IgdOpRegionPkg/IgdOpRegion.inf

!if $(TARGET) == DEBUG
[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000040
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x07
!endif
