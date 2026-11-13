#!/usr/bin/env python3
"""Quick checks that need no compiler (runs in well under a minute).

1. No merge conflict markers in tracked files.
2. Every source listed in nice_target_sources(... ${src_loc} ...) of
   Telegram/CMakeLists.txt and Telegram/cmake/luckgram*.cmake exists, and
   every Luckgram source (path contains "luckgram") is listed.
3. No duplicate keys in the language files (*.strings).
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
errors = []


def tracked_files():
    out = subprocess.run(
        ['git', '-C', str(ROOT), 'ls-files', '-z'],
        check=True, capture_output=True).stdout
    return [p for p in out.decode('utf-8', 'replace').split('\0') if p]


STRICT_EXT = ('.cpp', '.h', '.mm', '.c', '.cc', '.hpp', '.cmake', '.txt',
              '.yml', '.yaml', '.py', '.ps1', '.iss', '.strings', '.json')


def check_conflict_markers(files):
    marker = re.compile(rb'^(<{7}|>{7})( |$)|^={7}$', re.M)
    for name in files:
        path = ROOT / name
        if not path.is_file() or path.is_symlink():
            continue
        try:
            data = path.read_bytes()
        except OSError:
            continue
        if b'\0' in data[:8000]:
            continue  # binary
        for m in marker.finditer(data):
            # '=======' alone is a heading underline in some docs: in other
            # files count it only next to a real marker.
            if m.group(0).startswith(b'=') and not name.endswith(STRICT_EXT):
                if not re.search(rb'^(<{7}|>{7}) ', data, re.M):
                    continue
            line = data.count(b'\n', 0, m.start()) + 1
            errors.append(f'{name}:{line}: merge conflict marker')


def strip_cmake_comments(text):
    # Keep line numbers: drop '#' comments line by line.
    return '\n'.join(line.split('#', 1)[0] for line in text.split('\n'))


SOURCE_EXT = ('.cpp', '.h', '.mm', '.c', '.cc', '.hpp')


def check_cmake_sources(files):
    src = ROOT / 'Telegram' / 'SourceFiles'
    cmakes = [ROOT / 'Telegram' / 'CMakeLists.txt']
    cmakes += sorted((ROOT / 'Telegram' / 'cmake').glob('luckgram*.cmake'))
    block = re.compile(
        r'nice_target_sources\(\s*\w+\s+\$\{src_loc\}(.*?)\)', re.S)
    mentioned = set()
    for cmake in cmakes:
        text = strip_cmake_comments(cmake.read_text(encoding='utf-8'))
        rel = cmake.relative_to(ROOT)
        mentioned.update(re.findall(r'[\w./-]+', text))
        for m in block.finditer(text):
            offset = m.start(1)
            for raw in m.group(1).split('\n'):
                item = raw.strip()
                line = text.count('\n', 0, offset) + 1
                offset += len(raw) + 1
                if not item or item in ('PRIVATE', 'PUBLIC', 'INTERFACE'):
                    continue
                if '$' in item or '"' in item:
                    continue  # generated or variable paths
                if not (src / item).exists():
                    errors.append(
                        f'{rel}:{line}: listed source missing: {item}')

    # The other direction: every Luckgram source must be listed somewhere,
    # else the merged build fails to link (nobody compiles before merging).
    prefix = 'Telegram/SourceFiles/'
    for name in files:
        if not name.startswith(prefix) or not name.endswith(SOURCE_EXT):
            continue
        rel = name[len(prefix):]
        if 'luckgram' not in rel.lower():
            continue
        if rel not in mentioned and not any(
                token.endswith('/' + rel) for token in mentioned):
            errors.append(f'{name}:1: source not listed in any CMake file')


def check_lang_keys(files):
    key = re.compile(r'^\s*"((?:[^"\\]|\\.)+)"\s*=', re.M)
    for name in files:
        if not name.endswith('.strings'):
            continue
        path = ROOT / name
        try:
            text = path.read_text(encoding='utf-8-sig')
        except (OSError, UnicodeDecodeError):
            try:
                text = path.read_text(encoding='utf-16')
            except (OSError, UnicodeError):
                continue
        seen = {}
        for m in key.finditer(text):
            line = text.count('\n', 0, m.start()) + 1
            k = m.group(1)
            if k in seen:
                errors.append(
                    f'{name}:{line}: duplicate key "{k}" '
                    f'(first at line {seen[k]})')
            else:
                seen[k] = line


def main():
    files = tracked_files()
    check_conflict_markers(files)
    check_cmake_sources(files)
    check_lang_keys(files)
    for e in errors:
        print(f'::error::{e}' if '--github' in sys.argv else e)
    print(f'luckgram_lint: {len(errors)} problem(s)')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
