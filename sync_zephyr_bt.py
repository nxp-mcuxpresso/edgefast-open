#!/usr/bin/env python3
"""
sync_zephyr_bt.py - Sync Zephyr Bluetooth stack to Edgefast Open

This script synchronizes the Zephyr Bluetooth stack into the Edgefast Open middleware,
applying all necessary porting transformations automatically.

Usage:
    python sync_zephyr_bt.py [options]

Options:
    --zephyr-dir PATH    Path to Zephyr root (default: C:/_DDM/zephyrproject/zephyr)
    --ef-dir PATH        Path to Edgefast Open root (default: auto-detected from script location)
    --dry-run            Show what would be changed without making changes
    --verbose            Show detailed output
    --subsystem SUBSYS   Only sync a specific subsystem (host, audio, services, common, crypto, mesh)
    --help               Show this help message

Porting rules applied:
    1. Source .c/.h files are copied verbatim (no modification needed)
    2. CMakeLists.txt files at bluetooth/ level are transformed
    3. Kconfig files have specific text substitutions applied
    4. Controller/ Kconfig is replaced with custom NXP version (not synced from Zephyr)
    5. Mesh/ Kconfig is replaced with custom stub (not synced from Zephyr)
    6. Include headers for classic/ are synced verbatim (structural changes were in .c files)
    7. Extra files in Edgefast Open not present in Zephyr are preserved
"""

import os
import sys
import re
import shutil
import hashlib
import argparse
import logging
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

DEFAULT_ZEPHYR_DIR = r"C:\_DDM\zephyrproject\zephyr"

# These paths are relative to the Edgefast Open root
ZEPHYR_BT_SUBSYS = "subsys/bluetooth"
ZEPHYR_BT_INCLUDE = "include/zephyr/bluetooth"

EF_BT_SOURCE = "source/bluetooth"
EF_BT_INCLUDE = "include/zephyr/bluetooth"

# ---------------------------------------------------------------------------
# Additional sync mappings outside source/bluetooth
# Format: list of (zephyr_relative_path, ef_relative_path)
# These are direct path mappings synced verbatim (no transformations needed)
# ---------------------------------------------------------------------------

# lib/ files that have the same relative path in both Zephyr and Edgefast Open
ZEPHYR_LIB_SYNC_DIRS = [
    # (zephyr_dir_relative, ef_dir_relative)
    # lib/net_buf: Zephyr lib/net_buf/ -> EF lib/net_buf/
    ("lib/net_buf", "lib/net_buf"),
    # lib/utils: Zephyr lib/utils/ -> EF lib/utils/
    ("lib/utils", "lib/utils"),
    # lib/uuid: Zephyr lib/uuid/ -> EF lib/uuid/
    ("lib/uuid", "lib/uuid"),
]

# include/zephyr/bluetooth: already handled by sync_bluetooth_includes()
# The following include/zephyr directories are EF porting SHIMS - DO NOT SYNC from Zephyr:
#   include/zephyr/kernel.h, device.h, devicetree.h, irq.h, spinlock.h,
#   include/zephyr/arch/, include/zephyr/debug/, include/zephyr/drivers/bluetooth.h,
#   include/zephyr/logging/, include/zephyr/shell/, include/zephyr/sys/,
#   include/zephyr/kernel/, include/zephyr/dt-bindings/, include/zephyr/sys_clock.h
#
# RULE: Only sync include/zephyr/bluetooth/ and include/zephyr/net_buf.h,
#       include/zephyr/toolchain*.h, include/zephyr/types.h, include/zephyr/init.h
#       (files that are IDENTICAL between Zephyr and EF - these are safe to sync).
#       Everything else under include/zephyr/ is an EF porting shim.

# EF-only directories (not in Zephyr at all - never sync these):
EF_ONLY_DIRS = {
    "lib/liblc3",
    "lib/libsbc",
    "lib/asrc_sw",
    "lib/crc",
    "lib/libc",
    "source/bluetooth/controller/hci",  # NXP custom HCI driver
    "source/bluetooth/crypto",          # partially custom (bt_crypto_mbedtls.c)
    "source/bluetooth/services/fmp.c",  # EF-only services
    "source/bluetooth/services/hps.c",
    "source/bluetooth/services/hts.c",
    "source/bluetooth/services/ipsp.c",
    "source/bluetooth/services/pxr.c",
    "source/bluetooth/services/tip.c",
    "source/bluetooth/services/wus.c",
}

# Special-case path remappings: EF file comes from a DIFFERENT Zephyr path
# Format: (ef_relative_path, zephyr_relative_path)
SPECIAL_PATH_REMAPS = [
    # services/bas.c in EF flat layout maps to services/bas/bas.c in Zephyr
    ("source/bluetooth/services/bas.c", "subsys/bluetooth/services/bas/bas.c"),
]

# ---------------------------------------------------------------------------
# bt_shell_private.c / bt_shell_private.h transforms
#
# EF does NOT use the Zephyr shell backend APIs (shell_backend_count_get,
# shell_backend_get, shell_vfprintf, etc.) - the porting layer provides its
# own implementation of bt_shell_fprintf* through fsl_shell.h.
#
# Porting rule:
#   bt_shell_private.c  - wrap the entire implementation body in #if 0 / #endif
#                         (keep only the #include lines unchanged)
#   bt_shell_private.h  - wrap all declarations/macros in #if 0 / #endif
#                         (keep only #ifndef guard + #include unchanged)
# ---------------------------------------------------------------------------

def _transform_bt_shell_private_c(content: str) -> str:
    """Wrap the implementation body of bt_shell_private.c in #if 0 / #endif.

    Keeps the file header comments and #include lines unchanged.
    Wraps everything from the first static/void/function definition onwards.
    """
    lines = content.splitlines(keepends=True)
    insert_idx = None
    for i, line in enumerate(lines):
        # Find the line after the last #include / empty line preamble block
        stripped = line.strip()
        if stripped.startswith('#include'):
            insert_idx = i + 1  # after this include, start the #if 0 block
        elif insert_idx is not None and stripped and not stripped.startswith('//') \
                and not stripped.startswith('/*') and not stripped.startswith('*') \
                and not stripped.startswith('#'):
            # First non-blank, non-comment, non-preprocessor line after includes
            break

    if insert_idx is None:
        return content  # nothing to transform

    # Check if already wrapped
    for line in lines[insert_idx:]:
        if line.strip() == '#if 0':
            return content  # already has the guard

    # Insert #if 0 after the last include line, and #endif at end
    result = lines[:insert_idx]
    result.append('#if 0\n')
    result.extend(lines[insert_idx:])
    # Remove trailing newline from last line, add #endif
    while result and result[-1].strip() == '':
        result.pop()
    result.append('#endif\n')
    return ''.join(result)


def _transform_bt_shell_private_h(content: str) -> str:
    """Wrap the declarations/macros of bt_shell_private.h in #if 0 / #endif.

    Keeps the #ifndef guard, #define guard, and #include lines.
    Wraps everything from the first __printf_like declaration / /**  doc comment onwards.
    The closing #endif (guard) is preserved after the #endif for #if 0.
    """
    lines = content.splitlines(keepends=True)

    # Find insertion point: after the last #include line in the header
    insert_idx = None
    end_guard_idx = None

    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith('#include'):
            insert_idx = i + 1
        # Find the final #endif (header guard close)
        if stripped == '#endif /* __BT_SHELL_PRIVATE_H */':
            end_guard_idx = i

    if insert_idx is None:
        return content

    # Check if already wrapped
    for line in lines[insert_idx:]:
        if line.strip() == '#if 0':
            return content

    # Build output:
    # [0..insert_idx] + \n#if 0\n + [insert_idx..end_guard_idx] + #endif\n + [end_guard_idx..]
    if end_guard_idx is not None:
        result = (lines[:insert_idx]
                  + ['\n', '#if 0\n']
                  + lines[insert_idx:end_guard_idx]
                  + ['#endif\n']
                  + lines[end_guard_idx:])
    else:
        result = lines[:insert_idx] + ['\n', '#if 0\n'] + lines[insert_idx:]
        while result and result[-1].strip() == '':
            result.pop()
        result.append('#endif\n')
    return ''.join(result)

# ---------------------------------------------------------------------------
# Files / directories that should NOT be synced from Zephyr
# (Edgefast Open has completely custom versions)
# ---------------------------------------------------------------------------

# These paths are relative to source/bluetooth (or include/zephyr/bluetooth)
SKIP_SYNC_PATHS = {
    # Controller has a custom CMakeLists.txt and Kconfig for NXP HCI
    "source/bluetooth/controller/CMakeLists.txt",
    "source/bluetooth/controller/Kconfig",
    # Mesh Kconfig is a custom stub (mesh sources are synced, just not Kconfig)
    "source/bluetooth/mesh/Kconfig",
}

# Subdirectories whose *entire content* should not be synced from Zephyr
# (they exist in Zephyr but were intentionally excluded from EF)
SKIP_SYNC_DIRS = {
    # These nordic/openisa hardware-specific controller subdirs don't exist in EF
    "source/bluetooth/controller/ll_sw/nordic",
    "source/bluetooth/controller/ll_sw/openisa",
    "source/bluetooth/controller/ll_sw/shell",
    "source/bluetooth/controller/ll_sw",
    "source/bluetooth/controller/ticker",
    "source/bluetooth/controller/util",
    "source/bluetooth/controller/coex",
    "source/bluetooth/controller/flash",
    "source/bluetooth/controller/hci",
    "source/bluetooth/controller/hal",
    "source/bluetooth/controller/include",
    "source/bluetooth/controller/crypto",
    # Mesh sources are intentionally excluded from EF (not ported yet)
    "source/bluetooth/mesh",
}

# EF include/zephyr directories that are porting SHIMS - never sync from Zephyr
# (these differ from Zephyr because they adapt the Zephyr API to run on FreeRTOS/MCUXpresso)
SKIP_INCLUDE_ZEPHYR_DIRS = {
    "include/zephyr/arch",
    "include/zephyr/debug",
    "include/zephyr/drivers",
    "include/zephyr/dt-bindings",
    "include/zephyr/kernel",
    "include/zephyr/logging",
    "include/zephyr/shell",
    # NOTE: include/zephyr/sys is NOT entirely skipped - most files are verbatim
    # copies from Zephyr. Only the 4 shim files listed below are excluded.
    "include/zephyr/syscalls",
}

# EF include/zephyr top-level files that are porting shims
SKIP_INCLUDE_ZEPHYR_FILES = {
    "include/zephyr/kernel.h",
    "include/zephyr/device.h",
    "include/zephyr/devicetree.h",
    "include/zephyr/irq.h",
    "include/zephyr/spinlock.h",
    "include/zephyr/sys_clock.h",
    "include/zephyr/autoconf.h",
    "include/soc.h",
    # include/zephyr/sys/ - only these 3 files are porting shims; the rest are verbatim
    # Note: util.h is NOT skipped - it needs a specific transform (see sync_include_zephyr)
    "include/zephyr/sys/clock.h",
    "include/zephyr/sys/printk.h",
    "include/zephyr/sys/time_units.h",
}

# ---------------------------------------------------------------------------
# CMakeLists.txt transformation rules
# Each entry: (pattern_regex, replacement_string)
# Applied in order to the bluetooth-level CMakeLists.txt only
# ---------------------------------------------------------------------------

BT_ROOT_CMAKE_RULES = [
    # add_library → zephyr_add_library
    (r'\badd_library\(', r'zephyr_add_library('),
    # target_include_directories → zephyr_target_include_directories
    (r'\btarget_include_directories\(', r'zephyr_target_include_directories('),
    # add_subdirectory( → edgefast_open_add_subdirectory(
    # But only plain add_subdirectory (not add_subdirectory_ifdef)
    (r'\badd_subdirectory\((?!_)', r'edgefast_open_add_subdirectory('),
    # add_subdirectory_ifdef( → edgefast_open_add_subdirectory_ifdef(
    (r'\badd_subdirectory_ifdef\(', r'edgefast_open_add_subdirectory_ifdef('),
]

# Replacement for the entire bluetooth root CMakeLists.txt
# We use a template because the transformation is structural enough
BT_ROOT_CMAKE_TEMPLATE = """\
# SPDX-License-Identifier: Apache-2.0

zephyr_add_library(subsys__bluetooth INTERFACE)

zephyr_target_include_directories(subsys__bluetooth INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

edgefast_open_add_subdirectory(common)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT_HCI host)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT_HCI controller)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT_CONN services)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT_MESH mesh)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT_AUDIO audio)
edgefast_open_add_subdirectory_ifdef(CONFIG_BT_CRYPTO crypto)

zephyr_include_directories(.)

edgefast_open_add_subdirectory(lib)
"""

# ---------------------------------------------------------------------------
# Kconfig transformation rules
# Each entry: (compiled_regex, replacement_string)
# ---------------------------------------------------------------------------

KCONFIG_TEXT_RULES = [
    # $(UINT8_MAX) → 255
    (re.compile(r'\$\(UINT8_MAX\)'), '255'),
    # $(UINT16_MAX) → 65535
    (re.compile(r'\$\(UINT16_MAX\)'), '65535'),
    # $(INT16_MAX) → 32768
    (re.compile(r'\$\(INT16_MAX\)'), '32768'),
    # $(UINT32_MAX) → 4294967295
    (re.compile(r'\$\(UINT32_MAX\)'), '4294967295'),
    # Logging template paths: subsys/logging/ → middleware/edgefast_open/source/porting/
    # Handle both bare path and $(ZEPHYR_BASE)/subsys/logging/ prefix variants
    (re.compile(r'\$\(ZEPHYR_BASE\)/subsys/logging/Kconfig\.template\.log_config_inherit'),
     'middleware/edgefast_open/source/porting/Kconfig.template.log_config_inherit'),
    (re.compile(r'\$\(ZEPHYR_BASE\)/subsys/logging/Kconfig\.template\.log_config'),
     'middleware/edgefast_open/source/porting/Kconfig.template.log_config'),
    (re.compile(r'subsys/logging/Kconfig\.template\.log_config_inherit'),
     'middleware/edgefast_open/source/porting/Kconfig.template.log_config_inherit'),
    (re.compile(r'subsys/logging/Kconfig\.template\.log_config'),
     'middleware/edgefast_open/source/porting/Kconfig.template.log_config'),
    # BT_SHELL: select SHELL → select PORT_SHELL, select SHELL_GETOPT → select GETOPT
    (re.compile(r'^\tselect SHELL$', re.MULTILINE), '\tselect PORT_SHELL'),
    (re.compile(r'^\tselect SHELL_GETOPT$', re.MULTILINE), '\tselect GETOPT'),
    # BT_SETTINGS: depends on SETTINGS → depends on PORT_SETTINGS
    # (keep 'depends on' keyword, just replace the symbol name)
    (re.compile(r'\bdepends on SETTINGS\b'), 'depends on PORT_SETTINGS'),
    # BT_DIS_SETTINGS: select SETTINGS → select PORT_SETTINGS
    (re.compile(r'\bselect SETTINGS\b'), 'select PORT_SETTINGS'),
]

# Additional Kconfig rule: insert rsource "services/Kconfig" after rsource "lib/Kconfig"
# Only applied to the bluetooth root Kconfig
BT_ROOT_KCONFIG_EXTRA_RSOURCE = (
    re.compile(r'(rsource "lib/Kconfig"\n)'),
    'rsource "lib/Kconfig"\nrsource "services/Kconfig"\n'
)

# ---------------------------------------------------------------------------
# Kconfig.logging transformation rule: per-line source path substitution
# (already covered by KCONFIG_TEXT_RULES above)
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Services CMakeLists.txt: extra sources to add (files present in EF but not Zephyr)
# ---------------------------------------------------------------------------

SERVICES_CMAKE_EXTRA_SOURCES = """\
zephyr_sources_ifdef(CONFIG_BT_WUS wus.c)

zephyr_sources_ifdef(CONFIG_BT_FMP fmp.c)

zephyr_sources_ifdef(CONFIG_BT_HPS hps.c)

zephyr_sources_ifdef(CONFIG_BT_HTS hts.c)

zephyr_sources_ifdef(CONFIG_BT_IPSP ipsp.c)

zephyr_sources_ifdef(CONFIG_BT_PXR pxr.c)

zephyr_sources_ifdef(CONFIG_BT_TIP tip.c)

"""

SERVICES_CMAKE_INSERT_AFTER = re.compile(r'(zephyr_sources_ifdef\(CONFIG_BT_TPS\s+tps\.c\)\n)')

# ---------------------------------------------------------------------------
# Services Kconfig: extra rsource entries to add
# ---------------------------------------------------------------------------

SERVICES_KCONFIG_EXTRA_RSOURCE = """\
rsource "Kconfig.wus"

rsource "Kconfig.fmp"

rsource "Kconfig.hps"

rsource "Kconfig.hts"

rsource "Kconfig.ipsp"

rsource "Kconfig.pxr"

rsource "Kconfig.tip"

"""

SERVICES_KCONFIG_INSERT_AFTER = re.compile(r'(rsource "Kconfig\.tps"\n)')

# ---------------------------------------------------------------------------
# crypto/CMakeLists.txt: add PSA/mbedtls conditional
# ---------------------------------------------------------------------------

CRYPTO_CMAKE_PSA_BLOCK = """\
if(CONFIG_BT_CRYPTO_PSA)
zephyr_library_sources(bt_crypto_psa.c)
else()
zephyr_library_sources(bt_crypto_mbedtls.c)
endif()

"""

CRYPTO_CMAKE_REPLACE = (
    re.compile(r'zephyr_library_sources\(bt_crypto_psa\.c\)\n'),
    CRYPTO_CMAKE_PSA_BLOCK
)

# ---------------------------------------------------------------------------
# crypto/Kconfig: add PSA config entries
# ---------------------------------------------------------------------------

CRYPTO_KCONFIG_EXTRA = """
config PSA_CRYPTO
\tbool "PSA Crypto Support"
\thelp
\t  Enable support for PSA (Platform Security Architecture) Crypto

config BT_CRYPTO_PSA
\tbool "PSA Crypto Support for Bluetooth"
\tdepends on BT_CRYPTO
\thelp
\t  This option enables PSA Crypto API support for Bluetooth cryptographic operations.

config PSA_CRYPTO_CLIENT
\tbool
\tdefault y
\thelp
\t  Enable support for PSA (Platform Security Architecture) Crypto Client
"""

# Match the end of the existing BT_CRYPTO block (last help line before EOF)
CRYPTO_KCONFIG_INSERT_AFTER = re.compile(
    r'(\timply MBEDTLS_AES_ROM_TABLES if MBEDTLS_PSA_CRYPTO_C\n\thelp\n.*?Toolbox\.\n)',
    re.DOTALL
)

# ---------------------------------------------------------------------------
# Utility functions
# ---------------------------------------------------------------------------

def file_hash(path: str) -> Optional[str]:
    """Return MD5 hash of file, or None if file doesn't exist."""
    try:
        with open(path, 'rb') as f:
            return hashlib.md5(f.read()).hexdigest()
    except OSError:
        return None


def read_text(path: str) -> str:
    """Read file as UTF-8 text, replacing errors."""
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        return f.read()


def write_text(path: str, content: str, dry_run: bool = False) -> bool:
    """Write text to file. Returns True if file was changed."""
    # Check if content differs from existing file
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            existing = f.read()
        if existing == content:
            return False
    except OSError:
        pass

    if not dry_run:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(content)
    return True


def copy_file(src: str, dst: str, dry_run: bool = False) -> bool:
    """Copy file from src to dst. Returns True if file was changed."""
    src_hash = file_hash(src)
    dst_hash = file_hash(dst)
    if src_hash == dst_hash:
        return False
    if not dry_run:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
    return True


def apply_kconfig_rules(content: str, extra_rules=None) -> str:
    """Apply all standard Kconfig transformation rules."""
    for pattern, replacement in KCONFIG_TEXT_RULES:
        content = pattern.sub(replacement, content)
    if extra_rules:
        for pattern, replacement in extra_rules:
            content = pattern.sub(replacement, content)
    return content


def normalize_path(path: str) -> str:
    """Normalize path separators to forward slashes for display."""
    return path.replace('\\', '/')


# ---------------------------------------------------------------------------
# Per-file sync logic
# ---------------------------------------------------------------------------

class SyncEngine:
    def __init__(self, zephyr_dir: str, ef_dir: str, dry_run: bool = False, verbose: bool = False):
        self.zephyr_dir = os.path.normpath(zephyr_dir)
        self.ef_dir = os.path.normpath(ef_dir)
        self.dry_run = dry_run
        self.verbose = verbose
        self.stats = {'copied': 0, 'transformed': 0, 'skipped': 0, 'unchanged': 0, 'errors': 0}

        self.zephyr_bt = os.path.join(self.zephyr_dir, ZEPHYR_BT_SUBSYS.replace('/', os.sep))
        self.zephyr_inc = os.path.join(self.zephyr_dir, ZEPHYR_BT_INCLUDE.replace('/', os.sep))
        self.ef_bt = os.path.join(self.ef_dir, EF_BT_SOURCE.replace('/', os.sep))
        self.ef_inc = os.path.join(self.ef_dir, EF_BT_INCLUDE.replace('/', os.sep))

    def log(self, msg: str):
        if self.verbose:
            logging.info(msg)

    def log_change(self, action: str, rel_path: str):
        prefix = "[DRY-RUN] " if self.dry_run else ""
        logging.info(f"{prefix}{action}: {normalize_path(rel_path)}")

    def _ef_rel(self, ef_abs: str) -> str:
        """Return relative path from ef_dir root."""
        return os.path.relpath(ef_abs, self.ef_dir)

    def _is_skipped(self, ef_abs: str) -> bool:
        """Check if this file/dir is in the skip list."""
        ef_rel = self._ef_rel(ef_abs).replace('\\', '/')
        if ef_rel in SKIP_SYNC_PATHS:
            return True
        for skip_dir in SKIP_SYNC_DIRS:
            if ef_rel == skip_dir or ef_rel.startswith(skip_dir + '/'):
                return True
        return False

    def sync_source_file(self, zf: str, ef: str):
        """Sync a source file (.c or internal .h) - copy verbatim."""
        if self._is_skipped(ef):
            self.log(f"SKIP: {self._ef_rel(ef)}")
            self.stats['skipped'] += 1
            return
        try:
            changed = copy_file(zf, ef, self.dry_run)
            if changed:
                self.log_change("COPY", self._ef_rel(ef))
                self.stats['copied'] += 1
            else:
                self.stats['unchanged'] += 1
        except Exception as e:
            logging.error(f"Error copying {zf}: {e}")
            self.stats['errors'] += 1

    def sync_kconfig(self, zf: str, ef: str, extra_rules=None):
        """Sync a Kconfig file with transformations."""
        if self._is_skipped(ef):
            self.log(f"SKIP (custom): {self._ef_rel(ef)}")
            self.stats['skipped'] += 1
            return
        try:
            content = read_text(zf)
            transformed = apply_kconfig_rules(content, extra_rules)
            changed = write_text(ef, transformed, self.dry_run)
            if changed:
                self.log_change("TRANSFORM-KCONFIG", self._ef_rel(ef))
                self.stats['transformed'] += 1
            else:
                self.stats['unchanged'] += 1
        except Exception as e:
            logging.error(f"Error transforming Kconfig {zf}: {e}")
            self.stats['errors'] += 1

    def sync_cmake(self, zf: str, ef: str, transform_fn=None):
        """Sync a CMakeLists.txt, optionally applying transform."""
        if self._is_skipped(ef):
            self.log(f"SKIP (custom): {self._ef_rel(ef)}")
            self.stats['skipped'] += 1
            return
        try:
            if transform_fn is not None:
                content = read_text(zf)
                transformed = transform_fn(content)
                changed = write_text(ef, transformed, self.dry_run)
                if changed:
                    self.log_change("TRANSFORM-CMAKE", self._ef_rel(ef))
                    self.stats['transformed'] += 1
                else:
                    self.stats['unchanged'] += 1
            else:
                changed = copy_file(zf, ef, self.dry_run)
                if changed:
                    self.log_change("COPY", self._ef_rel(ef))
                    self.stats['copied'] += 1
                else:
                    self.stats['unchanged'] += 1
        except Exception as e:
            logging.error(f"Error syncing CMakeLists {zf}: {e}")
            self.stats['errors'] += 1

    # ------------------------------------------------------------------
    # Per-file transform functions
    # ------------------------------------------------------------------

    def _sync_transformed_source(self, zf: str, ef: str, transform_fn, action: str = 'TRANSFORM-SOURCE'):
        """Sync a source file applying a transformation function."""
        if self._is_skipped(ef):
            self.log(f"SKIP: {self._ef_rel(ef)}")
            self.stats['skipped'] += 1
            return
        try:
            content = read_text(zf)
            transformed = transform_fn(content)
            changed = write_text(ef, transformed, self.dry_run)
            if changed:
                self.log_change(action, self._ef_rel(ef))
                self.stats['transformed'] += 1
            else:
                self.stats['unchanged'] += 1
        except Exception as e:
            logging.error(f"Error transforming {zf}: {e}")
            self.stats['errors'] += 1

    def _transform_bt_root_cmake(self, content: str) -> str:
        """Transform bluetooth/CMakeLists.txt."""
        return BT_ROOT_CMAKE_TEMPLATE

    def _transform_bt_root_kconfig(self, content: str) -> str:
        """Transform bluetooth/Kconfig."""
        content = apply_kconfig_rules(content)
        # Add rsource "services/Kconfig" after rsource "lib/Kconfig"
        content = BT_ROOT_KCONFIG_EXTRA_RSOURCE[0].sub(
            BT_ROOT_KCONFIG_EXTRA_RSOURCE[1], content, count=1
        )
        return content

    def _transform_services_cmake(self, content: str) -> str:
        """Transform services/CMakeLists.txt - add extra BT service sources."""
        # Check if extra sources already present
        if 'CONFIG_BT_WUS' in content:
            return content  # already has the extra entries
        content = SERVICES_CMAKE_INSERT_AFTER.sub(
            r'\1' + SERVICES_CMAKE_EXTRA_SOURCES, content, count=1
        )
        return content

    def _transform_services_kconfig(self, content: str) -> str:
        """Transform services/Kconfig - add extra rsource entries."""
        content = apply_kconfig_rules(content)
        if 'Kconfig.wus' in content:
            return content  # already has the extra entries
        content = SERVICES_KCONFIG_INSERT_AFTER.sub(
            r'\1' + SERVICES_KCONFIG_EXTRA_RSOURCE, content, count=1
        )
        return content

    def _transform_crypto_cmake(self, content: str) -> str:
        """Transform crypto/CMakeLists.txt - add PSA/mbedtls conditional."""
        if 'CONFIG_BT_CRYPTO_PSA' in content:
            return content  # already transformed
        content = CRYPTO_CMAKE_REPLACE[0].sub(CRYPTO_CMAKE_REPLACE[1], content, count=1)
        return content

    def _transform_crypto_kconfig(self, content: str) -> str:
        """Transform crypto/Kconfig - add PSA config entries."""
        content = apply_kconfig_rules(content)
        if 'BT_CRYPTO_PSA' in content:
            return content  # already has extra entries
        content = content.rstrip() + '\n' + CRYPTO_KCONFIG_EXTRA
        return content

    def _transform_controller_kconfig(self, content: str) -> str:
        """Controller/Kconfig is custom NXP - do not sync from Zephyr."""
        # This function should never be called due to skip list, but just in case
        return content

    def _transform_host_kconfig(self, content: str) -> str:
        """Transform host/Kconfig: standard rules, then fix BT_SETTINGS block.

        BT_SETTINGS must use 'select PORT_SETTINGS' (not 'depends on PORT_SETTINGS').
        This post-processing step, applied after standard rules, fixes the block:
          - If 'depends on PORT_SETTINGS' is present → change to 'select PORT_SETTINGS'
          - If neither is present → inject 'select PORT_SETTINGS' after the bool line
          - If 'select PORT_SETTINGS' already present → no-op
        """
        # Step 1: apply standard Kconfig transformations
        content = apply_kconfig_rules(content)

        # Step 2: fix BT_SETTINGS block specifically (as a final post-process)
        def fix_bt_settings_block(m):
            block = m.group(0)
            # Case 1: has 'depends on PORT_SETTINGS' → promote to 'select PORT_SETTINGS'
            if '\tdepends on PORT_SETTINGS\n' in block:
                return block.replace('\tdepends on PORT_SETTINGS\n',
                                     '\tselect PORT_SETTINGS\n')
            # Case 2: already correct
            if '\tselect PORT_SETTINGS' in block:
                return block
            # Case 3: neither present → inject after the bool line
            return re.sub(
                r'(\tbool "Store Bluetooth state[^\n]*\n)',
                r'\1\tselect PORT_SETTINGS\n',
                block,
                count=1
            )

        content = re.sub(
            r'(config BT_SETTINGS\n(?:[ \t][^\n]*\n|\n)*)',
            fix_bt_settings_block,
            content
        )
        return content

    # ------------------------------------------------------------------
    # Main sync routines per subsystem
    # ------------------------------------------------------------------

    def sync_bluetooth_source(self, subsystem_filter: Optional[str] = None):
        """Sync all files under subsys/bluetooth -> source/bluetooth."""
        zephyr_bt = self.zephyr_bt
        ef_bt = self.ef_bt

        for root, dirs, files in os.walk(zephyr_bt):
            # Sort dirs for deterministic traversal
            dirs.sort()
            rel_dir = os.path.relpath(root, zephyr_bt)
            ef_dir_abs = os.path.join(ef_bt, rel_dir)
            ef_dir_rel = self._ef_rel(ef_dir_abs).replace('\\', '/')

            # Skip entire directories
            if any(ef_dir_rel == sd or ef_dir_rel.startswith(sd + '/')
                   for sd in SKIP_SYNC_DIRS):
                self.log(f"SKIP DIR: {ef_dir_rel}")
                dirs.clear()  # Don't recurse
                continue

            # Apply subsystem filter
            if subsystem_filter:
                top_level = rel_dir.split(os.sep)[0] if rel_dir != '.' else '.'
                if top_level != '.' and top_level != subsystem_filter:
                    continue

            for fname in sorted(files):
                zf = os.path.join(root, fname)
                ef = os.path.join(ef_dir_abs, fname)
                rel_path = os.path.join(rel_dir, fname)
                ef_rel_path = self._ef_rel(ef).replace('\\', '/')

                if fname == 'CMakeLists.txt':
                    self._sync_cmake_file(zf, ef, rel_dir, fname)
                elif fname.startswith('Kconfig'):
                    self._sync_kconfig_file(zf, ef, rel_dir, fname)
                elif fname == 'bt_shell_private.c':
                    self._sync_transformed_source(zf, ef, _transform_bt_shell_private_c,
                                                  'TRANSFORM-SOURCE')
                elif fname == 'bt_shell_private.h':
                    self._sync_transformed_source(zf, ef, _transform_bt_shell_private_h,
                                                  'TRANSFORM-SOURCE')
                else:
                    # Regular source file - copy verbatim
                    self.sync_source_file(zf, ef)

    def _sync_cmake_file(self, zf: str, ef: str, rel_dir: str, fname: str):
        """Dispatch to the right cmake transform based on location."""
        # Normalize rel_dir for comparison
        rd = rel_dir.replace('\\', '/').lstrip('.').lstrip('/')

        if rd == '' or rd == '.':
            # bluetooth/CMakeLists.txt - use template
            self.sync_cmake(zf, ef, self._transform_bt_root_cmake)
        elif rd == 'services':
            self.sync_cmake(zf, ef, self._transform_services_cmake)
        elif rd == 'crypto':
            self.sync_cmake(zf, ef, self._transform_crypto_cmake)
        elif rd == 'controller':
            # Controller CMakeLists.txt is fully custom - DO NOT SYNC
            self.log(f"SKIP (custom controller cmake): {self._ef_rel(ef)}")
            self.stats['skipped'] += 1
        else:
            # All other CMakeLists.txt files are identical to Zephyr - copy verbatim
            self.sync_cmake(zf, ef, transform_fn=None)

    def _sync_kconfig_file(self, zf: str, ef: str, rel_dir: str, fname: str):
        """Dispatch to the right Kconfig transform based on location."""
        rd = rel_dir.replace('\\', '/').lstrip('.').lstrip('/')

        if rd == '' or rd == '.':
            if fname == 'Kconfig':
                self.sync_kconfig(zf, ef,
                    extra_rules=[(BT_ROOT_KCONFIG_EXTRA_RSOURCE[0],
                                  BT_ROOT_KCONFIG_EXTRA_RSOURCE[1])])
                return
        elif rd == 'services':
            if fname == 'Kconfig':
                if not self._is_skipped(ef):
                    try:
                        content = read_text(zf)
                        transformed = self._transform_services_kconfig(content)
                        changed = write_text(ef, transformed, self.dry_run)
                        if changed:
                            self.log_change("TRANSFORM-KCONFIG", self._ef_rel(ef))
                            self.stats['transformed'] += 1
                        else:
                            self.stats['unchanged'] += 1
                    except Exception as e:
                        logging.error(f"Error: {e}")
                        self.stats['errors'] += 1
                return
        elif rd == 'crypto':
            if fname == 'Kconfig':
                if not self._is_skipped(ef):
                    try:
                        content = read_text(zf)
                        transformed = self._transform_crypto_kconfig(content)
                        changed = write_text(ef, transformed, self.dry_run)
                        if changed:
                            self.log_change("TRANSFORM-KCONFIG", self._ef_rel(ef))
                            self.stats['transformed'] += 1
                        else:
                            self.stats['unchanged'] += 1
                    except Exception as e:
                        logging.error(f"Error: {e}")
                        self.stats['errors'] += 1
                return
        elif rd == 'host':
            if fname == 'Kconfig':
                if not self._is_skipped(ef):
                    try:
                        content = read_text(zf)
                        transformed = self._transform_host_kconfig(content)
                        changed = write_text(ef, transformed, self.dry_run)
                        if changed:
                            self.log_change("TRANSFORM-KCONFIG", self._ef_rel(ef))
                            self.stats['transformed'] += 1
                        else:
                            self.stats['unchanged'] += 1
                    except Exception as e:
                        logging.error(f"Error: {e}")
                        self.stats['errors'] += 1
                return
        elif rd == 'controller':
            # Controller Kconfig is fully custom - DO NOT SYNC
            self.log(f"SKIP (custom controller kconfig): {self._ef_rel(ef)}")
            self.stats['skipped'] += 1
            return
        elif rd == 'mesh':
            # Mesh Kconfig is custom stub - DO NOT SYNC
            if fname == 'Kconfig':
                self.log(f"SKIP (custom mesh kconfig): {self._ef_rel(ef)}")
                self.stats['skipped'] += 1
                return

        # Default: apply standard Kconfig transformations
        self.sync_kconfig(zf, ef)

    def sync_bluetooth_includes(self, subsystem_filter: Optional[str] = None):
        """Sync all files under include/zephyr/bluetooth -> include/zephyr/bluetooth."""
        zephyr_inc = self.zephyr_inc
        ef_inc = self.ef_inc

        for root, dirs, files in os.walk(zephyr_inc):
            dirs.sort()
            rel_dir = os.path.relpath(root, zephyr_inc)

            for fname in sorted(files):
                # Skip CMakeLists.txt files in includes (those are EF-specific extras)
                if fname == 'CMakeLists.txt':
                    continue

                zf = os.path.join(root, fname)
                ef = os.path.join(ef_inc, rel_dir, fname)

                # Include header files - copy verbatim
                # (structural changes in goep.h etc. are handled by the .c file changes,
                #  the EF version of goep.h is already the correct refactored API)
                # Do NOT overwrite EF-specific headers (those in EXTRA_IN_EF list)
                if not os.path.exists(ef):
                    # File exists in Zephyr but not EF - skip (EF may not need it)
                    self.log(f"SKIP (not in EF): {normalize_path(os.path.relpath(ef, self.ef_dir))}")
                    self.stats['skipped'] += 1
                    continue

                self.sync_source_file(zf, ef)

    def sync_lib_dirs(self):
        """Sync lib/ directories that mirror Zephyr's lib/ structure verbatim.

        Covers:
          lib/net_buf/  <- Zephyr lib/net_buf/
          lib/utils/    <- Zephyr lib/utils/
          lib/uuid/     <- Zephyr lib/uuid/

        EF-only lib dirs (lib/liblc3, lib/libsbc, lib/asrc_sw, lib/crc, lib/libc)
        are not touched.
        """
        for z_rel, ef_rel in ZEPHYR_LIB_SYNC_DIRS:
            z_dir = os.path.join(self.zephyr_dir, z_rel.replace('/', os.sep))
            ef_dir = os.path.join(self.ef_dir, ef_rel.replace('/', os.sep))

            if not os.path.isdir(z_dir):
                logging.warning(f"Zephyr lib dir not found, skipping: {z_dir}")
                continue
            if not os.path.isdir(ef_dir):
                self.log(f"SKIP (lib dir not in EF): {ef_rel}")
                continue

            for root, dirs, files in os.walk(z_dir):
                dirs.sort()
                rel = os.path.relpath(root, z_dir)
                for fname in sorted(files):
                    if not (fname.endswith('.c') or fname.endswith('.h')):
                        continue
                    zf = os.path.join(root, fname)
                    ef = os.path.join(ef_dir, rel, fname)
                    if not os.path.exists(ef):
                        # File in Zephyr not in EF - skip (EF may not need it)
                        self.log(f"SKIP (not in EF lib): {normalize_path(os.path.join(ef_rel, rel, fname))}")
                        self.stats['skipped'] += 1
                        continue
                    self.sync_source_file(zf, ef)

    @staticmethod
    def _transform_sys_util_h(content: str) -> str:
        """Transform include/zephyr/sys/util.h.

        EF adds '#undef ARRAY_SIZE' before the IS_ARRAY-based '#define ARRAY_SIZE'
        (the C, non-C++ version) to avoid redefinition conflicts with the MCUXpresso
        SDK which also defines ARRAY_SIZE in its own headers.

        The file has two ARRAY_SIZE definitions:
          Line ~96:  #define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
                     (inside #else /* __cplusplus */ block)
          Line ~118: #define ARRAY_SIZE(array) ((size_t)(IS_ARRAY(array) + ...))
                     (inside #endif /* __cplusplus */ block, C-only version)

        The #undef is inserted before the IS_ARRAY variant (the C-only one).
        """
        # Check if already transformed
        if '#undef ARRAY_SIZE' in content:
            return content
        # Insert '#undef ARRAY_SIZE\n' before the IS_ARRAY-based definition only
        return re.sub(
            r'(#define ARRAY_SIZE\(array\) \(\(size_t\)\(IS_ARRAY)',
            r'#undef ARRAY_SIZE\n\1',
            content,
            count=1
        )

    def sync_include_zephyr(self):
        """Sync include/zephyr/ non-bluetooth headers that come verbatim from Zephyr.

        Rules:
        - include/zephyr/bluetooth/ is handled by sync_bluetooth_includes()
        - Porting shim dirs (arch/, debug/, drivers/, dt-bindings/, kernel/,
          logging/, shell/, sys/, syscalls/) are SKIPPED (EF custom)
        - Top-level porting shim files (kernel.h, device.h, etc.) are SKIPPED
        - include/zephyr/sys/util.h needs a specific transform (add #undef ARRAY_SIZE)
        - Everything else that already exists in EF is synced verbatim.
        """
        z_inc = os.path.join(self.zephyr_dir, 'include', 'zephyr')
        ef_inc = os.path.join(self.ef_dir, 'include', 'zephyr')

        if not os.path.isdir(z_inc) or not os.path.isdir(ef_inc):
            return

        for root, dirs, files in os.walk(z_inc):
            dirs.sort()
            rel_dir = os.path.relpath(root, z_inc)
            ef_dir_abs = os.path.join(ef_inc, rel_dir)
            ef_dir_rel = normalize_path(os.path.join('include', 'zephyr', rel_dir)).rstrip('/.')

            # Skip bluetooth subdir (handled separately)
            if rel_dir.replace('\\', '/') == 'bluetooth' or \
               rel_dir.replace('\\', '/').startswith('bluetooth/'):
                dirs.clear()
                continue

            # Skip porting shim directories
            skip_this_dir = False
            for skip_d in SKIP_INCLUDE_ZEPHYR_DIRS:
                # skip_d is like "include/zephyr/arch"
                skip_sub = skip_d[len('include/zephyr/'):] if skip_d.startswith('include/zephyr/') else skip_d
                rd_norm = rel_dir.replace('\\', '/')
                if rd_norm == skip_sub or rd_norm.startswith(skip_sub + '/'):
                    skip_this_dir = True
                    break
            if skip_this_dir:
                self.log(f"SKIP DIR (porting shim): {ef_dir_rel}")
                dirs.clear()
                continue

            for fname in sorted(files):
                if not (fname.endswith('.h') or fname.endswith('.c')):
                    continue
                if fname == 'CMakeLists.txt':
                    continue

                zf = os.path.join(root, fname)
                ef = os.path.join(ef_dir_abs, fname)
                ef_file_rel = normalize_path(os.path.join('include', 'zephyr',
                                                           rel_dir, fname)).replace('/./','/')

                # Skip porting shim top-level files
                if ef_file_rel in SKIP_INCLUDE_ZEPHYR_FILES:
                    self.log(f"SKIP (porting shim): {ef_file_rel}")
                    self.stats['skipped'] += 1
                    continue

                # Only sync if file already exists in EF (don't add new files
                # from Zephyr that weren't deliberately ported)
                if not os.path.exists(ef):
                    self.log(f"SKIP (not in EF include): {ef_file_rel}")
                    self.stats['skipped'] += 1
                    continue

                # Special transform for sys/util.h: add #undef ARRAY_SIZE
                if ef_file_rel == 'include/zephyr/sys/util.h':
                    self._sync_transformed_source(
                        zf, ef, self._transform_sys_util_h, 'TRANSFORM-INCLUDE')
                    continue

                self.sync_source_file(zf, ef)

    def sync_special_remaps(self):
        """Sync files that have different paths in Zephyr vs Edgefast Open."""
        for ef_rel, z_rel in SPECIAL_PATH_REMAPS:
            zf = os.path.join(self.zephyr_dir, z_rel.replace('/', os.sep))
            ef = os.path.join(self.ef_dir, ef_rel.replace('/', os.sep))

            if not os.path.exists(zf):
                logging.warning(f"Zephyr source not found for remap: {z_rel}")
                continue
            if not os.path.exists(ef):
                self.log(f"SKIP (EF file missing for remap): {ef_rel}")
                self.stats['skipped'] += 1
                continue

            self.sync_source_file(zf, ef)

    def run(self, subsystem_filter: Optional[str] = None):
        """Run the full sync."""
        logging.info("=" * 70)
        logging.info("Zephyr Bluetooth → Edgefast Open Sync")
        logging.info("=" * 70)
        logging.info(f"Zephyr:      {self.zephyr_dir}")
        logging.info(f"EdgefastOpen: {self.ef_dir}")
        if self.dry_run:
            logging.info("Mode:        DRY-RUN (no files will be changed)")
        if subsystem_filter:
            logging.info(f"Filter:      {subsystem_filter}")
        logging.info("")

        if subsystem_filter is None or subsystem_filter == 'host':
            # Sync special remapped files (e.g. services/bas.c)
            logging.info("--- Syncing special path-remapped files ---")
            self.sync_special_remaps()

        logging.info("--- Syncing bluetooth source files ---")
        self.sync_bluetooth_source(subsystem_filter)

        logging.info("--- Syncing bluetooth include files ---")
        self.sync_bluetooth_includes(subsystem_filter)

        if subsystem_filter is None or subsystem_filter == 'lib':
            logging.info("--- Syncing lib/ directories (net_buf, utils, uuid) ---")
            self.sync_lib_dirs()

            logging.info("--- Syncing include/zephyr/ non-bluetooth headers ---")
            self.sync_include_zephyr()

        logging.info("")
        logging.info("=" * 70)
        logging.info("Summary:")
        logging.info(f"  Copied (verbatim):   {self.stats['copied']}")
        logging.info(f"  Transformed:         {self.stats['transformed']}")
        logging.info(f"  Skipped (custom/EF): {self.stats['skipped']}")
        logging.info(f"  Unchanged:           {self.stats['unchanged']}")
        logging.info(f"  Errors:              {self.stats['errors']}")
        logging.info("=" * 70)
        if self.stats['errors'] > 0:
            logging.warning("Sync completed with errors. Check log above.")
            return 1
        return 0


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def auto_detect_ef_dir() -> str:
    """Auto-detect the Edgefast Open root from this script's location."""
    # This script lives at: <ef_root>/sync_zephyr_bt.py
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # Check if this looks like an edgefast_open root
    if os.path.exists(os.path.join(script_dir, 'source', 'bluetooth')):
        return script_dir
    raise RuntimeError(
        f"Cannot auto-detect Edgefast Open root from {script_dir}. "
        "Please pass --ef-dir explicitly."
    )


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        '--zephyr-dir',
        default=DEFAULT_ZEPHYR_DIR,
        help=f'Path to Zephyr root directory (default: {DEFAULT_ZEPHYR_DIR})'
    )
    parser.add_argument(
        '--ef-dir',
        default=None,
        help='Path to Edgefast Open root directory (default: auto-detected from script location)'
    )
    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='Show what would be changed without making any changes'
    )
    parser.add_argument(
        '--verbose',
        action='store_true',
        help='Show detailed output including unchanged files'
    )
    parser.add_argument(
        '--subsystem',
        choices=['host', 'audio', 'services', 'common', 'crypto', 'mesh', 'controller', 'lib'],
        default=None,
        help='Only sync a specific subsystem'
    )

    args = parser.parse_args()

    # Configure logging
    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=log_level,
        format='%(message)s',
        handlers=[logging.StreamHandler(sys.stdout)]
    )

    # Resolve paths
    zephyr_dir = os.path.normpath(args.zephyr_dir)
    if args.ef_dir:
        ef_dir = os.path.normpath(args.ef_dir)
    else:
        try:
            ef_dir = auto_detect_ef_dir()
        except RuntimeError as e:
            logging.error(str(e))
            sys.exit(1)

    # Validate paths
    if not os.path.isdir(zephyr_dir):
        logging.error(f"Zephyr directory not found: {zephyr_dir}")
        sys.exit(1)

    zephyr_bt_subsys = os.path.join(zephyr_dir, ZEPHYR_BT_SUBSYS.replace('/', os.sep))
    if not os.path.isdir(zephyr_bt_subsys):
        logging.error(
            f"Zephyr Bluetooth subsys not found: {zephyr_bt_subsys}\n"
            f"Expected subsys/bluetooth under {zephyr_dir}"
        )
        sys.exit(1)

    if not os.path.isdir(ef_dir):
        logging.error(f"Edgefast Open directory not found: {ef_dir}")
        sys.exit(1)

    ef_bt_source = os.path.join(ef_dir, EF_BT_SOURCE.replace('/', os.sep))
    if not os.path.isdir(ef_bt_source):
        logging.error(
            f"Edgefast Open Bluetooth source not found: {ef_bt_source}\n"
            f"Expected source/bluetooth under {ef_dir}"
        )
        sys.exit(1)

    # Run sync
    engine = SyncEngine(
        zephyr_dir=zephyr_dir,
        ef_dir=ef_dir,
        dry_run=args.dry_run,
        verbose=args.verbose
    )

    exit_code = engine.run(subsystem_filter=args.subsystem)
    sys.exit(exit_code)


if __name__ == '__main__':
    main()
