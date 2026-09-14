#!/usr/bin/env python3
"""Reads assets/external/manifest.json.

  manifest-tool.py fetch-list    name<TAB>url<TAB>file<TAB>sha256 per asset
  manifest-tool.py hash DIR      records the SHA-256 of every fetched file into the manifest
  manifest-tool.py table         prints the Markdown table for THIRD_PARTY_ASSETS.md
"""
import hashlib, json, os, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
path = os.path.join(root, "assets", "external", "manifest.json")

def load():
    with open(path) as f:
        return json.load(f)

def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "fetch-list"
    m = load()
    if cmd == "fetch-list":
        for a in m["assets"]:
            print("\t".join([a["name"], a["url"], a["file"], a.get("sha256", "")]))
    elif cmd == "hash":
        d = sys.argv[2]
        for a in m["assets"]:
            p = os.path.join(d, a["file"])
            if os.path.exists(p):
                h = hashlib.sha256(open(p, "rb").read()).hexdigest()
                a["sha256"] = h
                a["bytes"] = os.path.getsize(p)
        with open(path, "w") as f:
            json.dump(m, f, indent=1)
        print("hashed")
    elif cmd == "table":
        print("| Asset | Author / holder | Source | Licence | Role | Modifications |")
        print("|---|---|---|---|---|---|")
        for a in m["assets"]:
            mods = "; ".join(a.get("transformations", [])) or "none beyond CNA's import"
            print(f"| {a['title']} | {a['author']} | {a['source']} | [{a['licence']}]({a['licenceUrl']}) | {a['role']} | {mods} |")
    else:
        print(__doc__)
        sys.exit(2)

if __name__ == "__main__":
    main()
