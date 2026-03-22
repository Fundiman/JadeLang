"""
jade.toml parser and package config
"""

import os
import re
import json
from dataclasses import dataclass, field
from typing import Optional

try:
    import tomllib          # python 3.11+
except ImportError:
    try:
        import tomli as tomllib  # pip install tomli
    except ImportError:
        tomllib = None


# ── version resolution ────────────────────────────────────────────────────────

def parse_version(v: str) -> tuple:
    """parse '1.2.3' or '0.1.0-alpha' into comparable tuple"""
    v = v.lstrip("^~=v")
    # split off pre-release tag
    pre = ""
    if "-" in v:
        v, pre = v.split("-", 1)
    parts = [int(x) for x in v.split(".")]
    while len(parts) < 3:
        parts.append(0)
    return tuple(parts), pre

def version_satisfies(available: str, constraint: str) -> bool:
    """check if 'available' satisfies 'constraint'
    supports: ^1.2.3 (compatible), ~1.2.3 (patch), =1.2.3 (exact), 1.2.3 (exact)
    """
    if constraint == "*" or constraint == "latest":
        return True

    op = ""
    ver = constraint
    for prefix in ("^", "~", ">=", "<=", "!=", "=", ">", "<"):
        if constraint.startswith(prefix):
            op = prefix
            ver = constraint[len(prefix):]
            break

    avail, _ = parse_version(available)
    req,   _ = parse_version(ver)

    if op == "^":
        # compatible — same major, avail >= req
        return avail[0] == req[0] and avail >= req
    elif op == "~":
        # patch-compatible — same major.minor, avail >= req
        return avail[0] == req[0] and avail[1] == req[1] and avail >= req
    elif op == ">=":
        return avail >= req
    elif op == "<=":
        return avail <= req
    elif op == ">":
        return avail > req
    elif op == "<":
        return avail < req
    elif op == "!=":
        return avail != req
    else:
        return avail == req


# ── jade.toml ─────────────────────────────────────────────────────────────────

@dataclass
class BuildConfig:
    entry:   str = "src/main.jsc"
    output:  str = "out/main.joc"
    targets: list = field(default_factory=list)
    no_bytecode: bool = False

@dataclass
class PackageConfig:
    name:         str = ""
    version:      str = "0.1.0"
    license:      str = "LGPL-3.0"
    authors:      list = field(default_factory=list)
    description:  str = ""
    homepage:     str = ""
    dependencies: dict = field(default_factory=dict)  # name -> version constraint
    build:        BuildConfig = field(default_factory=BuildConfig)
    path:         str = ""  # path to the jade.toml file


def find_toml(start: str = ".") -> Optional[str]:
    """walk up from start looking for jade.toml"""
    current = os.path.abspath(start)
    while True:
        candidate = os.path.join(current, "jade.toml")
        if os.path.exists(candidate):
            return candidate
        parent = os.path.dirname(current)
        if parent == current:
            return None
        current = parent


def load_toml(path: str) -> PackageConfig:
    """load and parse a jade.toml file"""
    with open(path, "rb") as f:
        raw = f.read()

    if tomllib:
        data = tomllib.loads(raw.decode())
    else:
        # fallback: simple regex-based parser for basic cases
        data = _simple_toml_parse(raw.decode())

    cfg = PackageConfig()
    cfg.path = path

    pkg = data.get("package", {})
    cfg.name        = pkg.get("name",        "")
    cfg.version     = pkg.get("version",     "0.1.0")
    cfg.license     = pkg.get("license",     "LGPL-3.0")
    cfg.authors     = pkg.get("authors",     [])
    cfg.description = pkg.get("description", "")
    cfg.homepage    = pkg.get("homepage",    "")

    cfg.dependencies = data.get("dependencies", {})

    b = data.get("build", {})
    cfg.build = BuildConfig(
        entry       = b.get("entry",        "src/main.jsc"),
        output      = b.get("output",       "out/main.joc"),
        targets     = b.get("targets",      []),
        no_bytecode = b.get("no_bytecode",  False),
    )

    return cfg


def save_toml(cfg: PackageConfig, path: str):
    """write a PackageConfig back to jade.toml"""
    lines = [
        "[package]",
        f'name        = "{cfg.name}"',
        f'version     = "{cfg.version}"',
        f'license     = "{cfg.license}"',
        f'description = "{cfg.description}"',
    ]
    if cfg.authors:
        authors_str = ", ".join(f'"{a}"' for a in cfg.authors)
        lines.append(f"authors = [{authors_str}]")

    lines.append("")
    lines.append("[dependencies]")
    for name, ver in cfg.dependencies.items():
        lines.append(f'"{name}" = "{ver}"')

    lines.append("")
    lines.append("[build]")
    lines.append(f'entry  = "{cfg.build.entry}"')
    lines.append(f'output = "{cfg.build.output}"')
    if cfg.build.targets:
        tgts = ", ".join(f'"{t}"' for t in cfg.build.targets)
        lines.append(f"targets = [{tgts}]")
    if cfg.build.no_bytecode:
        lines.append("no_bytecode = true")

    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def _simple_toml_parse(text: str) -> dict:
    """minimal TOML parser for when tomllib is unavailable"""
    result = {}
    current_section = result

    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        # section header
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip()
            parts = section.split(".")
            d = result
            for p in parts:
                d = d.setdefault(p, {})
            current_section = d
            continue
        # key = value
        if "=" in line:
            k, _, v = line.partition("=")
            k = k.strip()
            v = v.strip()
            # string
            if v.startswith('"') and v.endswith('"'):
                current_section[k] = v[1:-1]
            # array
            elif v.startswith("[") and v.endswith("]"):
                items = [x.strip().strip('"') for x in v[1:-1].split(",") if x.strip()]
                current_section[k] = items
            # bool
            elif v == "true":
                current_section[k] = True
            elif v == "false":
                current_section[k] = False
            # number
            elif re.match(r'^-?\d+(\.\d+)?$', v):
                current_section[k] = float(v) if "." in v else int(v)
            else:
                current_section[k] = v

    return result
