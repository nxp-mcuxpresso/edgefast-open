import os
import re
import stat
from pathlib import Path
from collections import OrderedDict

def extract_subdirectories(cmake_content):
    """
    Extract all add_subdirectory calls from CMakeLists.txt content
    Supports four functions:
    - edgefast_open_add_subdirectory(dir)
    - edgefast_open_add_subdirectory_ifdef(config_option dir)
    - add_subdirectory_ifdef(config_option dir)
    - add_subdirectory(dir)

    Returns: [(function_name, config_option, subdir)]
    """
    subdirs = []

    # 1. Match edgefast_open_add_subdirectory(dir)
    pattern1 = r'edgefast_open_add_subdirectory\s*\(\s*([^)]+)\s*\)'
    for match in re.finditer(pattern1, cmake_content):
        subdir = match.group(1).strip()
        subdirs.append(('edgefast_open_add_subdirectory', None, subdir, match.start()))

    # 2. Match edgefast_open_add_subdirectory_ifdef(config_option dir)
    pattern2 = r'edgefast_open_add_subdirectory_ifdef\s*\(\s*([^)]+)\s*\)'
    for match in re.finditer(pattern2, cmake_content):
        args = match.group(1).strip().split()
        if len(args) >= 2:
            config_option = args[0]
            subdir = args[1]
            subdirs.append(('edgefast_open_add_subdirectory_ifdef', config_option, subdir, match.start()))

    # 3. Match add_subdirectory_ifdef(config_option dir)
    pattern3 = r'(?<!edgefast_open_)add_subdirectory_ifdef\s*\(\s*([^)]+)\s*\)'
    for match in re.finditer(pattern3, cmake_content):
        args = match.group(1).strip().split()
        if len(args) >= 2:
            config_option = args[0]
            subdir = args[1]
            subdirs.append(('add_subdirectory_ifdef', config_option, subdir, match.start()))

    # 4. Match add_subdirectory(dir)
    pattern4 = r'(?<!edgefast_open_)(?<!_ifdef\()add_subdirectory\s*\(\s*([^)]+)\s*\)'
    for match in re.finditer(pattern4, cmake_content):
        subdir = match.group(1).strip()
        # Filter out possible extra parameters (like EXCLUDE_FROM_ALL)
        subdir_parts = subdir.split()
        subdir = subdir_parts[0]
        subdirs.append(('add_subdirectory', None, subdir, match.start()))

    # Sort by order of appearance in file
    subdirs.sort(key=lambda x: x[3])

    # Return without position information
    return [(func, config, subdir) for func, config, subdir, _ in subdirs]

def is_source_file(filename):
    """
    Check if it is a source file
    """
    source_extensions = {'.c', '.cpp', '.cc', '.cxx', '.h', '.hpp', '.s', '.S', '.asm', '.dat'}
    return any(filename.endswith(ext) for ext in source_extensions)

def is_wildcard_pattern(word):
    """
    Check if it is a wildcard pattern
    """
    # Check if it contains wildcards
    if '*' in word or '?' in word:
        return True
    # Check if it is a standalone '.'
    if word == '.':
        return True
    return False

def convert_path_to_posix(path):
    """
    Convert path to POSIX format (using forward slashes)
    """
    return str(path).replace('\\', '/')

def process_wildcard_path(word, current_dir, base_dir):
    """
    Process wildcard paths
    Example: *.c -> ./source/bluetooth/host/*.c
             *.* -> ./source/bluetooth/host/*.*
             .   -> ./source/bluetooth (for include_directories)
    """
    # Remove possible trailing characters
    trailing = ''
    clean_word = word
    if word and word[-1] in ',)':
        trailing = word[-1]
        clean_word = word[:-1]

    # Skip if already starts with ./
    if clean_word.startswith('./'):
        return word

    # Get path relative to root directory
    try:
        rel_dir = current_dir.relative_to(base_dir)
        rel_dir_str = convert_path_to_posix(rel_dir)

        if clean_word == '.':
            # Standalone '.' is usually for include_directories, convert to relative path
            new_path = f'./{rel_dir_str}'
        else:
            # Wildcard pattern, like *.c, *.*
            new_path = f'./{rel_dir_str}/{clean_word}'

        return new_path + trailing
    except ValueError:
        return word

def replace_environment_variables(line, current_dir, base_dir):
    """
    Replace environment variables with relative path of current directory

    Args:
        line: Line to process
        current_dir: Directory containing current CMakeLists.txt file
        base_dir: Base directory path (edgefast_open directory)

    Returns:
        Line after replacement
    """
    # Define list of environment variables to replace
    env_vars = [
        '${ZEPHYR_ASRC_SW_MODULE_DIR}',
        '${ZEPHYR_LIBLC3_MODULE_DIR}',
        '${ZEPHYR_LIBSBC_MODULE_DIR}',
    ]

    result = line

    # Check if it contains any environment variables
    for env_var in env_vars:
        if env_var in result:
            # Calculate path of current directory relative to base directory
            try:
                rel_dir = current_dir.relative_to(base_dir)
                rel_path = './' + convert_path_to_posix(rel_dir)
                # Replace environment variable with relative path
                result = result.replace(env_var, rel_path)
            except ValueError:
                # If unable to calculate relative path, keep as is
                pass

    return result

def process_source_paths(line, current_dir, base_dir):
    """
    Process source file paths in a line, converting relative paths to paths relative to root directory
    If path already contains edgefast_open, do not add relative path prefix
    """

    # Skip comments
    if len(line.strip()) > 0 and '#' == (line.strip())[0]:
        return line

    # Check if line may contain paths
    if not any(pattern in line for pattern in ['.c', '.cpp', '.h', '.hpp', '.s', '.S', '.asm', '.dat', '*', '.', 'include_directories']):
        return line

    # Save original indentation
    leading_spaces = len(line) - len(line.lstrip())
    stripped_line = line.lstrip()

    # Split line, preserving spaces and format
    words = stripped_line.split()
    if not words:
        return line

    processed_words = []
    # Check if it is an include_directories related command
    is_include_dir_cmd = any(cmd in words[0] for cmd in ['include_directories', 'target_include_directories', 'zephyr_include_directories'])

    for i, word in enumerate(words):
        # Check if it contains left parenthesis (start of function call)
        if '(' in word:
            # Separate function name and arguments
            left_paren_idx = word.index('(')
            func_part = word[:left_paren_idx + 1]  # Include function name and left parenthesis
            arg_part = word[left_paren_idx + 1:]   # Argument part

            # If argument part is empty or just right parenthesis, keep as is
            if not arg_part or arg_part == ')':
                processed_words.append(word)
                continue

            # Process argument part
            trailing = ''
            clean_arg = arg_part

            # Handle trailing right parentheses
            trailing_parens = ''
            while clean_arg and clean_arg[-1] == ')':
                trailing_parens = ')' + trailing_parens
                clean_arg = clean_arg[:-1]

            # Handle other trailing characters
            if clean_arg and clean_arg[-1] in ',':
                trailing = clean_arg[-1]
                clean_arg = clean_arg[:-1]

            trailing = trailing + trailing_parens

            # Process argument
            processed_arg = clean_arg

            # If path already contains edgefast_open, keep as is
            if 'edgefast_open' in clean_arg:
                processed_words.append(func_part + processed_arg + trailing)
                continue

            # Check if it is a standalone '.'
            if clean_arg == '.' and is_include_dir_cmd:
                try:
                    rel_dir = current_dir.relative_to(base_dir)
                    rel_dir_str = convert_path_to_posix(rel_dir)
                    processed_arg = f'./{rel_dir_str}'
                except ValueError:
                    processed_arg = clean_arg
            # Check if it is a wildcard pattern
            elif is_wildcard_pattern(clean_arg):
                try:
                    rel_dir = current_dir.relative_to(base_dir)
                    rel_dir_str = convert_path_to_posix(rel_dir)
                    processed_arg = f'./{rel_dir_str}/{clean_arg}'
                except ValueError:
                    processed_arg = clean_arg
            # Check if it is a source file
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

        # Case without left parenthesis, process with original logic
        trailing = ''
        clean_word = word

        # Handle trailing right parentheses
        trailing_parens = ''
        while clean_word and clean_word[-1] == ')':
            trailing_parens = ')' + trailing_parens
            clean_word = clean_word[:-1]

        # Handle other trailing characters
        if clean_word and clean_word[-1] in ',':
            trailing = clean_word[-1]
            clean_word = clean_word[:-1]

        trailing = trailing + trailing_parens

        # If path already contains edgefast_open, keep as is
        if 'edgefast_open' in clean_word:
            processed_words.append(word)
            continue

        # Check if it is a standalone '.' (usually for include_directories)
        if clean_word == '.' and is_include_dir_cmd:
            new_word = process_wildcard_path(clean_word, current_dir, base_dir)
            processed_words.append(new_word + trailing)
            continue

        # Check if it is a wildcard pattern
        if is_wildcard_pattern(clean_word) and clean_word != '.':
            new_word = process_wildcard_path(clean_word, current_dir, base_dir)
            processed_words.append(new_word + trailing)
            continue

        # Check if it is a source file
        if is_source_file(clean_word):
            # Skip if already starts with ./
            if clean_word.startswith('./'):
                processed_words.append(word)
                continue

            # If it is an absolute path or contains ${...}, keep unchanged
            if clean_word.startswith('/') or clean_word.startswith('${') or ':' in clean_word:
                processed_words.append(word)
                continue

            # Build full path relative to current CMakeLists.txt
            file_path = current_dir / clean_word

            # Convert to path relative to root directory
            try:
                rel_path = file_path.relative_to(base_dir)
                # Add ./ prefix and convert to POSIX format
                new_path = './' + convert_path_to_posix(rel_path)
                processed_words.append(new_path + trailing)
            except ValueError:
                # If path is not under base_dir, keep as is
                processed_words.append(word)
        else:
            processed_words.append(word)

    # Reassemble line, preserving original indentation
    return ' ' * leading_spaces + ' '.join(processed_words)

def is_inside_function(lines, line_index):
    """
    Check if specified line is inside a function definition

    Args:
        lines: List of all lines
        line_index: Line index to check

    Returns:
        True if inside function, otherwise False
    """
    # Search backward to see if between function() and endfunction()
    function_depth = 0

    # Search backward from current line
    for i in range(line_index, -1, -1):
        line = lines[i].strip()

        # Check if it is endfunction()
        if re.match(r'^\s*endfunction\s*\(', line):
            function_depth += 1

        # Check if it is function()
        if re.match(r'^\s*function\s*\(', line):
            if function_depth == 0:
                # Found function containing current line, so it is inside function
                return True
            else:
                function_depth -= 1

    return False

def set_file_readonly(file_path):
    """
    Set file to read-only

    Args:
        file_path: File path
    """
    try:
        # Get current file permissions
        current_permissions = os.stat(file_path).st_mode

        # Set to read-only: remove all write permissions
        readonly_permissions = current_permissions & ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH)

        # Apply new permissions
        os.chmod(file_path, readonly_permissions)

        print(f"✓ File set to read-only")
    except Exception as e:
        print(f"Warning: Unable to set file to read-only: {e}")

def remove_readonly(file_path):
    """
    Remove read-only attribute from file

    Args:
        file_path: File path
    """
    try:
        if os.path.exists(file_path):
            # Get current file permissions
            current_permissions = os.stat(file_path).st_mode

            # Add write permission
            writable_permissions = current_permissions | stat.S_IWUSR

            # Apply new permissions
            os.chmod(file_path, writable_permissions)

            print(f"✓ Removed read-only attribute for regeneration")
    except Exception as e:
        print(f"Warning: Unable to remove read-only attribute: {e}")

def find_condition_for_subdirectory(lines, subdir_line_idx):
    """
    Find if() condition wrapping add_subdirectory call

    Args:
        lines: List of all lines
        subdir_line_idx: Line index of add_subdirectory call

    Returns:
        (condition, if_line_idx) if condition found, otherwise (None, None)
    """
    # Search backward for nearest if() statement
    if_depth = 0

    for i in range(subdir_line_idx - 1, -1, -1):
        line = lines[i].strip()

        # Skip comments and empty lines
        if not line or line.startswith('#'):
            continue

        # Check if it is endif()
        if re.match(r'^\s*endif\s*\(', line):
            if_depth += 1
            continue

        # Check if it is if()
        if_match = re.match(r'^\s*if\s*\(\s*([^)]+)\s*\)', line)
        if if_match:
            if if_depth == 0:
                # Found if directly wrapping current add_subdirectory
                # Check if there is only comments or empty lines between this if and add_subdirectory
                has_other_content = False
                for j in range(i + 1, subdir_line_idx):
                    check_line = lines[j].strip()
                    if check_line and not check_line.startswith('#'):
                        has_other_content = True
                        break

                if not has_other_content:
                    condition = if_match.group(1).strip()
                    return (condition, i)
                else:
                    return (None, None)
            else:
                if_depth -= 1

    return (None, None)

def find_matching_endif(lines, if_line_idx):
    """
    Find endif() matching if()

    Args:
        lines: List of all lines
        if_line_idx: Line index of if() statement

    Returns:
        endif_line_idx if found, otherwise None
    """
    if_depth = 1

    for i in range(if_line_idx + 1, len(lines)):
        line = lines[i].strip()

        # Skip comments and empty lines
        if not line or line.startswith('#'):
            continue

        # Check if it is if()
        if re.match(r'^\s*if\s*\(', line):
            if_depth += 1
            continue

        # Check if it is endif()
        if re.match(r'^\s*endif\s*\(', line):
            if_depth -= 1
            if if_depth == 0:
                return i

    return None

def process_cmake_file(cmake_path, base_dir, processed_files, output_lines, indent_level=0):
    """
    Recursively process CMakeLists.txt file
    """
    # Avoid duplicate processing
    abs_path = cmake_path.resolve()
    if abs_path in processed_files:
        return

    if not cmake_path.exists():
        print(f"Warning: File does not exist - {cmake_path}")
        return

    processed_files.add(abs_path)

    indent = "    " * indent_level
    try:
        relative_path = cmake_path.relative_to(base_dir)
    except ValueError:
        relative_path = cmake_path

    # Add comment marking start of this file
    output_lines.append(f"\n{indent}# ========================================")
    output_lines.append(f"{indent}# Content from: {convert_path_to_posix(relative_path)}")
    output_lines.append(f"{indent}# ========================================")

    try:
        with open(cmake_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        print(f"Error: Unable to read file {cmake_path}: {e}")
        return

    current_dir = cmake_path.parent

    # Extract all subdirectory calls
    subdirs = extract_subdirectories(content)

    # Find wrapping condition for each subdirectory
    subdir_conditions = {}
    lines = content.splitlines()

    for func_name, config_option, subdir in subdirs:
        # Find position of this call in file
        for line_idx, line in enumerate(lines):
            if config_option:
                pattern = rf'{func_name}\s*\(\s*{re.escape(config_option)}\s+{re.escape(subdir)}'
            else:
                pattern = rf'{func_name}\s*\(\s*{re.escape(subdir)}'

            if re.search(pattern, line):
                # Find wrapping condition
                condition, if_idx = find_condition_for_subdirectory(lines, line_idx)
                if condition:
                    endif_idx = find_matching_endif(lines, if_idx)
                    subdir_conditions[subdir] = {
                        'condition': condition,
                        'if_idx': if_idx,
                        'endif_idx': endif_idx,
                        'subdir_idx': line_idx
                    }
                break

    # Record lines to skip (already processed as conditional blocks)
    skip_lines = set()
    for subdir, info in subdir_conditions.items():
        if info['if_idx'] is not None and info['endif_idx'] is not None:
            # Mark all lines from if to endif as needing to be skipped
            for idx in range(info['if_idx'], info['endif_idx'] + 1):
                skip_lines.add(idx)

    # Process file content
    i = 0
    while i < len(lines):
        # If this line has already been processed as part of conditional block, skip
        if i in skip_lines:
            i += 1
            continue

        line = lines[i]
        stripped = line.strip()

        # Check if it is any kind of add_subdirectory call
        is_add_subdir = (
            'edgefast_open_add_subdirectory' in stripped or
            (stripped.startswith('add_subdirectory') and '(' in stripped)
        )

        if is_add_subdir:
            # Check if it is edgefast_open related function and inside function definition
            is_edgefast_open_func = (
                'edgefast_open_add_subdirectory_ifdef' in stripped or
                'edgefast_open_add_subdirectory' in stripped
            )

            if is_edgefast_open_func and is_inside_function(lines, i):
                # Inside function, keep as is without conversion
                output_lines.append(line)
            else:
                # Comment out original add_subdirectory call
                output_lines.append(f"{indent}# {line}")
        else:
            # First replace environment variables
            processed_line = replace_environment_variables(line, current_dir, base_dir)
            # Then process source file paths
            processed_line = process_source_paths(processed_line, current_dir, base_dir)
            output_lines.append(processed_line)

        i += 1

    # Recursively process all subdirectories
    for func_name, config_option, subdir in subdirs:
        # Check if this add_subdirectory call is inside function
        is_edgefast_open_func = func_name in ['edgefast_open_add_subdirectory', 'edgefast_open_add_subdirectory_ifdef']

        # Find position of this call in file
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

        # If it is edgefast_open function and inside function, skip expansion
        if is_edgefast_open_func and call_in_function:
            continue

        sub_cmake = current_dir / subdir / 'CMakeLists.txt'

        # Check if there is wrapping condition
        wrapper_condition = None
        if subdir in subdir_conditions:
            wrapper_condition = subdir_conditions[subdir]['condition']

        # Determine final condition to use
        final_condition = None
        if config_option:
            # edgefast_open_add_subdirectory_ifdef has its own condition
            final_condition = config_option
        elif wrapper_condition:
            # Use wrapping if() condition
            final_condition = wrapper_condition

        # Add explanatory comment
        output_lines.append(f"\n{indent}# ----------------------------------------")
        if final_condition:
            if wrapper_condition and not config_option:
                output_lines.append(f"{indent}# Conditional subdirectory: {subdir}")
                output_lines.append(f"{indent}# Condition: {final_condition} (from wrapper if)")
                output_lines.append(f"{indent}# Original: if({final_condition}) {func_name}({subdir}) endif()")
            else:
                output_lines.append(f"{indent}# Conditional subdirectory: {subdir}")
                output_lines.append(f"{indent}# Condition: {final_condition}")
                output_lines.append(f"{indent}# Original: {func_name}({final_condition} {subdir})")
            output_lines.append(f"{indent}# ----------------------------------------")
            # Add conditional check
            output_lines.append(f"{indent}if({final_condition})")
            # Recursively process subdirectory's CMakeLists.txt, increase indentation level
            process_cmake_file(sub_cmake, base_dir, processed_files, output_lines, indent_level + 1)
            # Add endif()
            output_lines.append(f"{indent}endif()")
        else:
            output_lines.append(f"{indent}# Subdirectory: {subdir}")
            output_lines.append(f"{indent}# Original: {func_name}({subdir})")
            output_lines.append(f"{indent}# ----------------------------------------")
            # Recursively process subdirectory's CMakeLists.txt, keep same indentation level
            process_cmake_file(sub_cmake, base_dir, processed_files, output_lines, indent_level)

def merge_cmake_files(root_cmake_path, output_path):
    """
    Merge all CMakeLists.txt files
    """
    root_path = Path(root_cmake_path).resolve()
    base_dir = root_path.parent

    print(f"Working directory: {os.getcwd()}")
    print(f"Root CMakeLists.txt: {root_path}")
    print(f"Base directory: {base_dir}")
    print(f"Output file: {Path(output_path).resolve()}\n")

    if not root_path.exists():
        print(f"Error: Root CMakeLists.txt file does not exist: {root_path}")
        return

    processed_files = set()
    output_lines = []

    # Add file header
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

    # Process root CMakeLists.txt
    process_cmake_file(root_path, base_dir, processed_files, output_lines)

    # Before writing output file, remove read-only attribute (if file already exists)
    output_path_abs = Path(output_path).resolve()
    remove_readonly(output_path_abs)

    # Write output file
    try:
        with open(output_path_abs, 'w', encoding='utf-8') as f:
            f.write('\n'.join(output_lines))
    except Exception as e:
        print(f"Error: Unable to write output file: {e}")
        return

    print(f"✓ Merge completed!")
    print(f"\nProcessed {len(processed_files)} CMakeLists.txt files:")
    for f in sorted(processed_files):
        try:
            rel_path = f.relative_to(base_dir)
        except ValueError:
            rel_path = f
        print(f"  - {convert_path_to_posix(rel_path)}")
    print(f"\nOutput file: {output_path_abs}")
    print(f"File size: {os.path.getsize(output_path_abs):,} bytes")

    # Set file to read-only
    set_file_readonly(output_path_abs)

def convert_zephyr_functions_to_mcux(input_path, output_path=None):
    """
    Convert zephyr functions to mcux format in CMakeLists file

    Args:
        input_path: Input file path
        output_path: Output file path, if None, overwrite input file

    Supported conversions:
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

    Note: If these functions are inside function() definitions, they will not be converted
    """
    input_path = Path(input_path).resolve()

    if output_path is None:
        output_path = input_path
    else:
        output_path = Path(output_path).resolve()

    print(f"\nStarting zephyr function conversion...")
    print(f"Input file: {input_path}")
    print(f"Output file: {output_path}")

    if not input_path.exists():
        print(f"Error: Input file does not exist: {input_path}")
        return

    # Remove read-only attribute for modification
    remove_readonly(input_path)

    try:
        with open(input_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        print(f"Error: Unable to read file {input_path}: {e}")
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

        # Check if it is zephyr_sources or zephyr_include_directories related function
        # Note: Match _ifdef version first, then regular version
        pattern = r'^\s*(zephyr_(?:library_)?(?:sources|include_directories)(?:_ifdef)?)\s*\('
        match = re.match(pattern, line)

        if match:
            func_name = match.group(1)

            # Check if inside function definition
            if is_inside_function(lines, i):
                # Inside function, skip conversion
                result_lines.append(line)
                if func_name in skipped_stats:
                    skipped_stats[func_name] += 1
                i += 1
                continue

            # Get original indentation
            indent = len(line) - len(line.lstrip())
            indent_str = ' ' * indent

            # Collect complete function call (may span multiple lines)
            full_call = line
            paren_count = line.count('(') - line.count(')')
            j = i

            while paren_count > 0 and j < len(lines) - 1:
                j += 1
                full_call += ' ' + lines[j].strip()
                paren_count += lines[j].count('(') - lines[j].count(')')

            # Determine if it is _ifdef version
            is_ifdef = func_name.endswith('_ifdef')

            # Determine if it is sources or include_directories
            is_sources = 'sources' in func_name
            is_includes = 'include_directories' in func_name

            if is_ifdef:
                # Parse _ifdef version: function_name(CONDITION items... [TOOLCHAINS toolchains...])
                full_pattern = r'(zephyr_(?:library_)?(?:sources|include_directories)_ifdef)\s*\(\s*(\w+)\s+(.*?)\)'
                full_match = re.search(full_pattern, full_call)

                if full_match:
                    condition = full_match.group(2)
                    args_str = full_match.group(3).strip()

                    # Separate items and TOOLCHAINS
                    toolchains_str = None
                    items_str = args_str

                    if 'TOOLCHAINS' in args_str:
                        parts = args_str.split('TOOLCHAINS')
                        items_str = parts[0].strip()
                        toolchains_str = parts[1].strip() if len(parts) > 1 else None

                    # Split item list
                    items = [f.strip() for f in items_str.split() if f.strip()]

                    # Build converted content
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
                # Parse regular version: function_name(items... [TOOLCHAINS toolchains...])
                full_pattern = r'(zephyr_(?:library_)?(?:sources|include_directories))\s*\((.*?)\)'
                full_match = re.search(full_pattern, full_call)

                if full_match:
                    args_str = full_match.group(2).strip()

                    # Separate items and TOOLCHAINS
                    toolchains_str = None
                    items_str = args_str

                    if 'TOOLCHAINS' in args_str:
                        parts = args_str.split('TOOLCHAINS')
                        items_str = parts[0].strip()
                        toolchains_str = parts[1].strip() if len(parts) > 1 else None

                    # Split item list
                    items = [f.strip() for f in items_str.split() if f.strip()]

                    # Build converted content (no if/endif needed)
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

        # Not a zephyr function, keep original line
        result_lines.append(line)
        i += 1

    # Write output file
    try:
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(result_lines))
    except Exception as e:
        print(f"Error: Unable to write output file: {e}")
        return

    print(f"✓ Conversion completed!")
    print(f"\nConversion statistics:")
    total_converted = 0
    for func, count in conversion_stats.items():
        if count > 0:
            print(f"  {func}: {count} occurrences")
            total_converted += count
    print(f"  Total: {total_converted} occurrences")

    total_skipped = sum(skipped_stats.values())
    if total_skipped > 0:
        print(f"\nSkipped statistics (inside functions):")
        for func, count in skipped_stats.items():
            if count > 0:
                print(f"  {func}: {count} occurrences")
        print(f"  Total: {total_skipped} occurrences")

    print(f"\nOutput file: {output_path}")
    print(f"File size: {os.path.getsize(output_path):,} bytes")

    # Set file to read-only
    set_file_readonly(output_path)

def extract_conditions_from_cmake(cmake_path):
    """
    Extract all conditional compilation conditions from CMakeLists.txt file
    Ignore conditions in comments

    Args:
        cmake_path: CMakeLists.txt file path

    Returns:
        OrderedDict: {original condition name: occurrence count}
    """
    cmake_path = Path(cmake_path).resolve()

    if not cmake_path.exists():
        print(f"Error: File does not exist: {cmake_path}")
        return OrderedDict()

    try:
        with open(cmake_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Error: Unable to read file {cmake_path}: {e}")
        return OrderedDict()

    conditions = OrderedDict()

    # Process line by line, ignoring comments
    for line in lines:
        # Remove comment part
        # Find position of #
        comment_pos = line.find('#')
        if comment_pos != -1:
            # Only process content before #
            line = line[:comment_pos]

        # If line is empty or only whitespace, skip
        if not line.strip():
            continue

        # Match if(CONFIG_XXX) and if(NOT CONFIG_XXX) patterns
        # Support multiple formats: if(CONFIG_XXX), if(NOT CONFIG_XXX), if(CONFIG_XXX AND CONFIG_YYY) etc.
        pattern = r'\bif\s*\(\s*(?:NOT\s+)?(CONFIG_\w+)'

        for match in re.finditer(pattern, line):
            condition = match.group(1)
            # Skip conditions already in MCUX_COMPONENT format
            if not condition.startswith('CONFIG_MCUX_COMPONENT_'):
                if condition in conditions:
                    conditions[condition] += 1
                else:
                    conditions[condition] = 1

    return conditions

def convert_config_name_to_mcux(config_name):
    """
    Convert CONFIG_XXX to CONFIG_MCUX_COMPONENT_middleware.edgefast_open.xxx format

    Args:
        config_name: Original config name, e.g. CONFIG_BT_CONN

    Returns:
        Converted config name, e.g. CONFIG_MCUX_COMPONENT_middleware.edgefast_open.bt_conn
    """
    # Remove CONFIG_ prefix
    name_without_prefix = config_name[7:]  # Remove "CONFIG_"

    # Convert to lowercase
    name_lower = name_without_prefix.lower()

    # Build new config name
    new_config_name = f'CONFIG_MCUX_COMPONENT_middleware.edgefast_open.{name_lower}'

    return new_config_name

def convert_config_name_to_kconfig(config_name):
    """
    Convert CONFIG_XXX to Kconfig config name (without CONFIG_ prefix)

    Args:
        config_name: Original config name, e.g. CONFIG_BT_CONN

    Returns:
        Kconfig config name, e.g. MCUX_COMPONENT_middleware.edgefast_open.bt_conn
    """
    mcux_name = convert_config_name_to_mcux(config_name)
    # Remove CONFIG_ prefix
    return mcux_name[7:]  # Remove "CONFIG_"

def replace_conditions_in_cmake(cmake_path, output_path=None, ignore_list=None):
    """
    Replace all conditions in CMakeLists.txt file with MCUX format
    Only replace conditions in non-comment parts

    Args:
        cmake_path: Input CMakeLists.txt file path
        output_path: Output file path, if None, overwrite input file
        ignore_list: List of conditions to ignore, these conditions will not be replaced

    Returns:
        Replacement condition mapping {original condition: new condition}
    """
    cmake_path = Path(cmake_path).resolve()

    if output_path is None:
        output_path = cmake_path
    else:
        output_path = Path(output_path).resolve()

    if ignore_list is None:
        ignore_list = []

    if not cmake_path.exists():
        print(f"Error: File does not exist: {cmake_path}")
        return {}

    # Remove read-only attribute for modification
    remove_readonly(cmake_path)

    try:
        with open(cmake_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Error: Unable to read file {cmake_path}: {e}")
        return {}

    # Extract all conditions
    conditions = extract_conditions_from_cmake(cmake_path)

    # Build replacement mapping, skip conditions in ignore list
    replacements = OrderedDict()
    for condition in conditions.keys():
        if condition in ignore_list:
            print(f"  Skipping ignored condition: {condition}")
            continue
        new_condition = convert_config_name_to_mcux(condition)
        replacements[condition] = new_condition

    # Process line by line, only replace non-comment parts
    modified_lines = []
    replacement_count = {}

    for line in lines:
        # Find comment position
        comment_pos = line.find('#')

        if comment_pos == -1:
            # No comment, entire line can be replaced
            modified_line = line
            for old_condition, new_condition in replacements.items():
                pattern = r'\b' + re.escape(old_condition) + r'\b'
                count = len(re.findall(pattern, modified_line))
                if count > 0:
                    modified_line = re.sub(pattern, new_condition, modified_line)
                    replacement_count[old_condition] = replacement_count.get(old_condition, 0) + count
            modified_lines.append(modified_line)
        else:
            # Has comment, process parts before and after comment separately
            code_part = line[:comment_pos]
            comment_part = line[comment_pos:]

            # Only replace code part
            modified_code = code_part
            for old_condition, new_condition in replacements.items():
                pattern = r'\b' + re.escape(old_condition) + r'\b'
                count = len(re.findall(pattern, modified_code))
                if count > 0:
                    modified_code = re.sub(pattern, new_condition, modified_code)
                    replacement_count[old_condition] = replacement_count.get(old_condition, 0) + count

            modified_lines.append(modified_code + comment_part)

    # Write output file
    try:
        with open(output_path, 'w', encoding='utf-8') as f:
            f.writelines(modified_lines)
    except Exception as e:
        print(f"Error: Unable to write file {output_path}: {e}")
        return {}

    # Print replacement statistics
    if replacement_count:
        print(f"\n  Replacement statistics:")
        for old_condition, count in replacement_count.items():
            new_condition = replacements[old_condition]
            print(f"    {old_condition} -> {new_condition}: {count} times")
    else:
        print(f"\n  No conditions found to replace")

    return replacements

def generate_kconfig_file(replacements, output_path):
    """
    Generate Kconfig.ecosystem file

    Args:
        replacements: Condition replacement mapping {original condition: new condition}
        output_path: Output Kconfig.ecosystem file path
    """
    output_path = Path(output_path).resolve()

    # Remove read-only attribute for modification
    remove_readonly(output_path)

    kconfig_lines = []

    # Add file header
    kconfig_lines.append("# Copyright 2025 NXP")
    kconfig_lines.append("#")
    kconfig_lines.append("# SPDX-License-Identifier: BSD-3-Clause")
    kconfig_lines.append("#")
    kconfig_lines.append("# !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
    kconfig_lines.append("# WARNING: This file is AUTO-GENERATED by merge_cmake.py script")
    kconfig_lines.append("# DO NOT MODIFY THIS FILE MANUALLY!")
    kconfig_lines.append("#")
    kconfig_lines.append("# To make changes:")
    kconfig_lines.append("#   1. Modify the source CMakeLists.txt files in their respective directories")
    kconfig_lines.append("#   2. Re-run the merge_cmake.py script to regenerate this file")
    kconfig_lines.append("#")
    kconfig_lines.append("# Any manual modifications to this file will be OVERWRITTEN when the script")
    kconfig_lines.append("# is run again.")
    kconfig_lines.append("# !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
    kconfig_lines.append("")
    kconfig_lines.append("# This file contains Kconfig definitions for EdgeFast Open middleware components")
    kconfig_lines.append("# Each configuration maps to a corresponding Zephyr configuration option")
    kconfig_lines.append("")

    # Generate Kconfig configuration for each condition
    for old_condition, new_condition in replacements.items():
        # Get Kconfig config name (without CONFIG_ prefix)
        kconfig_name = convert_config_name_to_kconfig(old_condition)

        # Get original condition name (without CONFIG_ prefix)
        original_name = old_condition[7:]  # Remove "CONFIG_"

        # Generate Kconfig configuration
        kconfig_lines.append(f"config {kconfig_name}")
        kconfig_lines.append(f"    def_bool {original_name}")
        kconfig_lines.append("")

    # Write file
    try:
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(kconfig_lines))
    except Exception as e:
        print(f"Error: Unable to write Kconfig file: {e}")
        return

    print(f"✓ Kconfig file generation completed!")
    print(f"\nGeneration statistics:")
    print(f"  Generated {len(replacements)} Kconfig configurations")
    print(f"\nOutput file: {output_path}")
    print(f"File size: {os.path.getsize(output_path):,} bytes")

    if len(replacements) > 0:
        print(f"\nFirst 10 Kconfig configuration examples:")
        for i, (old_condition, new_condition) in enumerate(list(replacements.items())[:10]):
            kconfig_name = convert_config_name_to_kconfig(old_condition)
            original_name = old_condition[7:]
            print(f"  config {kconfig_name}")
            print(f"      def_bool {original_name}")
            print()

    # Set file to read-only
    set_file_readonly(output_path)

if __name__ == "__main__":
    # Current working directory should be C:\_DDM\sdk-next\mcuxsdk\middleware\edgefast_open
    # Root CMakeLists_root.txt is in current directory
    root_cmake = "CMakeLists_root.txt"
    output_file = "CMakeLists.txt"
    kconfig_file = "Kconfig.ecosystem"

    # Step 1: Merge all CMakeLists.txt files
    print("=" * 80)
    print("Step 1: Merge all CMakeLists.txt files")
    print("=" * 80)
    merge_cmake_files(root_cmake, output_file)

    # Step 2: Convert zephyr functions to mcux format
    print("\n" + "=" * 80)
    print("Step 2: Convert zephyr functions to mcux format")
    print("=" * 80)
    convert_zephyr_functions_to_mcux(output_file)

    # Step 3: Replace conditions and generate Kconfig file
    print("\n" + "=" * 80)
    print("Step 3: Replace conditions and generate Kconfig file")
    print("=" * 80)

    # 3.1: Extract and replace conditions
    print("\n3.1: Extract and replace conditions in CMakeLists.txt...")
    ignore_list = ['CONFIG_BT_RX_PRIO']
    replacements = replace_conditions_in_cmake(output_file, ignore_list=ignore_list)

    # 3.2: Generate Kconfig.ecosystem file
    print("\n3.2: Generate Kconfig.ecosystem file...")
    generate_kconfig_file(replacements, kconfig_file)

    print("\n" + "=" * 80)
    print("All steps completed!")
    print("=" * 80)
    print(f"\nGenerated files:")
    print(f"  1. {Path(output_file).resolve()}")
    print(f"  2. {Path(kconfig_file).resolve()}")
