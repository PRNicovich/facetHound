# -*- coding: utf-8 -*-

import hashlib
import pickle
import pathlib
import time

from gem_generator import build_gem


CACHE_VERSION = 1


def file_sha256(path, block_size=1024 * 1024):
    h = hashlib.sha256()

    with open(path, "rb") as f:
        while True:
            block = f.read(block_size)
            if not block:
                break
            h.update(block)

    return h.hexdigest()


def default_cache_dir(path):
    path = pathlib.Path(path)
    return path.parent / ".gem_cache"


def cache_path_for_file(path, cache_dir=None):
    path = pathlib.Path(path)

    if cache_dir is None:
        cache_dir = default_cache_dir(path)
    else:
        cache_dir = pathlib.Path(cache_dir)

    cache_dir.mkdir(parents=True, exist_ok=True)

    digest = file_sha256(path)
    name = f"{path.stem}_{digest[:16]}_v{CACHE_VERSION}.pkl"

    return cache_dir / name, digest


def cache_is_valid(payload, path, digest):
    path = pathlib.Path(path)
    st = path.stat()

    meta = payload.get("meta", {})

    return (
        meta.get("cache_version") == CACHE_VERSION
        and meta.get("source_sha256") == digest
        and meta.get("source_size") == st.st_size
    )


def save_gem_cache(cache_path, path, digest, gem_data):
    path = pathlib.Path(path)
    st = path.stat()

    payload = {
        "meta": {
            "cache_version": CACHE_VERSION,
            "created_unix": time.time(),
            "source_path": str(path),
            "source_name": path.name,
            "source_sha256": digest,
            "source_size": st.st_size,
            "source_mtime_ns": st.st_mtime_ns,
        },
        "gem_data": gem_data,
    }

    with open(cache_path, "wb") as f:
        pickle.dump(payload, f, protocol=pickle.HIGHEST_PROTOCOL)


def load_gem_cache(cache_path):
    with open(cache_path, "rb") as f:
        return pickle.load(f)


def load_or_build_gem(path, force_rebuild=False, cache_dir=None):
    path = pathlib.Path(path)

    cache_path, digest = cache_path_for_file(path, cache_dir=cache_dir)

    if not force_rebuild and cache_path.exists():
        try:
            payload = load_gem_cache(cache_path)

            if cache_is_valid(payload, path, digest):
                print(f"loaded gem cache: {cache_path}")
                return payload["gem_data"]

            print("cache stale; rebuilding")

        except Exception as e:
            print(f"cache load failed; rebuilding: {e}")

    print("building gem geometry")
    gem_data = build_gem(path)

    try:
        save_gem_cache(cache_path, path, digest, gem_data)
        print(f"saved gem cache: {cache_path}")

    except Exception as e:
        print(f"cache save failed: {e}")

    return gem_data


def clear_gem_cache(path, cache_dir=None):
    path = pathlib.Path(path)

    if cache_dir is None:
        cache_dir = default_cache_dir(path)
    else:
        cache_dir = pathlib.Path(cache_dir)

    if not cache_dir.exists():
        return 0

    count = 0

    for p in cache_dir.glob(f"{path.stem}_*_v*.pkl"):
        p.unlink()
        count += 1

    return count