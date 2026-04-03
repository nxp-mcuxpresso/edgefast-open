import os
import re
import stat
from pathlib import Path

def extract_subdirectories(cmake_content):
    """
    从 CMakeLists.txt 内容中提取所有 add_subdirectory 调用
    支持四种函数:
    - edgefast_open_add_subdirectory(dir)
    - edgefast_open_add_subdirectory_ifdef(config_option dir)
    - add_subdirectory_ifdef(config_option dir)
    - add_subdirectory(dir)

    返回: [(function_name, config_option, subdir)]
    """
    subdirs = []

    # 1. 匹配 edgefast_open_add_subdirectory(dir)
    pattern1 = r'edgefast_open_add_subdirectory\s*\(\s*([^)]+)\s*\)'
    for match in re.finditer(pattern1, cmake_content):
        subdir = match.group(1).strip()
        subdirs.append(('edgefast_open_add_subdirectory', None, subdir, match.start()))

    # 2. 匹配 edgefast_open_add_subdirectory_ifdef(config_option dir)
    pattern2 = r'edgefast_open_add_subdirectory_ifdef\s*\(\s*([^)]+)\s*\)'
    for match in re.finditer(pattern2, cmake_content):
        args = match.group(1).strip().split()
        if len(args) >= 2:
            config_option = args[0]
            subdir = args[1]
            subdirs.append(('edgefast_open_add_subdirectory_ifdef', config_option, subdir, match.start()))

    # 3. 匹配 add_subdirectory_ifdef(config_option dir)
    pattern3 = r'(?<!edgefast_open_)add_subdirectory_ifdef\s*\(\s*([^)]+)\s*\)'
    for match in re.finditer(pattern3, cmake_content):
        args = match.group(1).strip().split()
        if len(args) >= 2:
            config_option = args[0]
            subdir = args[1]
            subdirs.append(('add_subdirectory_ifdef', config_option, subdir, match.start()))

    # 4. 匹配 add_subdirectory(dir)
    pattern4 = r'(?<!edgefast_open_)(?<!_ifdef\()add_subdirectory\s*\(\s*([^)]+)\s*\)'
    for match in re.finditer(pattern4, cmake_content):
        subdir = match.group(1).strip()
        # 过滤掉可能的额外参数（如 EXCLUDE_FROM_ALL）
        subdir_parts = subdir.split()
        subdir = subdir_parts[0]
        subdirs.append(('add_subdirectory', None, subdir, match.start()))

    # 按照在文件中出现的顺序排序
    subdirs.sort(key=lambda x: x[3])

    # 返回时去掉位置信息
    return [(func, config, subdir) for func, config, subdir, _ in subdirs]

def is_source_file(filename):
    """
    判断是否是源文件
    """
    source_extensions = {'.c', '.cpp', '.cc', '.cxx', '.h', '.hpp', '.s', '.S', '.asm', '.dat'}
    return any(filename.endswith(ext) for ext in source_extensions)

def is_wildcard_pattern(word):
    """
    判断是否是通配符模式
    """
    # 检查是否包含通配符
    if '*' in word or '?' in word:
        return True
    # 检查是否是单独的 '.'
    if word == '.':
        return True
    return False

def convert_path_to_posix(path):
    """
    将路径转换为 POSIX 格式（使用正斜杠）
    """
    return str(path).replace('\\', '/')

def process_wildcard_path(word, current_dir, base_dir):
    """
    处理通配符路径
    例如: *.c -> ./source/bluetooth/host/*.c
          *.* -> ./source/bluetooth/host/*.*
          .   -> ./source/bluetooth (对于 include_directories)
    """
    # 移除可能的尾随字符
    trailing = ''
    clean_word = word
    if word and word[-1] in ',)':
        trailing = word[-1]
        clean_word = word[:-1]

    # 如果已经是以 ./ 开头的路径，跳过
    if clean_word.startswith('./'):
        return word

    # 获取相对于根目录的路径
    try:
        rel_dir = current_dir.relative_to(base_dir)
        rel_dir_str = convert_path_to_posix(rel_dir)

        if clean_word == '.':
            # 单独的 '.' 通常用于 include_directories，转换为相对路径
            new_path = f'./{rel_dir_str}'
        else:
            # 通配符模式，如 *.c, *.*
            new_path = f'./{rel_dir_str}/{clean_word}'

        return new_path + trailing
    except ValueError:
        return word

def replace_environment_variables(line, current_dir, base_dir):
    """
    替换环境变量为当前目录的相对路径

    参数:
        line: 要处理的行
        current_dir: 当前 CMakeLists.txt 文件所在目录
        base_dir: 基础目录路径（edgefast_open 目录）

    返回:
        替换后的行
    """
    # 定义需要替换的环境变量列表
    env_vars = [
        '${ZEPHYR_ASRC_SW_MODULE_DIR}',
        '${ZEPHYR_LIBLC3_MODULE_DIR}',
        '${ZEPHYR_LIBSBC_MODULE_DIR}',
    ]

    result = line

    # 检查是否包含任何环境变量
    for env_var in env_vars:
        if env_var in result:
            # 计算当前目录相对于基础目录的路径
            try:
                rel_dir = current_dir.relative_to(base_dir)
                rel_path = './' + convert_path_to_posix(rel_dir)
                # 替换环境变量为相对路径
                result = result.replace(env_var, rel_path)
            except ValueError:
                # 如果无法计算相对路径，保持原样
                pass

    return result

def process_source_paths(line, current_dir, base_dir):
    """
    处理一行中的源文件路径，将相对路径转换为相对于根目录的路径
    如果路径中已包含 edgefast_open，则不添加相对路径前缀
    """

    # 跳过注释
    if len(line.strip()) > 0 and '#' == (line.strip())[0]:
        return line

    # 检查是否是可能包含路径的行
    if not any(pattern in line for pattern in ['.c', '.cpp', '.h', '.hpp', '.s', '.S', '.asm', '.dat', '*', '.', 'include_directories']):
        return line

    # 保存原始缩进
    leading_spaces = len(line) - len(line.lstrip())
    stripped_line = line.lstrip()

    # 分割行，保留空格和格式
    words = stripped_line.split()
    if not words:
        return line

    processed_words = []
    # 检查是否是 include_directories 相关的命令
    is_include_dir_cmd = any(cmd in words[0] for cmd in ['include_directories', 'target_include_directories', 'zephyr_include_directories'])

    for i, word in enumerate(words):
        # 检查是否包含左括号（函数调用的开始）
        if '(' in word:
            # 分离函数名和参数
            left_paren_idx = word.index('(')
            func_part = word[:left_paren_idx + 1]  # 包含函数名和左括号
            arg_part = word[left_paren_idx + 1:]   # 参数部分

            # 如果参数部分为空或只是右括号，保持原样
            if not arg_part or arg_part == ')':
                processed_words.append(word)
                continue

            # 处理参数部分
            trailing = ''
            clean_arg = arg_part

            # 处理尾随的右括号
            trailing_parens = ''
            while clean_arg and clean_arg[-1] == ')':
                trailing_parens = ')' + trailing_parens
                clean_arg = clean_arg[:-1]

            # 处理其他尾随字符
            if clean_arg and clean_arg[-1] in ',':
                trailing = clean_arg[-1]
                clean_arg = clean_arg[:-1]

            trailing = trailing + trailing_parens

            # 处理参数
            processed_arg = clean_arg

            # 如果路径中已包含 edgefast_open，保持原样
            if 'edgefast_open' in clean_arg:
                processed_words.append(func_part + processed_arg + trailing)
                continue

            # 检查是否是单独的 '.'
            if clean_arg == '.' and is_include_dir_cmd:
                try:
                    rel_dir = current_dir.relative_to(base_dir)
                    rel_dir_str = convert_path_to_posix(rel_dir)
                    processed_arg = f'./{rel_dir_str}'
                except ValueError:
                    processed_arg = clean_arg
            # 检查是否是通配符模式
            elif is_wildcard_pattern(clean_arg):
                try:
                    rel_dir = current_dir.relative_to(base_dir)
                    rel_dir_str = convert_path_to_posix(rel_dir)
                    processed_arg = f'./{rel_dir_str}/{clean_arg}'
                except ValueError:
                    processed_arg = clean_arg
            # 检查是否是源文件
            elif is_source_file(clean_arg):
                if not clean_arg.startswith('./') and not clean_arg.startswith('/') and not clean_arg.startswith('${') and ':' not in clean_arg:
                    file_path = current_dir / clean_arg
                    try:
                        rel_path = file_path.relative_to(base_dir)
                        processed_arg = './' + convert_path_to_posix(rel_path)
                    except ValueError:
                        processed_arg = clean_arg

            processed_words.append(func_part + processed_arg + trailing)
            continue

        # 没有左括号的情况，按原来的逻辑处理
        trailing = ''
        clean_word = word

        # 处理尾随的右括号
        trailing_parens = ''
        while clean_word and clean_word[-1] == ')':
            trailing_parens = ')' + trailing_parens
            clean_word = clean_word[:-1]

        # 处理其他尾随字符
        if clean_word and clean_word[-1] in ',':
            trailing = clean_word[-1]
            clean_word = clean_word[:-1]

        trailing = trailing + trailing_parens

        # 如果路径中已包含 edgefast_open，保持原样
        if 'edgefast_open' in clean_word:
            processed_words.append(word)
            continue

        # 检查是否是单独的 '.' (通常用于 include_directories)
        if clean_word == '.' and is_include_dir_cmd:
            new_word = process_wildcard_path(clean_word, current_dir, base_dir)
            processed_words.append(new_word + trailing)
            continue

        # 检查是否是通配符模式
        if is_wildcard_pattern(clean_word) and clean_word != '.':
            new_word = process_wildcard_path(clean_word, current_dir, base_dir)
            processed_words.append(new_word + trailing)
            continue

        # 检查是否是源文件
        if is_source_file(clean_word):
            # 如果已经是以 ./ 开头的路径，跳过
            if clean_word.startswith('./'):
                processed_words.append(word)
                continue

            # 如果是绝对路径或包含 ${...}，保持不变
            if clean_word.startswith('/') or clean_word.startswith('${') or ':' in clean_word:
                processed_words.append(word)
                continue

            # 构建相对于当前 CMakeLists.txt 的完整路径
            file_path = current_dir / clean_word

            # 转换为相对于根目录的路径
            try:
                rel_path = file_path.relative_to(base_dir)
                # 添加 ./ 前缀并转换为 POSIX 格式
                new_path = './' + convert_path_to_posix(rel_path)
                processed_words.append(new_path + trailing)
            except ValueError:
                # 如果路径不在 base_dir 下，保持原样
                processed_words.append(word)
        else:
            processed_words.append(word)

    # 重新组合行，保持原有的缩进
    return ' ' * leading_spaces + ' '.join(processed_words)

def is_inside_function(lines, line_index):
    """
    检查指定行是否在函数定义内部

    参数:
        lines: 所有行的列表
        line_index: 要检查的行索引

    返回:
        True 如果在函数内部，否则 False
    """
    # 向前查找，看是否在 function() 和 endfunction() 之间
    function_depth = 0

    # 从当前行向前查找
    for i in range(line_index, -1, -1):
        line = lines[i].strip()

        # 检查是否是 endfunction()
        if re.match(r'^\s*endfunction\s*\(', line):
            function_depth += 1

        # 检查是否是 function()
        if re.match(r'^\s*function\s*\(', line):
            if function_depth == 0:
                # 找到了包含当前行的 function，说明在函数内部
                return True
            else:
                function_depth -= 1

    return False

def set_file_readonly(file_path):
    """
    将文件设置为只读

    参数:
        file_path: 文件路径
    """
    try:
        # 获取当前文件权限
        current_permissions = os.stat(file_path).st_mode

        # 设置为只读：移除所有写权限
        readonly_permissions = current_permissions & ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH)

        # 应用新权限
        os.chmod(file_path, readonly_permissions)

        print(f"✓ 文件已设置为只读")
    except Exception as e:
        print(f"警告: 无法设置文件为只读: {e}")

def remove_readonly(file_path):
    """
    移除文件的只读属性

    参数:
        file_path: 文件路径
    """
    try:
        if os.path.exists(file_path):
            # 获取当前文件权限
            current_permissions = os.stat(file_path).st_mode

            # 添加写权限
            writable_permissions = current_permissions | stat.S_IWUSR

            # 应用新权限
            os.chmod(file_path, writable_permissions)

            print(f"✓ 已移除文件的只读属性以便重新生成")
    except Exception as e:
        print(f"警告: 无法移除只读属性: {e}")

def process_cmake_file(cmake_path, base_dir, processed_files, output_lines, indent_level=0):
    """
    递归处理 CMakeLists.txt 文件
    """
    # 避免重复处理
    abs_path = cmake_path.resolve()
    if abs_path in processed_files:
        return

    if not cmake_path.exists():
        print(f"警告: 文件不存在 - {cmake_path}")
        return

    processed_files.add(abs_path)

    indent = "    " * indent_level
    try:
        relative_path = cmake_path.relative_to(base_dir)
    except ValueError:
        relative_path = cmake_path

    # 添加注释标记这个文件的开始
    output_lines.append(f"\n{indent}# ========================================")
    output_lines.append(f"{indent}# Content from: {convert_path_to_posix(relative_path)}")
    output_lines.append(f"{indent}# ========================================")

    try:
        with open(cmake_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        print(f"错误: 无法读取文件 {cmake_path}: {e}")
        return

    current_dir = cmake_path.parent

    # 提取所有子目录调用
    subdirs = extract_subdirectories(content)

    # 处理文件内容
    lines = content.splitlines()
    for line_idx, line in enumerate(lines):
        stripped = line.strip()

        # 检查是否是任何一种 add_subdirectory 调用
        is_add_subdir = (
            'edgefast_open_add_subdirectory' in stripped or
            (stripped.startswith('add_subdirectory') and '(' in stripped)
        )

        if is_add_subdir:
            # 检查是否是 edgefast_open 相关函数且在函数定义内部
            is_edgefast_open_func = (
                'edgefast_open_add_subdirectory_ifdef' in stripped or
                'edgefast_open_add_subdirectory' in stripped
            )

            if is_edgefast_open_func and is_inside_function(lines, line_idx):
                # 在函数内部，保持原样不转换
                output_lines.append(line)
            else:
                # 注释掉原始的 add_subdirectory 调用
                output_lines.append(f"{indent}# {line}")
        else:
            # 先替换环境变量
            processed_line = replace_environment_variables(line, current_dir, base_dir)
            # 再处理源文件路径
            processed_line = process_source_paths(processed_line, current_dir, base_dir)
            output_lines.append(processed_line)

    # 递归处理所有子目录
    for func_name, config_option, subdir in subdirs:
        # 检查这个 add_subdirectory 调用是否在函数内部
        # 需要找到这个调用在原文件中的位置
        is_edgefast_open_func = func_name in ['edgefast_open_add_subdirectory', 'edgefast_open_add_subdirectory_ifdef']

        # 查找这个调用在文件中的位置
        call_in_function = False
        if is_edgefast_open_func:
            for line_idx, line in enumerate(lines):
                if config_option:
                    pattern = rf'{func_name}\s*\(\s*{re.escape(config_option)}\s+{re.escape(subdir)}'
                else:
                    pattern = rf'{func_name}\s*\(\s*{re.escape(subdir)}'

                if re.search(pattern, line):
                    if is_inside_function(lines, line_idx):
                        call_in_function = True
                    break

        # 如果是 edgefast_open 函数且在函数内部，跳过展开
        if is_edgefast_open_func and call_in_function:
            continue

        sub_cmake = current_dir / subdir / 'CMakeLists.txt'

        # 添加说明注释
        output_lines.append(f"\n{indent}# ----------------------------------------")
        if config_option:
            output_lines.append(f"{indent}# Conditional subdirectory: {subdir}")
            output_lines.append(f"{indent}# Condition: {config_option}")
            output_lines.append(f"{indent}# Original: {func_name}({config_option} {subdir})")
            output_lines.append(f"{indent}# ----------------------------------------")
            # 添加条件判断
            output_lines.append(f"{indent}if({config_option})")
        else:
            output_lines.append(f"{indent}# Subdirectory: {subdir}")
            output_lines.append(f"{indent}# Original: {func_name}({subdir})")
            output_lines.append(f"{indent}# ----------------------------------------")

        # 递归处理子目录的 CMakeLists.txt
        process_cmake_file(sub_cmake, base_dir, processed_files, output_lines, indent_level)

        # 如果有条件，添加 endif()
        if config_option:
            output_lines.append(f"{indent}endif()")

def merge_cmake_files(root_cmake_path, output_path):
    """
    合并所有 CMakeLists.txt 文件
    """
    root_path = Path(root_cmake_path).resolve()
    base_dir = root_path.parent

    print(f"工作目录: {os.getcwd()}")
    print(f"根 CMakeLists.txt: {root_path}")
    print(f"基础目录: {base_dir}")
    print(f"输出文件: {Path(output_path).resolve()}\n")

    if not root_path.exists():
        print(f"错误: 根 CMakeLists.txt 文件不存在: {root_path}")
        return

    processed_files = set()
    output_lines = []

    # 添加文件头
    output_lines.append("# Copyright 2025 NXP")
    output_lines.append("#")
    output_lines.append("# SPDX-License-Identifier: BSD-3-Clause")
    output_lines.append("#")
    output_lines.append("# !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
    output_lines.append("# WARNING: This file is AUTO-GENERATED by merge_cmake.py script")
    output_lines.append("# DO NOT MODIFY THIS FILE MANUALLY!")
    output_lines.append("#")
    output_lines.append("# To make changes:")
    output_lines.append("#   1. Modify the source CMakeLists.txt files in their respective directories")
    output_lines.append("#   2. Re-run the merge_cmake.py script to regenerate this file")
    output_lines.append("#")
    output_lines.append("# Any manual modifications to this file will be OVERWRITTEN when the script")
    output_lines.append("# is run again.")
    output_lines.append("# !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
    output_lines.append("#")
    output_lines.append("# This file is auto-generated by merging all CMakeLists.txt files")
    output_lines.append(f"# Generated from: {convert_path_to_posix(root_path)}")
    output_lines.append(f"# Working directory: {os.getcwd()}")
    output_lines.append("#")
    output_lines.append("# Supported add_subdirectory functions:")
    output_lines.append("#   - edgefast_open_add_subdirectory(dir)")
    output_lines.append("#   - edgefast_open_add_subdirectory_ifdef(config_option dir)")
    output_lines.append("#   - add_subdirectory_ifdef(config_option dir)")
    output_lines.append("#   - add_subdirectory(dir)")
    output_lines.append("#")
    output_lines.append("# Note: Conditional subdirectories are wrapped with if(CONFIG)/endif()")
    output_lines.append("# Note: All source file paths and wildcards are converted to relative paths from the root directory")
    output_lines.append("#   Examples:")
    output_lines.append("#     *.c    -> ./source/path/*.c")
    output_lines.append("#     *.*    -> ./source/path/*.*")
    output_lines.append("#     .      -> ./source/path (for include_directories)")
    output_lines.append("# Note: edgefast_open_add_subdirectory* calls inside function definitions are not expanded")
    output_lines.append("# Note: Environment variables are replaced with the relative path of the file containing them:")
    output_lines.append("#   ${ZEPHYR_ASRC_SW_MODULE_DIR}  -> ./lib/asrc_sw (in lib/asrc_sw/CMakeLists.txt)")
    output_lines.append("#   ${ZEPHYR_LIBLC3_MODULE_DIR}   -> ./lib/liblc3 (in lib/liblc3/CMakeLists.txt)")
    output_lines.append("#   ${ZEPHYR_LIBSBC_MODULE_DIR}   -> ./lib/libsbc (in lib/libsbc/CMakeLists.txt)")
    output_lines.append("")

    # 处理根 CMakeLists.txt
    process_cmake_file(root_path, base_dir, processed_files, output_lines)

    # 写入输出文件前，先移除只读属性（如果文件已存在）
    output_path_abs = Path(output_path).resolve()
    remove_readonly(output_path_abs)

    # 写入输出文件
    try:
        with open(output_path_abs, 'w', encoding='utf-8') as f:
            f.write('\n'.join(output_lines))
    except Exception as e:
        print(f"错误: 无法写入输出文件: {e}")
        return

    print(f"✓ 合并完成！")
    print(f"\n共处理 {len(processed_files)} 个 CMakeLists.txt 文件:")
    for f in sorted(processed_files):
        try:
            rel_path = f.relative_to(base_dir)
        except ValueError:
            rel_path = f
        print(f"  - {convert_path_to_posix(rel_path)}")
    print(f"\n输出文件: {output_path_abs}")
    print(f"文件大小: {os.path.getsize(output_path_abs):,} 字节")

    # 设置文件为只读
    set_file_readonly(output_path_abs)

def convert_zephyr_functions_to_mcux(input_path, output_path=None):
    """
    转换 CMakeLists 文件中的 zephyr 函数为 mcux 格式

    参数:
        input_path: 输入文件路径
        output_path: 输出文件路径，如果为 None，则覆盖输入文件

    支持的转换:
    1. zephyr_library_sources_ifdef(CONFIG_XXX file1 file2 [TOOLCHAINS toolchains])
       =>
       if(CONFIG_XXX)
           mcux_add_source(SOURCES file1 [TOOLCHAINS toolchains])
           mcux_add_source(SOURCES file2 [TOOLCHAINS toolchains])
       endif()

    2. zephyr_sources_ifdef(CONFIG_XXX file1 file2 [TOOLCHAINS toolchains])
       =>
       if(CONFIG_XXX)
           mcux_add_source(SOURCES file1 [TOOLCHAINS toolchains])
           mcux_add_source(SOURCES file2 [TOOLCHAINS toolchains])
       endif()

    3. zephyr_library_sources(file1 file2 [TOOLCHAINS toolchains])
       =>
       mcux_add_source(SOURCES file1 [TOOLCHAINS toolchains])
       mcux_add_source(SOURCES file2 [TOOLCHAINS toolchains])

    4. zephyr_sources(file1 file2 [TOOLCHAINS toolchains])
       =>
       mcux_add_source(SOURCES file1 [TOOLCHAINS toolchains])
       mcux_add_source(SOURCES file2 [TOOLCHAINS toolchains])

    5. zephyr_include_directories_ifdef(CONFIG_XXX path1 path2 [TOOLCHAINS toolchains])
       =>
       if(CONFIG_XXX)
           mcux_add_include(INCLUDES path1 [TOOLCHAINS toolchains])
           mcux_add_include(INCLUDES path2 [TOOLCHAINS toolchains])
       endif()

    6. zephyr_include_directories(path1 path2 [TOOLCHAINS toolchains])
       =>
       mcux_add_include(INCLUDES path1 [TOOLCHAINS toolchains])
       mcux_add_include(INCLUDES path2 [TOOLCHAINS toolchains])

    7. zephyr_library_include_directories_ifdef(CONFIG_XXX path1 path2 [TOOLCHAINS toolchains])
       =>
       if(CONFIG_XXX)
           mcux_add_include(INCLUDES path1 [TOOLCHAINS toolchains])
           mcux_add_include(INCLUDES path2 [TOOLCHAINS toolchains])
       endif()

    8. zephyr_library_include_directories(path1 path2 [TOOLCHAINS toolchains])
       =>
       mcux_add_include(INCLUDES path1 [TOOLCHAINS toolchains])
       mcux_add_include(INCLUDES path2 [TOOLCHAINS toolchains])

    注意: 如果这些函数在 function() 定义内部，则不会被转换
    """
    input_path = Path(input_path).resolve()

    if output_path is None:
        output_path = input_path
    else:
        output_path = Path(output_path).resolve()

    print(f"\n开始转换 zephyr 函数...")
    print(f"输入文件: {input_path}")
    print(f"输出文件: {output_path}")

    if not input_path.exists():
        print(f"错误: 输入文件不存在: {input_path}")
        return

    # 移除只读属性以便修改
    remove_readonly(input_path)

    try:
        with open(input_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        print(f"错误: 无法读取文件 {input_path}: {e}")
        return

    lines = content.splitlines()
    result_lines = []
    i = 0
    conversion_stats = {
        'zephyr_library_sources_ifdef': 0,
        'zephyr_sources_ifdef': 0,
        'zephyr_library_sources': 0,
        'zephyr_sources': 0,
        'zephyr_include_directories_ifdef': 0,
        'zephyr_include_directories': 0,
        'zephyr_library_include_directories_ifdef': 0,
        'zephyr_library_include_directories': 0
    }
    skipped_stats = {
        'zephyr_library_sources_ifdef': 0,
        'zephyr_sources_ifdef': 0,
        'zephyr_library_sources': 0,
        'zephyr_sources': 0,
        'zephyr_include_directories_ifdef': 0,
        'zephyr_include_directories': 0,
        'zephyr_library_include_directories_ifdef': 0,
        'zephyr_library_include_directories': 0
    }

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        # 检查是否是 zephyr_sources 或 zephyr_include_directories 相关函数
        # 注意：先匹配 _ifdef 版本，再匹配普通版本
        pattern = r'^\s*(zephyr_(?:library_)?(?:sources|include_directories)(?:_ifdef)?)\s*\('
        match = re.match(pattern, line)

        if match:
            func_name = match.group(1)

            # 检查是否在函数定义内部
            if is_inside_function(lines, i):
                # 在函数内部，跳过转换
                result_lines.append(line)
                if func_name in skipped_stats:
                    skipped_stats[func_name] += 1
                i += 1
                continue

            # 获取原始缩进
            indent = len(line) - len(line.lstrip())
            indent_str = ' ' * indent

            # 收集完整的函数调用（可能跨多行）
            full_call = line
            paren_count = line.count('(') - line.count(')')
            j = i

            while paren_count > 0 and j < len(lines) - 1:
                j += 1
                full_call += ' ' + lines[j].strip()
                paren_count += lines[j].count('(') - lines[j].count(')')

            # 判断是否是 _ifdef 版本
            is_ifdef = func_name.endswith('_ifdef')

            # 判断是 sources 还是 include_directories
            is_sources = 'sources' in func_name
            is_includes = 'include_directories' in func_name

            if is_ifdef:
                # 解析 _ifdef 版本: function_name(CONDITION items... [TOOLCHAINS toolchains...])
                full_pattern = r'(zephyr_(?:library_)?(?:sources|include_directories)_ifdef)\s*\(\s*(\w+)\s+(.*?)\)'
                full_match = re.search(full_pattern, full_call)

                if full_match:
                    condition = full_match.group(2)
                    args_str = full_match.group(3).strip()

                    # 分离项目和 TOOLCHAINS
                    toolchains_str = None
                    items_str = args_str

                    if 'TOOLCHAINS' in args_str:
                        parts = args_str.split('TOOLCHAINS')
                        items_str = parts[0].strip()
                        toolchains_str = parts[1].strip() if len(parts) > 1 else None

                    # 分割项目列表
                    items = [f.strip() for f in items_str.split() if f.strip()]

                    # 构建转换后的内容
                    result_lines.append(f'{indent_str}if({condition})')

                    for item in items:
                        if is_sources:
                            result_lines.append(f'{indent_str}    mcux_add_source(')
                            result_lines.append(f'{indent_str}        SOURCES {item}')
                        elif is_includes:
                            result_lines.append(f'{indent_str}    mcux_add_include(')
                            result_lines.append(f'{indent_str}        INCLUDES {item}')

                        if toolchains_str:
                            result_lines.append(f'{indent_str}        TOOLCHAINS {toolchains_str}')
                        result_lines.append(f'{indent_str}    )')

                    result_lines.append(f'{indent_str}endif()')

                    if func_name in conversion_stats:
                        conversion_stats[func_name] += 1
                    i = j + 1
                    continue
            else:
                # 解析普通版本: function_name(items... [TOOLCHAINS toolchains...])
                full_pattern = r'(zephyr_(?:library_)?(?:sources|include_directories))\s*\((.*?)\)'
                full_match = re.search(full_pattern, full_call)

                if full_match:
                    args_str = full_match.group(2).strip()

                    # 分离项目和 TOOLCHAINS
                    toolchains_str = None
                    items_str = args_str

                    if 'TOOLCHAINS' in args_str:
                        parts = args_str.split('TOOLCHAINS')
                        items_str = parts[0].strip()
                        toolchains_str = parts[1].strip() if len(parts) > 1 else None

                    # 分割项目列表
                    items = [f.strip() for f in items_str.split() if f.strip()]

                    # 构建转换后的内容（不需要 if/endif）
                    for item in items:
                        if is_sources:
                            result_lines.append(f'{indent_str}mcux_add_source(')
                            result_lines.append(f'{indent_str}    SOURCES {item}')
                        elif is_includes:
                            result_lines.append(f'{indent_str}mcux_add_include(')
                            result_lines.append(f'{indent_str}    INCLUDES {item}')

                        if toolchains_str:
                            result_lines.append(f'{indent_str}    TOOLCHAINS {toolchains_str}')
                        result_lines.append(f'{indent_str})')

                    if func_name in conversion_stats:
                        conversion_stats[func_name] += 1
                    i = j + 1
                    continue

        # 不是 zephyr 函数，保留原始行
        result_lines.append(line)
        i += 1

    # 写入输出文件
    try:
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(result_lines))
    except Exception as e:
        print(f"错误: 无法写入输出文件: {e}")
        return

    print(f"✓ 转换完成！")
    print(f"\n转换统计:")
    total_converted = 0
    for func, count in conversion_stats.items():
        if count > 0:
            print(f"  {func}: {count} 处")
            total_converted += count
    print(f"  总计: {total_converted} 处")

    total_skipped = sum(skipped_stats.values())
    if total_skipped > 0:
        print(f"\n跳过统计 (函数内部):")
        for func, count in skipped_stats.items():
            if count > 0:
                print(f"  {func}: {count} 处")
        print(f"  总计: {total_skipped} 处")

    print(f"\n输出文件: {output_path}")
    print(f"文件大小: {os.path.getsize(output_path):,} 字节")

    # 设置文件为只读
    set_file_readonly(output_path)

if __name__ == "__main__":
    # 当前工作目录应该是 C:\_DDM\sdk-next\mcuxsdk\middleware\edgefast_open
    # 根 CMakeLists_root.txt 在当前目录
    root_cmake = "CMakeLists_root.txt"
    output_file = "CMakeLists.txt"

    # 第一步：合并所有 CMakeLists.txt 文件
    merge_cmake_files(root_cmake, output_file)

    # 第二步：转换 zephyr 函数为 mcux 格式
    convert_zephyr_functions_to_mcux(output_file)
