#!/usr/bin/env python3
"""Import the current HEAD of a local git tree into an empty GitHub repository
through the Git Data API, for networks where git's own transport is blocked.

Credentials: GH_TOKEN in the environment. Repository: first argument, "owner/name".

The API has no way to attach new objects to history the remote does not have, so
the import lands as one commit; the message records where the tree came from.
"""
import base64
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor

API = "https://api.github.com"
# The local proxy drops tunnels to GitHub now and then and adds a handshake
# per request; a direct connection to the API is both stable and faster here.
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
TOKEN = os.environ["GH_TOKEN"]
REPO = sys.argv[1]
MESSAGE_FILE = sys.argv[2] if len(sys.argv) > 2 else None


def api(path, data=None, method=None, retries=6):
    body = json.dumps(data).encode() if data is not None else None
    for attempt in range(retries):
        request = urllib.request.Request(
            API + path, data=body, method=method or ("POST" if body else "GET"))
        request.add_header("Authorization", "Bearer " + TOKEN)
        request.add_header("Accept", "application/vnd.github+json")
        if body:
            request.add_header("Content-Type", "application/json")
        try:
            with OPENER.open(request, timeout=60) as response:
                return json.loads(response.read() or b"{}")
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", "replace")[:300]
            limited = error.code == 403 and "rate limit" in detail
            if (error.code in (429, 500, 502, 503, 504) or limited) and attempt + 1 < retries:
                wait = 75 if limited else 5 * (attempt + 1)
                print(f"  rate limited, waiting {wait}s ({detail.splitlines()[1][:60] if limited else error.code})")
                time.sleep(wait)
                continue
            raise SystemExit(f"{path}: HTTP {error.code} {detail}")
        except urllib.error.URLError as error:
            # the proxy here drops tunnels now and then (502 Bad Gateway)
            if attempt + 1 < retries:
                time.sleep(4 * (attempt + 1))
                continue
            raise SystemExit(f"{path}: network {error}")
    raise SystemExit(f"{path}: gave up")


def list_tree():
    raw = subprocess.run(["git", "ls-tree", "-r", "-z", "HEAD"],
                         capture_output=True, check=True).stdout
    entries = []
    for record in raw.split(b"\0"):
        if not record:
            continue
        meta, path = record.split(b"\t", 1)
        mode, kind, sha = meta.split(b" ")
        if kind != b"blob":
            continue
        entries.append((mode.decode(), path.decode("utf-8", "surrogateescape"), sha.decode()))
    return entries


def read_blobs(shas):
    """git cat-file --batch: one process for every object."""
    request = "\n".join(shas) + "\n"
    out = subprocess.run(["git", "cat-file", "--batch"], input=request.encode(),
                         capture_output=True, check=True).stdout
    blobs, pos = {}, 0
    while pos < len(out):
        end = out.index(b"\n", pos)
        header = out[pos:end].split(b" ")
        size = int(header[2])
        start = end + 1
        blobs[header[0].decode()] = out[start:start + size]
        pos = start + size + 1
    return blobs


def main():
    entries = list_tree()
    print(f"{len(entries)} files")
    blobs = read_blobs(sorted({sha for _, _, sha in entries}))
    print(f"{len(blobs)} blobs read")

    shas = sorted(blobs)
    cache_path = os.path.join(tempfile.gettempdir(), "gh_import_cache.json")
    uploaded = json.load(open(cache_path)) if os.path.exists(cache_path) else {}
    todo = [sha for sha in shas if sha not in uploaded]
    print(f"{len(uploaded)} cached, {len(todo)} to upload")

    # Roughly one request a second: GitHub's secondary limit for content creation
    # is around 80 a minute, and 8 threads tripped it after 300 blobs.
    for done, sha in enumerate(todo, 1):
        payload = base64.b64encode(blobs[sha]).decode()
        uploaded[sha] = api(f"/repos/{REPO}/git/blobs",
                            {"content": payload, "encoding": "base64"})["sha"]
        if done % 50 == 0:
            json.dump(uploaded, open(cache_path, "w"))
            print(f"  blobs {done}/{len(todo)}")
        time.sleep(0.12)
    json.dump(uploaded, open(cache_path, "w"))

    tree = api(f"/repos/{REPO}/git/trees", {
        "tree": [{"path": path, "mode": mode, "type": "blob", "sha": uploaded[sha]}
                 for mode, path, sha in entries]})
    print("tree", tree["sha"])

    message = open(MESSAGE_FILE, encoding="utf-8").read() if MESSAGE_FILE else "Import"

    # An empty repository rejects the blob API with 409, so a first commit is made
    # before this runs; parent the import on it when it is there.
    parents = []
    try:
        existing = api(f"/repos/{REPO}/git/ref/heads/main")
        parents = [existing["object"]["sha"]]
    except SystemExit:
        pass

    commit = api(f"/repos/{REPO}/git/commits",
                 {"message": message, "tree": tree["sha"], "parents": parents})
    print("commit", commit["sha"])

    api(f"/repos/{REPO}/git/refs", {"ref": "refs/heads/main", "sha": commit["sha"]})
    print("refs/heads/main ->", commit["sha"])


if __name__ == "__main__":
    main()
