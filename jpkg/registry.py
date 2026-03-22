"""
registry.py — talks to the jade-pkgs GitHub registry
"""

import os
import json
import urllib.request
import urllib.error
import zipfile
import tempfile
import shutil
from typing import Optional

REGISTRY_RAW  = "https://raw.githubusercontent.com/Fundiman/jade-pkgs/main"
REGISTRY_REPO = "https://github.com/Fundiman/jade-pkgs"
REGISTRY_API  = "https://api.github.com/repos/Fundiman/jade-pkgs"

# local cache: ~/.jade/registry/
CACHE_DIR = os.path.join(os.path.expanduser("~"), ".jade", "registry")


def _fetch(url: str) -> bytes:
    """fetch a URL, return bytes"""
    req = urllib.request.Request(url, headers={"User-Agent": "jpkg/0.1.0-alpha"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def _fetch_json(url: str) -> dict:
    return json.loads(_fetch(url))


# ── package index ─────────────────────────────────────────────────────────────

def fetch_index() -> dict:
    """fetch the registry index (packages/index.json)"""
    try:
        return _fetch_json(f"{REGISTRY_RAW}/packages/index.json")
    except Exception:
        # return empty index if registry unreachable
        return {}


def search_packages(query: str) -> list:
    """search for packages matching query string"""
    index = fetch_index()
    q = query.lower()
    results = []
    for name, info in index.items():
        if q in name.lower() or q in info.get("description", "").lower():
            results.append({
                "name":        name,
                "version":     info.get("latest", "?"),
                "description": info.get("description", ""),
                "author":      info.get("author", ""),
            })
    return results


def get_package_info(name: str) -> Optional[dict]:
    """get metadata for a specific package"""
    try:
        return _fetch_json(f"{REGISTRY_RAW}/packages/{name}/info.json")
    except Exception:
        return None


def get_available_versions(name: str) -> list:
    """get all available versions of a package"""
    info = get_package_info(name)
    if not info:
        return []
    return info.get("versions", [])


def resolve_version(name: str, constraint: str) -> Optional[str]:
    """find the best version satisfying the constraint"""
    from config import version_satisfies, parse_version

    versions = get_available_versions(name)
    if not versions:
        # stdlib packages always resolve locally
        if name.startswith("jade.stdlib"):
            return "0.1.0-alpha"
        return None

    # filter satisfying versions, pick latest
    satisfying = [v for v in versions if version_satisfies(v, constraint)]
    if not satisfying:
        return None

    satisfying.sort(key=lambda v: parse_version(v)[0], reverse=True)
    return satisfying[0]


# ── download + cache ──────────────────────────────────────────────────────────

def package_cache_path(name: str, version: str) -> str:
    """path to cached package in ~/.jade/registry/"""
    safe_name = name.replace(".", os.sep)
    return os.path.join(CACHE_DIR, safe_name, version)


def is_cached(name: str, version: str) -> bool:
    return os.path.isdir(package_cache_path(name, version))


def download_package(name: str, version: str) -> bool:
    """download a package from registry into local cache"""

    # stdlib packages are bundled with the Jade installation
    if name.startswith("jade.stdlib"):
        _install_stdlib(name, version)
        return True

    cache_path = package_cache_path(name, version)
    if os.path.isdir(cache_path):
        return True  # already cached

    os.makedirs(cache_path, exist_ok=True)

    try:
        # download the .joc and jade.toml from registry
        safe_name = name.replace(".", "/")
        base_url  = f"{REGISTRY_RAW}/packages/{safe_name}/{version}"

        for filename in ["jade.toml", "out.joc"]:
            url  = f"{base_url}/{filename}"
            dest = os.path.join(cache_path, filename)
            print(f"  downloading {filename}...")
            data = _fetch(url)
            with open(dest, "wb") as f:
                f.write(data)

        return True

    except Exception as e:
        print(f"  error downloading {name}@{version}: {e}")
        shutil.rmtree(cache_path, ignore_errors=True)
        return False


def _install_stdlib(name: str, version: str):
    """install a stdlib package from the bundled jade installation"""
    cache_path = package_cache_path(name, version)
    os.makedirs(cache_path, exist_ok=True)

    # write a marker so jvk knows the stdlib is available
    marker = os.path.join(cache_path, "jade.toml")
    if not os.path.exists(marker):
        with open(marker, "w") as f:
            f.write(f'[package]\nname = "{name}"\nversion = "{version}"\n')


def install_to_modules(name: str, version: str, modules_dir: str) -> bool:
    """symlink or copy a cached package into jade_modules/"""
    cache_path = package_cache_path(name, version)
    if not os.path.isdir(cache_path):
        if not download_package(name, version):
            return False

    # destination: jade_modules/name@version/
    dest = os.path.join(modules_dir, f"{name}@{version}")
    if os.path.exists(dest):
        return True

    os.makedirs(modules_dir, exist_ok=True)
    try:
        os.symlink(cache_path, dest)
    except (OSError, NotImplementedError):
        # fallback: copy instead of symlink (Windows)
        shutil.copytree(cache_path, dest)

    return True


# ── publish helpers ───────────────────────────────────────────────────────────

def prepare_publish_branch(cfg, joc_path: str, tmp_dir: str) -> str:
    """
    clone jade-pkgs, create a branch for this package version,
    add the package files, return the branch name
    """
    import subprocess

    branch = f"pkg/{cfg.name}/{cfg.version}"
    pkg_dir = os.path.join(tmp_dir, "jade-pkgs", "packages",
                           cfg.name.replace(".", os.sep), cfg.version)

    # clone the registry
    print("cloning jade-pkgs registry...")
    subprocess.run(
        ["git", "clone", "--depth=1", REGISTRY_REPO,
         os.path.join(tmp_dir, "jade-pkgs")],
        check=True
    )

    # create branch
    subprocess.run(
        ["git", "-C", os.path.join(tmp_dir, "jade-pkgs"),
         "checkout", "-b", branch],
        check=True
    )

    # add package files
    os.makedirs(pkg_dir, exist_ok=True)

    # copy jade.toml
    shutil.copy(cfg.path, os.path.join(pkg_dir, "jade.toml"))

    # copy .joc
    if os.path.exists(joc_path):
        shutil.copy(joc_path, os.path.join(pkg_dir, "out.joc"))

    # write/update index entry
    index_path = os.path.join(tmp_dir, "jade-pkgs", "packages", "index.json")
    index = {}
    if os.path.exists(index_path):
        with open(index_path) as f:
            index = json.load(f)

    index[cfg.name] = {
        "latest":      cfg.version,
        "description": cfg.description,
        "author":      cfg.authors[0] if cfg.authors else "",
        "license":     cfg.license,
    }

    with open(index_path, "w") as f:
        json.dump(index, f, indent=2)

    # commit
    repo_dir = os.path.join(tmp_dir, "jade-pkgs")
    subprocess.run(["git", "-C", repo_dir, "add", "-A"], check=True)
    subprocess.run(
        ["git", "-C", repo_dir, "commit", "-m",
         f"add {cfg.name}@{cfg.version}"],
        check=True
    )

    return branch, repo_dir
