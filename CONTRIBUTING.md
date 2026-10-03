# 参与开发

本项目维护一段用于 QEMU/OVMF 的 IGD OpRegion 交接代码，以及 Mac 构建和封装工具。提交改动前，请先阅读 [README](README.md)、[原理与代码导读](docs/原理与代码导读.md) 和 [兼容性与验证](docs/兼容性与验证.md)。

## 修改与检查

- `IgdOpRegion.c` 保持入口与所属设备定位；内存分配和地址交接放在 `OpRegion.c`，二进制格式检查放在 `Validate.c`。
- 保留原始 OpRegion/VBT 内容。更改格式解析或内存生命周期时，说明依据的规范或固定版本源码，并为新增边界补充有意义的测试。
- 运行时不固定 PCI ID 或 BDF；ROM 的标准设备元数据由调用者通过 `--pci-id` 提供。
- 注释和用户文档使用中文，解释接口的用途、数据的来源及失败时的处理。

在 Mac 上准备 README 指定的外部 EDK2，然后从项目根目录运行：

```bash
bash tools/build.sh ../edk2-rom-build --pci-id 8086:7d67
bash tests/run.sh ../edk2-rom-build

bash tools/build.sh ../edk2-rom-build --pci-id 8086:7d67 \
  --debug --output build/debug
bash tests/run.sh ../edk2-rom-build build/debug
```

这里的 PCI ID 是测试示例。C 测试以本机架构运行并启用 ASan/UBSan；ROM 测试只检查构建文件，均不能代替客体运行验证。GitHub Actions 对 RELEASE 和 DEBUG 执行相同流程，固定 EDK2 提交，Homebrew 工具版本会随 runner 更新。

PR 请说明具体问题、改动后的行为和已完成的检查。涉及硬件时，写明 PVE/QEMU/OVMF、GPU PCI ID、客体及驱动版本；区分编译通过、固件交接和实际 GPU 工作负载的结果。

## 报告问题

请使用 GitHub Issue 的问题模板。无需公开整份宿主机或虚拟机配置；提供与问题有关的设置和已脱敏日志即可。不要上传原始固件/VBT、客体磁盘或完整内存转储。

## 分发与许可

源码贡献采用项目的 [BSD-2-Clause 许可](LICENSE)。引用或移入第三方代码时保留其版权和许可，并更新 [NOTICE](NOTICE)。

`build/`、SDK 和自动生成的 `.vscode/c_cpp_properties.json` 不进入源码仓库。分发构建产物时，同时提供构建目录中的 `LICENSE`、`NOTICE`、`licenses/` 和 `build-info.json`；声明所用 PCI ID 与验证范围。不要将机器日志、个人路径或原始平台数据混入发布附件。
