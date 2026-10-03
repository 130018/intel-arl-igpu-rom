#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# 封装已有 X64 EFI。PCI ID 由调用者提供，不在脚本中固定某个型号。
set -euo pipefail
usage() {
  printf '用法：bash tools/pack.sh EDK2目录 EFI文件 --pci-id VVVV:DDDD [--output ROM文件]\n'
}
if [[ ${1:-} == --help ]]; then usage; exit 0; fi
if (( $# < 2 )); then usage >&2; exit 2; fi
task_project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
task_edk2=$(cd -- "$1" && pwd)
task_efi=$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$2")
shift 2
task_pci_id=''
task_rom="$task_project/build/intel-arl-igpu.rom"
while (( $# )); do
  [[ $# -ge 2 ]] || { usage >&2; exit 2; }
  case "$1" in
    --pci-id) task_pci_id=$2 ;;
    --output) task_rom=$2 ;;
    *) usage >&2; exit 2 ;;
  esac
  shift 2
done
task_rom=$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$task_rom")
task_efirom="$task_edk2/BaseTools/Source/C/bin/EfiRom"
[[ -f "$task_efi" ]] || { printf '找不到 EFI：%s\n' "$task_efi" >&2; exit 2; }
[[ "$task_efi" != "$task_rom" ]] || { printf '输入 EFI 与输出 ROM 不能相同。\n' >&2; exit 2; }
[[ -f "$task_edk2/edksetup.sh" ]] || { printf '不是 EDK2 源码目录。\n' >&2; exit 2; }
# 先检查 CPU 架构/子系统与 ID，避免错误输入生成看似成功的 ROM。
PYTHONDONTWRITEBYTECODE=1 python3 - "$task_project/tools" "$task_efi" "$task_pci_id" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from verify_rom import check_pe, parse_pci_id
try:
    parse_pci_id(sys.argv[3])
    check_pe(Path(sys.argv[2]).read_bytes())
except (ValueError, OSError) as error:
    sys.exit(str(error))
PY

if [[ ! -x "$task_efirom" ]]; then
  make -j"${JOBS:-4}" -C "$task_edk2/BaseTools/Source/C" EfiRom PYTHON_COMMAND=python3
fi
mkdir -p "$(dirname -- "$task_rom")"
# 临时文件校验成功后再替换最终 ROM；失败不覆盖上一个有效产物。
task_tmp=$(mktemp "${task_rom}.tmp.XXXXXX")
trap 'rm -f -- "$task_tmp"' EXIT
"$task_efirom" -f "0x${task_pci_id%:*}" -i "0x${task_pci_id#*:}" -e "$task_efi" -o "$task_tmp"
python3 "$task_project/tools/verify_rom.py" "$task_tmp" --efi "$task_efi" --pci-id "$task_pci_id"
mv -f -- "$task_tmp" "$task_rom"
printf 'ROM：%s\n' "$task_rom"
