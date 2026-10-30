"""리소스 검사 (Step 6.19.8 / 6.20.2)

M2Draw.rc (CP949) 와 Resource.h 를 읽어 다음을 확인한다.
  1. 같은 팝업 메뉴 안에서 니모닉(&X)이 중복되지 않는다 (중복이면 메뉴 키 이동이 깨진다)
  2. 메뉴 항목 ID 가 문자열 테이블(상태 표시줄 설명)을 가진다
  3. Resource.h 의 같은 값 ID 중복이 없다 (같은 번호를 두 이름이 쓰면 명령이 섞인다)

사용: python tools/check_resources.py   (종료 코드 0 = 통과)
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
RC = os.path.join(ROOT, 'M2Draw.rc')
RESH = os.path.join(ROOT, 'Resource.h')

# 이 목록은 시스템/표준 ID 라서 문자열 테이블 검사에서 뺀다
IGNORE_STRING_CHECK = {'ID_FILE_MRU_FILE1', 'ID_APP_EXIT'}


def read_text(path):
    raw = open(path, 'rb').read()
    try:
        return raw.decode('utf-8-sig')
    except UnicodeDecodeError:
        return raw.decode('cp949')


def menu_mnemonic_problems(rc_text):
    problems = []
    stack = []
    menu_name = None
    in_menu = False
    lines = rc_text.split('\n')
    for line in lines:
        s = line.strip()
        if re.match(r'^(IDR_\w+)\s+MENU\b', s):
            menu_name = s.split()[0]
            in_menu = True
            continue
        if not in_menu:
            continue
        if s == 'BEGIN':
            continue
        if s == 'END':
            if stack:
                popup = stack.pop()
                for key, labels in popup['keys'].items():
                    if len(labels) > 1:
                        problems.append('[%s] 팝업 "%s" 니모닉 &%s 중복: %s' % (popup['menu'], popup['label'], key, ' / '.join(labels)))
            else:
                in_menu = False
            continue
        m = re.match(r'^POPUP\s+"([^"]*)"', s)
        if m:
            stack.append({'label': m.group(1), 'keys': defaultdict(list), 'menu': menu_name})
            continue
        m = re.match(r'^MENUITEM\s+"([^"]*)"', s)
        if m and stack:
            mm = re.search(r'&(.)', m.group(1))
            if mm:
                stack[-1]['keys'][mm.group(1).upper()].append(m.group(1))
    return problems


def menu_item_ids(rc_text):
    ids = set()
    in_menu = False
    for line in rc_text.split('\n'):
        s = line.strip()
        if re.match(r'^(IDR_\w+)\s+MENU\b', s):
            in_menu = True
            continue
        m = re.match(r'^MENUITEM\s+"[^"]*"\s*,\s*(ID_\w+)', s)
        if m:
            ids.add(m.group(1))
    return ids


def string_table_ids(rc_text):
    ids = set()
    for line in rc_text.split('\n'):
        m = re.match(r'^\s+(ID_\w+)\s+"', line)
        if m:
            ids.add(m.group(1))
    return ids


def duplicate_defines(resh_text):
    by_value = defaultdict(list)
    for m in re.finditer(r'^#define\s+(ID_\w+)\s+(\d+)\s*$', resh_text, re.M):
        by_value[int(m.group(2))].append(m.group(1))
    return ['값 %d 를 여러 이름이 씀: %s' % (v, ', '.join(names)) for v, names in by_value.items() if len(names) > 1]


def main():
    rc = read_text(RC)
    resh = read_text(RESH)
    problems = []
    problems += menu_mnemonic_problems(rc)

    strings = string_table_ids(rc)
    missing = sorted(i for i in menu_item_ids(rc) if i not in strings and i not in IGNORE_STRING_CHECK)
    problems += ['메뉴 항목 %s 에 문자열(상태 표시줄 설명)이 없음' % i for i in missing]
    problems += duplicate_defines(resh)

    if problems:
        print('리소스 검사 실패 (%d건)' % len(problems))
        for p in problems:
            print('  - ' + p)
        return 1
    print('리소스 검사 통과: 메뉴 니모닉 중복 없음, 메뉴 항목 문자열 모두 있음, ID 값 중복 없음')
    return 0


if __name__ == '__main__':
    sys.exit(main())
