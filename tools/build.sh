#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Mac 原生交叉编译：构建机运行 ARM64/x86_64 工具，产物固定为 X64 EFI。
# 主流程：检查依赖 → 构建 EDK2 工具 → 编译 EFI → 封装 ROM。
set -euo pipefail

usage() {
  printf '用法：bash tools/build.sh EDK2目录 --pci-id VVVV:DDDD [--output 目录] [--debug]\n'
}
if [[ ${1:-} == --help ]]; then usage; exit 0; fi
if (( $# < 1 )); then usage >&2; exit 2; fi
task_project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
task_rom_name="intel-arl-igpu.rom"
task_edk2=$(cd -- "$1" && pwd)
shift
task_pci_id=''
task_out="$task_project/build"
task_target=RELEASE
while (( $# )); do
  case "$1" in
    --pci-id|--output)
      [[ $# -ge 2 ]] || { usage >&2; exit 2; }
      case "$1" in
        --pci-id) task_pci_id=$2 ;;
        --output) task_out=$2 ;;
      esac
      shift 2 ;;
    --debug) task_target=DEBUG; shift ;;
    *) usage >&2; exit 2 ;;
  esac
done

[[ $(uname -s) == Darwin ]] || { printf '本脚本面向 macOS 构建。\n' >&2; exit 2; }
[[ -f "$task_edk2/edksetup.sh" ]] || { printf '不是 EDK2 源码目录。\n' >&2; exit 2; }
command -v brew >/dev/null || { printf '请先安装 Homebrew，再安装 llvm lld nasm。\n' >&2; exit 2; }
# llvm 和 lld 在 Homebrew 中是分开的包。仅调整本进程的 PATH。
task_llvm=$(brew --prefix llvm)
task_lld=$(brew --prefix lld)
export PATH="$task_llvm/bin:$task_lld/bin:$(brew --prefix nasm)/bin:$PATH"
for task_tool in clang llvm-lib lld-link nasm make python3; do
  command -v "$task_tool" >/dev/null || { printf '缺少 %s，请运行 brew install llvm lld nasm。\n' "$task_tool" >&2; exit 2; }
done
[[ -x "$task_llvm/bin/clang" ]] || { printf '缺少 Homebrew LLVM。\n' >&2; exit 2; }
task_out=$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$task_out")
# EDK2 自身的 make/元数据流程不可靠地支持带空白的路径，提前报告。
if [[ "$task_edk2$task_project$task_out" == *[[:space:]]* ]]; then
  printf 'EDK2、项目和产物路径不能包含空白字符。\n' >&2; exit 2
fi
PYTHONDONTWRITEBYTECODE=1 python3 - "$task_project/tools" "$task_pci_id" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
from verify_rom import parse_pci_id
try:
    parse_pci_id(sys.argv[2])
except ValueError as error:
    sys.exit(str(error))
PY

mkdir -p "$task_out"
export PYTHON_COMMAND=python3 PYTHONDONTWRITEBYTECODE=1
# 固定时间戳；构建身份仍需同时记录 SDK、工具版本及源码。
export SOURCE_DATE_EPOCH=1786522436
cd "$task_edk2"
# GenFw / EfiRom 是 Mac 程序，用系统编译器；它们不在虚拟机里运行。
if ! make -j"${JOBS:-4}" -C BaseTools/Source/C GenFw EfiRom \
  CC="$(xcrun --find clang)" CXX="$(xcrun --find clang++)" \
  > "$task_out/BaseTools.log" 2>&1; then
  tail -60 "$task_out/BaseTools.log" >&2; exit 1
fi

# PACKAGES_PATH 让 EDK2 直接读取项目源码，无需复制或改动 SDK 源文件。
unset WORKSPACE EDK_TOOLS_PATH CONF_PATH EDK_TOOLS_BIN PACKAGES_PATH PYTHONPATH
unset CLANG_BIN CLANG_HOST_BIN CLANGPDB_DLL NASM_PREFIX
export PACKAGES_PATH="$task_project/src"
set +u
source edksetup.sh BaseTools > "$task_out/setup.log" 2>&1
set -u
# 直接使用刚编译的本机工具，避免走依赖新版 Bash 的 SDK C 工具包装脚本。
export PATH="$task_edk2/BaseTools/Source/C/bin:$PATH"
# CLANGPDB 是 EDK2 标准工具链名，输出 PE/COFF；目标仍是 X64。
if ! build -p IgdOpRegionPkg/IgdOpRegion.dsc \
  -m IgdOpRegionPkg/IgdOpRegion.inf -a X64 -b "$task_target" -t CLANGPDB \
  -n "${JOBS:-4}" > "$task_out/compile.log" 2>&1; then
  tail -80 "$task_out/compile.log" >&2; exit 1
fi

cp "Build/IgdOpRegion/${task_target}_CLANGPDB/X64/IgdOpRegion.efi" "$task_out/IgdOpRegion.efi"
bash "$task_project/tools/pack.sh" "$task_edk2" "$task_out/IgdOpRegion.efi" \
  --pci-id "$task_pci_id" --output "$task_out/$task_rom_name"
# 配置、工具版本与哈希留在产物目录，避免生成的 ROM 与源码混淆。
python3 - "$task_project" "$task_edk2" "$task_out" "$task_target" "$task_pci_id" "$task_rom_name" <<'PY'
import hashlib, json, platform, subprocess, sys
from pathlib import Path
project, sdk, out = map(Path, sys.argv[1:4])
def version(command):
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT).splitlines()[0]
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
try:
    revision = version(['git', '-C', str(sdk), 'rev-parse', 'HEAD'])
except subprocess.CalledProcessError:
    revision = 'archive: see SDK source provenance'
info = {'host': platform.platform(), 'target': 'X64', 'build': sys.argv[4],
        'pci_id': sys.argv[5], 'edk2_revision': revision,
        'clang': version(['clang', '--version']), 'lld': version(['lld-link', '--version']),
        'nasm': version(['nasm', '-v']),
        'source_sha256': {str(p.relative_to(project)): digest(p) for p in sorted((project/'src').rglob('*')) if p.is_file()},
        'artifacts_sha256': {p.name: digest(p) for p in (out/'IgdOpRegion.efi', out/sys.argv[6])}}
(out/'build-info.json').write_text(json.dumps(info, indent=2, ensure_ascii=False)+'\n')
PY
# 编辑器配置与当前 SDK、目标配置使用同一来源。
python3 "$task_project/tools/configure_vscode.py" "$task_edk2" --target "$task_target"
# ROM 包含本项目与静态链接的 EDK2 代码；二进制分发须随附各自许可。
cp "$task_project/LICENSE" "$task_project/NOTICE" "$task_out/"
mkdir -p "$task_out/licenses"
cp -R "$task_project/licenses/." "$task_out/licenses/"
printf '构建完成（%s / X64）：%s\n' "$task_target" "$task_out"
