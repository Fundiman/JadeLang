"""
commands.py — all jpkg subcommands
"""

import os
import sys
import json
import shutil
import subprocess
import tempfile
from config import PackageConfig, BuildConfig, find_toml, load_toml, save_toml
from registry import (
    search_packages, get_package_info, get_available_versions,
    resolve_version, download_package, install_to_modules,
    prepare_publish_branch, REGISTRY_REPO
)

# ── helpers ───────────────────────────────────────────────────────────────────

def _require_toml(args=None) -> PackageConfig:
    path = find_toml()
    if not path:
        print("error: no jade.toml found — run 'jpkg init' first")
        sys.exit(1)
    return load_toml(path)


def _modules_dir(cfg: PackageConfig) -> str:
    return os.path.join(os.path.dirname(cfg.path), "jade_modules")


def _find_tool(name: str) -> str:
    """find jscc or jvk binary"""
    # check PATH first
    found = shutil.which(name)
    if found:
        return found
    # check common install locations
    for d in ["/usr/local/bin", "/usr/bin", os.path.expanduser("~/.jade/bin")]:
        candidate = os.path.join(d, name)
        if os.path.isfile(candidate):
            return candidate
    return name  # hope it's on PATH


# ── init ──────────────────────────────────────────────────────────────────────

def cmd_init(args: list) -> int:
    toml_path = "jade.toml"
    if os.path.exists(toml_path):
        print("jade.toml already exists")
        return 1

    # guess package name from directory
    cwd_name   = os.path.basename(os.getcwd())
    pkg_name   = f"com.yourname.{cwd_name}"

    cfg = PackageConfig(
        name        = pkg_name,
        version     = "0.1.0",
        license     = "LGPL-3.0",
        description = "",
        authors     = [],
        dependencies = {"jade.stdlib": "0.1.0-alpha"},
        build       = BuildConfig(
            entry  = "src/main.jsc",
            output = "out/main.joc",
        ),
        path = toml_path,
    )
    save_toml(cfg, toml_path)

    # scaffold directories
    os.makedirs("src", exist_ok=True)
    os.makedirs("out", exist_ok=True)

    # write a hello world main.jsc if it doesn't exist
    main_path = "src/main.jsc"
    if not os.path.exists(main_path):
        with open(main_path, "w") as f:
            f.write(f'package {pkg_name};\n\nimport jade.stdlib.io;\n\n@entry\ndef main(args []str) i32 {{\n    io.println("hello from Jade!");\n    return 0;\n}}\n')

    print(f"created jade.toml")
    print(f"created src/main.jsc")
    print(f"\npackage name: {pkg_name}")
    print(f"edit jade.toml to set your name, description, and dependencies")
    print(f"then run: jpkg install && jpkg run")
    return 0


# ── add ───────────────────────────────────────────────────────────────────────

def cmd_add(args: list) -> int:
    if not args:
        print("usage: jpkg add <package[@version]>")
        return 1

    cfg = _require_toml()

    for pkg_spec in args:
        if "@" in pkg_spec:
            name, constraint = pkg_spec.split("@", 1)
        else:
            name, constraint = pkg_spec, "latest"

        # resolve to actual version
        print(f"resolving {name}@{constraint}...")
        version = resolve_version(name, constraint)
        if not version:
            print(f"error: package '{name}' not found in registry")
            continue

        cfg.dependencies[name] = f"^{version}" if constraint == "latest" else constraint
        print(f"added {name} = \"{cfg.dependencies[name]}\"")

    save_toml(cfg, cfg.path)
    print(f"\nrun 'jpkg install' to download new packages")
    return 0


# ── remove ────────────────────────────────────────────────────────────────────

def cmd_remove(args: list) -> int:
    if not args:
        print("usage: jpkg remove <package>")
        return 1

    cfg = _require_toml()

    for name in args:
        if name in cfg.dependencies:
            del cfg.dependencies[name]
            # also remove from jade_modules
            modules = _modules_dir(cfg)
            for entry in os.listdir(modules) if os.path.isdir(modules) else []:
                if entry.startswith(name + "@"):
                    path = os.path.join(modules, entry)
                    if os.path.islink(path):
                        os.unlink(path)
                    elif os.path.isdir(path):
                        shutil.rmtree(path)
            print(f"removed {name}")
        else:
            print(f"'{name}' is not in dependencies")

    save_toml(cfg, cfg.path)
    return 0


# ── install ───────────────────────────────────────────────────────────────────

def cmd_install(args: list) -> int:
    cfg = _require_toml()
    modules = _modules_dir(cfg)
    os.makedirs(modules, exist_ok=True)

    if not cfg.dependencies:
        print("no dependencies to install")
        return 0

    print(f"installing {len(cfg.dependencies)} package(s)...")
    failed = []

    for name, constraint in cfg.dependencies.items():
        version = resolve_version(name, constraint)
        if not version:
            print(f"  ✗ {name}: no version satisfying '{constraint}'")
            failed.append(name)
            continue

        print(f"  installing {name}@{version}...")
        if install_to_modules(name, version, modules):
            print(f"  ✓ {name}@{version}")
        else:
            print(f"  ✗ {name}@{version}: download failed")
            failed.append(name)

    if failed:
        print(f"\n{len(failed)} package(s) failed to install")
        return 1

    print(f"\nall packages installed to jade_modules/")
    return 0


# ── update ────────────────────────────────────────────────────────────────────

def cmd_update(args: list) -> int:
    cfg = _require_toml()
    updated = []

    for name, constraint in cfg.dependencies.items():
        versions = get_available_versions(name)
        if not versions:
            continue
        from config import parse_version, version_satisfies
        versions.sort(key=lambda v: parse_version(v)[0], reverse=True)
        latest = versions[0]
        current = resolve_version(name, constraint)
        if latest != current:
            cfg.dependencies[name] = f"^{latest}"
            updated.append(f"{name}: {current} -> {latest}")

    if updated:
        save_toml(cfg, cfg.path)
        print("updated:")
        for u in updated:
            print(f"  {u}")
        print("\nrun 'jpkg install' to download updates")
    else:
        print("all packages up to date")
    return 0


# ── list ──────────────────────────────────────────────────────────────────────

def cmd_list(args: list) -> int:
    cfg = _require_toml()
    modules = _modules_dir(cfg)

    print(f"package: {cfg.name} v{cfg.version}\n")

    if not cfg.dependencies:
        print("no dependencies")
        return 0

    print("dependencies:")
    for name, constraint in cfg.dependencies.items():
        installed = any(
            e.startswith(name + "@")
            for e in (os.listdir(modules) if os.path.isdir(modules) else [])
        )
        status = "✓" if installed else "✗ not installed"
        print(f"  {name:<40} {constraint:<20} {status}")

    return 0


# ── build ─────────────────────────────────────────────────────────────────────

def cmd_build(args: list) -> int:
    cfg = _require_toml()
    jscc = _find_tool("jscc")

    entry  = cfg.build.entry
    output = cfg.build.output

    if not os.path.exists(entry):
        print(f"error: entry file '{entry}' not found")
        return 1

    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)

    cmd = [jscc, entry, output]

    # extra target flags
    for target in cfg.build.targets:
        cmd += [f"--target={target}"]
    if cfg.build.no_bytecode:
        cmd.append("--no-bytecode")

    # pass any extra args from CLI
    cmd += args

    print(f"building {entry} -> {output}")
    print(f"  {' '.join(cmd)}")

    try:
        result = subprocess.run(cmd)
    except FileNotFoundError:
        print(f"error: 'jscc' not found — is it installed and on PATH?")
        print(f"install from: https://github.com/Fundiman/JadeLang")
        return 1
    if result.returncode == 0:
        print(f"\nbuild OK: {output}")
    else:
        print(f"\nbuild failed (exit {result.returncode})")
    return result.returncode


# ── run ───────────────────────────────────────────────────────────────────────

def cmd_run(args: list) -> int:
    # build first
    build_result = cmd_build([])
    if build_result != 0:
        return build_result

    cfg  = _require_toml()
    jvk  = _find_tool("jvk")
    joc  = cfg.build.output

    cmd = [jvk, joc] + args
    print(f"\nrunning: {' '.join(cmd)}\n")
    result = subprocess.run(cmd)
    return result.returncode


# ── search ────────────────────────────────────────────────────────────────────

def cmd_search(args: list) -> int:
    if not args:
        print("usage: jpkg search <query>")
        return 1

    query = " ".join(args)
    print(f"searching for '{query}'...\n")

    results = search_packages(query)
    if not results:
        print("no packages found")
        return 0

    print(f"{'name':<40} {'version':<15} description")
    print("-" * 80)
    for r in results:
        print(f"{r['name']:<40} {r['version']:<15} {r['description']}")
    return 0


# ── info ──────────────────────────────────────────────────────────────────────

def cmd_info(args: list) -> int:
    if not args:
        print("usage: jpkg info <package>")
        return 1

    name = args[0]
    info = get_package_info(name)

    if not info:
        print(f"package '{name}' not found in registry")
        return 1

    print(f"name:        {name}")
    print(f"latest:      {info.get('latest', '?')}")
    print(f"description: {info.get('description', '')}")
    print(f"author:      {info.get('author', '')}")
    print(f"license:     {info.get('license', '')}")
    print(f"homepage:    {info.get('homepage', '')}")
    versions = info.get("versions", [])
    if versions:
        print(f"versions:    {', '.join(versions)}")
    return 0


# ── publish ───────────────────────────────────────────────────────────────────

def cmd_publish(args: list) -> int:
    cfg = _require_toml()

    # validate
    if not cfg.name or cfg.name.startswith("com.yourname"):
        print("error: set a real package name in jade.toml before publishing")
        return 1
    if not cfg.version:
        print("error: set a version in jade.toml")
        return 1

    joc_path = cfg.build.output

    # build first if .joc doesn't exist
    if not os.path.exists(joc_path):
        print(f".joc not found at {joc_path} — building first...")
        if cmd_build([]) != 0:
            return 1

    print(f"publishing {cfg.name}@{cfg.version} to jade-pkgs...")
    print(f"registry: {REGISTRY_REPO}\n")

    # check git is available
    if not shutil.which("git"):
        print("error: git is required to publish — install git and try again")
        return 1

    # check gh cli for PR creation (optional but recommended)
    has_gh = shutil.which("gh") is not None

    with tempfile.TemporaryDirectory() as tmp:
        try:
            branch, repo_dir = prepare_publish_branch(cfg, joc_path, tmp)
        except subprocess.CalledProcessError as e:
            print(f"error preparing publish: {e}")
            return 1

        if has_gh:
            # push and open PR automatically via gh cli
            print("pushing branch and opening pull request...")
            try:
                subprocess.run(
                    ["git", "-C", repo_dir, "push", "origin", branch],
                    check=True
                )
                subprocess.run(
                    ["gh", "pr", "create",
                     "--repo", "Fundiman/jade-pkgs",
                     "--title", f"add {cfg.name}@{cfg.version}",
                     "--body",  f"Publishing {cfg.name} version {cfg.version}\n\n"
                                f"Author: {', '.join(cfg.authors)}\n"
                                f"License: {cfg.license}\n"
                                f"Description: {cfg.description}",
                     "--base",  "main",
                     "--head",  branch],
                    check=True
                )
                print(f"\n✓ pull request opened!")
                print(f"once merged, '{cfg.name}' will be available via:")
                print(f"  jpkg add {cfg.name}")
            except subprocess.CalledProcessError as e:
                print(f"error opening PR: {e}")
                print("install 'gh' cli and authenticate with 'gh auth login'")
                return 1
        else:
            # manual instructions
            print("to publish manually:")
            print(f"  1. fork {REGISTRY_REPO}")
            print(f"  2. create branch: {branch}")
            print(f"  3. add your package files under packages/{cfg.name.replace('.', '/')}/{cfg.version}/")
            print(f"  4. open a pull request")
            print(f"\ntip: install 'gh' cli to automate this: https://cli.github.com")

    return 0
