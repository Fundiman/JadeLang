#!/usr/bin/env python3
"""
jpkg — Jade Package Manager
public: 0.1.0-alpha
"""

import sys
import os
from commands import (
    cmd_init, cmd_add, cmd_remove, cmd_install,
    cmd_list, cmd_build, cmd_run, cmd_publish,
    cmd_search, cmd_info, cmd_update
)

VERSION         = "0.1.0-alpha"
REGISTRY_REPO   = "https://github.com/Fundiman/jade-pkgs"
REGISTRY_RAW    = "https://raw.githubusercontent.com/Fundiman/jade-pkgs/main"

COMMANDS = {
    "init":    (cmd_init,    "create a new jade.toml in the current directory"),
    "add":     (cmd_add,     "add a dependency"),
    "remove":  (cmd_remove,  "remove a dependency"),
    "install": (cmd_install, "install all dependencies from jade.toml"),
    "update":  (cmd_update,  "update dependencies to latest compatible versions"),
    "list":    (cmd_list,    "list installed packages"),
    "build":   (cmd_build,   "compile .jsc -> .joc via jscc"),
    "run":     (cmd_run,     "build and run with jvk"),
    "publish": (cmd_publish, "publish package to jade-pkgs registry via PR"),
    "search":  (cmd_search,  "search the registry"),
    "info":    (cmd_info,    "show package details"),
}

def print_help():
    print(f"jpkg {VERSION} — Jade Package Manager")
    print(f"registry: {REGISTRY_REPO}\n")
    print("usage: jpkg <command> [args]\n")
    print("commands:")
    for name, (_, desc) in COMMANDS.items():
        print(f"  {name:<12} {desc}")
    print()
    print("examples:")
    print("  jpkg init")
    print("  jpkg add jade.stdlib.http")
    print("  jpkg build")
    print("  jpkg run")
    print("  jpkg publish")

def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help", "help"):
        print_help()
        return 0

    if sys.argv[1] in ("-v", "--version", "version"):
        print(f"jpkg {VERSION}")
        return 0

    cmd = sys.argv[1]
    args = sys.argv[2:]

    if cmd not in COMMANDS:
        print(f"jpkg: unknown command '{cmd}'")
        print(f"run 'jpkg --help' for usage")
        return 1

    fn, _ = COMMANDS[cmd]
    return fn(args) or 0

if __name__ == "__main__":
    sys.exit(main())
