# Intel Arrow Lake 核显直通辅助 ROM

在 macOS 上构建用于 **Proxmox VE 9.2 / QEMU / OVMF** 的 Intel Arrow Lake 核显直通辅助 ROM，产物名称为 **`intel-arl-igpu.rom`**。

这个程序在客体操作系统启动前运行：读取 QEMU 提供的 Intel 图形平台数据 **OpRegion**，将完整副本放入客体保留内存，再把地址写入核显的 **ASLS** 寄存器，让后续显卡驱动能够找到这些数据。每次启动都重新建立副本，避免客体重启后沿用失效的旧地址。它用于补齐这一段固件交接，适合已有物理核显直通配置的环境。

项目包含 C 源码、Mac 原生构建与封装脚本、格式检查器和本机测试。Apple Silicon Mac 可以直接生成 X64 UEFI 产物，无需 Linux 构建虚拟机。若会普通 C、尚未接触硬件或固件编程，可从 [原理与代码导读](docs/原理与代码导读.md) 开始。

## 适用前提

- 客体使用 X64 OVMF，固件能够从直通 PCI 设备的 Option ROM 加载 EFI 程序。
- QEMU 向客体固件提供 `etc/igd-opregion` 数据；本 ROM 只处理这一份 IGD 数据源。
- **使用者将 ROM 正确绑定到目标 Arrow Lake 核显。** 程序根据自身 ROM 的加载上下文取得 PCI 设备，不扫描设备，也不检查厂商、型号或 BDF 地址。
- 宿主设备绑定、IOMMU、客体 PCI 资源和操作系统驱动已按直通需求配置。本项目只负责 OpRegion 的交接。

它不提供 GOP 开机图形输出、SR-IOV、Windows 驱动、主板 BIOS 更新或 Secure Boot 签名；生成 ROM 文件不会烧录显卡或主板。Code 43 有多种原因，单独加入这个 ROM 无法保证解决所有情况。

旧版本已在一套 PVE 9.2 / Arrow Lake-S `8086:7d67` / Windows 11 环境中完成两次客体关机后启动，并通过基本硬件 D3D11 回读；随后发现内部重启会沿用失效的 OpRegion。本次修改了该交接逻辑，修订后的 ROM 尚待部署验证。版本与验证范围见 [兼容性与验证](docs/兼容性与验证.md)。

## 在 Mac 上构建

以下命令从项目根目录执行。EDK2 是外部 SDK，放在项目旁；项目、SDK 和产物路径不能包含空白字符。

### 1. 安装工具

需要 Xcode Command Line Tools 或完整 Xcode、Homebrew、Git 和 Python 3。

```bash
# 已安装 Xcode 开发工具时可跳过第一行。
xcode-select --install
brew install llvm lld nasm
python3 --version
```

LLVM/Clang 将 C 编译为 X64 代码，LLD 链接 EFI 映像，NASM 编译 EDK2 依赖库中的汇编。构建脚本只调整自身进程的工具路径，无需修改 `.zshrc`。已验证工具版本为 LLVM/LLD 23.1.2、NASM 3.02；新版本工具仍需重新验证。

### 2. 获取固定版本 EDK2

```bash
git clone --depth 1 --branch edk2-stable202608 \
  https://github.com/tianocore/edk2.git ../edk2-rom-build

git -C ../edk2-rom-build rev-parse HEAD
# 固定版本：2970e5699ba6267f3384ffab20f96647578aebc8

git -C ../edk2-rom-build submodule update --init \
  MdePkg/Library/MipiSysTLib/mipisyst \
  MdeModulePkg/Library/BrotliCustomDecompressLib/brotli
```

仅构建本模块、依赖库及封装工具，无需构建整套 OVMF。源码通过 `PACKAGES_PATH` 引入 SDK，不复制进 EDK2 目录。

### 3. 生成 ROM

`--pci-id` 必须填写目标核显真实的十六进制 `厂商ID:设备ID`，可在 PVE 上用 `lspci -nn` 查询。下面以 `8086:7d67` 为例；它不是项目的默认型号，也不表示其他型号已验证。

```bash
bash tools/build.sh ../edk2-rom-build --pci-id 8086:7d67
```

默认使用 **RELEASE** 配置，生成：

```text
build/IgdOpRegion.efi       X64 UEFI 程序
build/intel-arl-igpu.rom    含 EFI 程序的 PCI Option ROM
build/build-info.json       工具版本、SDK 版本、源码与产物 SHA-256
build/LICENSE              项目许可
build/NOTICE               上游版权与依赖说明
build/licenses/            随产物保留的第三方许可
build/*.log                构建日志
```

需要固件调试输出时，将 DEBUG 产物放在单独目录，避免覆盖 RELEASE：

```bash
bash tools/build.sh ../edk2-rom-build --pci-id 8086:7d67 \
  --debug --output build/debug
```

`RELEASE` 使用优化和无日志的库配置；构建模式本身不代表某套硬件已通过运行验证。ROM 标准头仍须包含目标 PCI ID，封装脚本由参数填写这些字段；运行时 C 代码无需写死设备型号。

## 在已有 PVE 直通配置中使用

将 `build/intel-arl-igpu.rom` 复制到 PVE 宿主机的 `/usr/share/kvm/intel-arl-igpu.rom`。在虚拟机关机后，给**目标核显对应的现有 `hostpci` 条目**增加或替换 `romfile` 参数，保留已有设备地址和其他参数。例如，下面是配置格式模板：

```text
hostpci<N>: <目标 PCI 地址及已有参数>,romfile=intel-arl-igpu.rom
```

`<N>` 是现有配置槽位，尖括号部分需换成实际配置；不要把模板原样写入配置，也不要新增第二个指向同一核显的条目。ROM 必须随目标设备加载，不能作为独立 EFI 程序从 Shell 启动来代替这一流程。

保留原 `hostpci` 配置和原 ROM，完整关机后再启动客体。首先检查设备管理器状态与驱动版本，再验证应用确实使用 Intel 硬件适配器。显示设备名称或能进入 Windows 都不足以证明硬件功能正常。恢复时关闭客体，还原原有 `romfile` 设置即可。

## 开发与测试

```text
src/IgdOpRegionPkg/
  IgdOpRegion.c        入口 → 取得 ROM 所属设备 → 调用交接
  OpRegion.c / .h     fw_cfg、保留内存和 ASLS 地址发布
  Validate.c / .h     独立的 OpRegion / VBT / BDB 结构校验
  IgdOpRegion.inf     模块入口、源文件和库依赖
  IgdOpRegion.dsc     X64 目标与 RELEASE / DEBUG 配置
tools/
  build.sh           从源码生成 EFI 和 ROM
  pack.sh            将已有 EFI 封装为 ROM
  verify_rom.py      验证 ROM 容器及内嵌 EFI
  configure_vscode.py 生成本机 C/C++ 编辑器配置
tests/               C 数据校验、交接生命周期与 ROM 格式测试
docs/                原理导读、兼容性与验证范围
```

构建后运行本机测试：

```bash
bash tests/run.sh ../edk2-rom-build
```

C 测试用本机编译器执行真实 `Validate.c` 和 `OpRegion.c`，启用 AddressSanitizer 和 UndefinedBehaviorSanitizer。交接测试通过 mock 页分配、fw_cfg 和 PCI 接口检查非零 ASLS 时仍重新交接，以及发布前后的内存保留规则；ROM 测试检查容器字段、X64 架构、PE 节边界及 EFI 载荷。这些测试不执行固件、不访问 GPU，也不能验证真实 UEFI 内存图或显卡重启恢复；合成 VBT 不能作为真实显卡配置。部署验证的范围另见 [验证说明](docs/兼容性与验证.md)。

仅需改变 ROM 容器中的 PCI ID 时，可以复用已有 EFI；修改 C 源码后必须重新构建：

```bash
bash tools/pack.sh ../edk2-rom-build build/IgdOpRegion.efi \
  --pci-id 8086:7d67 --output build/intel-arl-igpu.rom

python3 tools/verify_rom.py build/intel-arl-igpu.rom \
  --efi build/IgdOpRegion.efi --pci-id 8086:7d67
```

### VS Code

使用微软 C/C++ 扩展。构建成功后自动生成 `.vscode/c_cpp_properties.json`，配置 EDK2 头文件、`AutoGen.h`、编译宏及 X64 UEFI 目标。该文件包含本机路径，由 `.gitignore` 排除。

SDK 路径变化后可重新构建，或单独刷新配置：

```bash
python3 tools/configure_vscode.py ../edk2-rom-build
```

编辑固件源码时选择 `UEFI X64` 配置；必要时执行 VS Code 的 `Developer: Reload Window`。`tests/` 的本机测试使用 macOS SDK，由测试脚本独立编译。

仓库提供 RELEASE / DEBUG 构建与测试的 GitHub Actions 配置。贡献和问题报告方式见 [CONTRIBUTING](CONTRIBUTING.md)；CI 检查不能代替真实硬件验证。

## 源码依据与许可

- [EDK2 固定版本](https://github.com/tianocore/edk2/tree/2970e5699ba6267f3384ffab20f96647578aebc8)：UEFI 接口、构建工具和 QEMU 固件库。
- [QEMU IGD 交接约定](https://github.com/qemu/qemu/blob/v11.0.3/docs/igd-assign.txt)：`etc/igd-opregion` 与 ASLS。
- [参考 IgdAssignmentDxe](https://github.com/tomitamoeko/VfioIgdPkg/blob/067328df2554c865cc0078cb922357301c4c36c6/IgdAssignmentDxe/IgdAssignment.c)：OpRegion 交接实现。

项目许可见 [LICENSE](LICENSE)，上游版权、参考来源和依赖许可说明见 [NOTICE](NOTICE)。EDK2 SDK 不包含在仓库中；分发链接了其库代码的 EFI 或 ROM 时，也应随附产物目录中的许可文件。
