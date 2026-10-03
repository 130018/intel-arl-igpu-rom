#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""根据实际 EDK2 构建目录配置 VS Code C/C++，路径不写死在项目中。"""
import argparse
import json
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('edk2', type=Path, help='本次构建使用的 EDK2 目录')
    parser.add_argument('--target', choices=('RELEASE', 'DEBUG'), default='RELEASE')
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    sdk = args.edk2.resolve()
    autogen = (sdk / 'Build/IgdOpRegion' / f'{args.target}_CLANGPDB' /
               'X64/IgdOpRegionPkg/IgdOpRegion/DEBUG/AutoGen.h')
    includes = [project / 'src/IgdOpRegionPkg', sdk / 'MdePkg/Include',
                sdk / 'MdePkg/Include/X64', sdk / 'OvmfPkg/Include']
    for path in [autogen, *includes]:
        if not path.exists():
            parser.error(f'缺少 {path}；请先使用该 SDK 完成一次 {args.target} 构建。')
    llvm = Path(subprocess.check_output(['brew', '--prefix', 'llvm'], text=True).strip())
    compiler = llvm / 'bin/clang'
    if not compiler.is_file():
        parser.error('找不到 Homebrew LLVM，请运行 brew install llvm。')

    # 对齐 EDK2 CLANGPDB 的预处理环境，而不是构建机默认的 macOS/ARM64。
    # AutoGen.h 是 EDK2 自动生成的模块声明，不能自行伪造协议 GUID 声明。
    configuration = {
        'name': 'UEFI X64',
        'compilerPath': str(compiler),
        'compilerArgs': ['-target', 'x86_64-pc-windows-msvc', '-fshort-wchar',
                         '-funsigned-char', '-mno-red-zone'],
        'intelliSenseMode': 'windows-clang-x64',
        'cStandard': 'c17',
        'includePath': [str(path) for path in includes],
        'forcedInclude': [str(autogen)],
        'defines': ['__GNUC__=4', '__GNUC_MINOR__=2', '__GNUC_PATCHLEVEL__=1',
                    '__MINGW32__=1', 'EFIAPI=__attribute__((ms_abi))',
                    'STRING_ARRAY_NAME=IgdOpRegionStrings'],
    }
    output = project / '.vscode/c_cpp_properties.json'
    output.parent.mkdir(exist_ok=True)
    # 只更新本工具拥有的配置项，保留用户添加的其他配置。
    data = json.loads(output.read_text()) if output.exists() else {'configurations': [], 'version': 4}
    data['configurations'] = [configuration] + [
        item for item in data['configurations'] if item.get('name') != configuration['name']
    ]
    output.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n')
    print(f'VS Code C/C++ 配置已更新：{output}')


if __name__ == '__main__':
    main()
