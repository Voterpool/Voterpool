#!/usr/bin/env python3
"""Страж изоляции швов scaling (tasks 2.6, design Risks).

Файлы вне src/scaling НЕ ДОЛЖНЫ обращаться к внутренностям Directory/Identity
плоскостей напрямую: OrgNameRegistry и auth-пути AgentRepository доступны
только имплементациям портов. ConsensusEngine использует OrgNameRegistry как
внутренний механизм консенсус-плоскости — whitelisted.

Запуск добавлен в CTest (target seam_isolation).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

# Прямые вызовы, запрещённые вне src/scaling (и whitelist'ов ниже).
FORBIDDEN = [
    r"\borgNames\s*->",                    # реестр имён каталога
    r"\bagents\s*->\s*resolveAgentByTokenHash",  # auth-путь AgentRepository
]

# Разрешённые места (относительно корня репозитория).
WHITELIST = [
    "src/scaling/",
    "src/consensus/",          # orgNames как внутренний механизм консенсуса
    "src/storage/",            # владельцы данных
    "include/",
    "tests/",                  # тесты вправе собирать порты любым способом
]

# Composition root: собирает конкретные имплементации и их зависимости.
# Единственное разрешённое исключение вне src/scaling.
WHITELIST_FILES = {"src/server/AppContext.cpp"}


def is_whitelisted(rel: str) -> bool:
    return any(rel.startswith(w) for w in WHITELIST)


def main() -> int:
    violations = []
    for pattern in FORBIDDEN:
        rx = re.compile(pattern)
        for cpp in ROOT.glob("src/**/*.cpp"):
            rel = cpp.relative_to(ROOT).as_posix()
            if is_whitelisted(rel) or rel in WHITELIST_FILES:
                continue
            text = cpp.read_text(encoding="utf-8")
            for lineno, line in enumerate(text.splitlines(), 1):
                if rx.search(line) and not line.lstrip().startswith("//"):
                    violations.append(f"{rel}:{lineno}: {line.strip()[:120]}")

    if violations:
        print("Seam isolation violated:")
        for v in violations:
            print("  " + v)
        print("\nИспользуйте порты IDirectory/IIdentity (include/scaling/).")
        return 1
    print("Seam isolation OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
