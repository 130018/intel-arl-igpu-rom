#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# 在 Mac 上运行纯数据测试；不执行 EFI，不访问设备。
set -euo pipefail
if (( $# < 1 || $# > 2 )); then
  printf '用法：bash tests/run.sh EDK2目录 [已构建产物目录]\n' >&2
  exit 2
fi
task_project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
task_edk2=$(cd -- "$1" && pwd)
task_out=${2:-$task_project/build}
case $(uname -m) in
  arm64) task_arch=AArch64 ;;
  x86_64) task_arch=X64 ;;
  *) printf '未支持的本机测试架构。\n' >&2; exit 2 ;;
esac
task_tmp=$(mktemp -d)
trap 'rm -rf -- "$task_tmp"' EXIT
# 这里只执行平台无关的数据校验代码，用本机架构和 AddressSanitizer。
# EFIAPI 留空是本机测试 ABI；正式 EFI 的调用约定由 EDK2 构建流程处理。
"$(xcrun --find clang)" -std=c11 -Wall -Wextra -Werror -fshort-wchar \
  -isysroot "$(xcrun --sdk macosx --show-sdk-path)" \
  -fsanitize=address,undefined -g -DEFIAPI= -DMDEPKG_NDEBUG \
  -I "$task_edk2/MdePkg/Include" -I "$task_edk2/MdePkg/Include/$task_arch" \
  -I "$task_project/src/IgdOpRegionPkg" \
  "$task_project/tests/validate_test.c" "$task_project/src/IgdOpRegionPkg/Validate.c" \
  -o "$task_tmp/validate_test"
"$task_tmp/validate_test"
PYTHONDONTWRITEBYTECODE=1 python3 "$task_project/tests/test_rom.py" "$task_out"
