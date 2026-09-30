#!/usr/bin/env python3
"""Copy the WebGL build into a static demo directory.

GitHub rejects files over 100 MiB, so the Emscripten data package is served in
64 MiB pieces. The generated JavaScript retains Emscripten's package metadata
and receives a small fetch adapter that streams the pieces in order.
"""

import argparse
import math
import os
from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parent.parent
SOURCE = Path(os.environ.get("CNA_WEB_BUILD_DIR", ROOT / "build-web")) / "bin"
NAME = "living-room-simulator"
PART_SIZE = 64 * 1024 * 1024
FETCH_CALL = "await fetch(packageName)"


def loader(part_count: int) -> str:
    return f"""// Reassemble the Emscripten data package from GitHub-sized pieces.
async function fetchLivingRoomDataParts(name, totalSize) {{
  const partCount = {part_count};
  let part = 0;
  let reader = null;
  const body = new ReadableStream({{
    async pull(controller) {{
      while (true) {{
        if (reader === null) {{
          if (part === partCount) {{
            controller.close();
            return;
          }}
          const url = name + ".part" + part++;
          const response = await fetch(url);
          if (!response.ok || !response.body) {{
            throw new Error("Failed to load data package part: " + url + " (" + response.status + ")");
          }}
          reader = response.body.getReader();
        }}
        const chunk = await reader.read();
        if (chunk.done) {{
          reader = null;
          continue;
        }}
        controller.enqueue(chunk.value);
        return;
      }}
    }}
  }});
  return {{
    ok: true,
    url: name,
    headers: new Headers({{ "Content-Length": String(totalSize) }}),
    body
  }};
}}
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path, help="Static demo directory")
    args = parser.parse_args()
    destination = args.destination

    sources = {ext: SOURCE / f"{NAME}.{ext}" for ext in ("html", "js", "wasm", "data")}
    for source in sources.values():
        if not source.is_file():
            parser.error(f"Missing build artifact: {source}. Run scripts/build-web.sh first.")

    original_js = sources["js"].read_text()
    if original_js.count(FETCH_CALL) != 1:
        parser.error("The Emscripten data loader changed; update this packaging script.")

    total_size = sources["data"].stat().st_size
    part_count = math.ceil(total_size / PART_SIZE)
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(sources["html"], destination / f"{NAME}.html")
    shutil.copyfile(sources["wasm"], destination / f"{NAME}.wasm")
    for notice in ("LICENSE", "THIRD_PARTY_ASSETS.md"):
        shutil.copyfile(ROOT / notice, destination / notice)
    (destination / f"{NAME}.js").write_text(
        loader(part_count) + original_js.replace(
            FETCH_CALL, "await fetchLivingRoomDataParts(packageName, packageSize)", 1
        )
    )

    with sources["data"].open("rb") as source:
        for part in range(part_count):
            target = destination / f"{NAME}.data.part{part}"
            with target.open("wb") as output:
                remaining = min(PART_SIZE, total_size - part * PART_SIZE)
                while remaining:
                    chunk = source.read(min(1024 * 1024, remaining))
                    if not chunk:
                        raise OSError("Data package ended before its reported size")
                    output.write(chunk)
                    remaining -= len(chunk)
            print(f"{target}: {target.stat().st_size} bytes")
    print(f"Demo entry: {destination / f'{NAME}.html'}")


if __name__ == "__main__":
    main()
