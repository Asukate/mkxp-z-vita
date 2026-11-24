#!/usr/bin/env python3
"""Export a Steam achievement catalog for HardRPG.

With --record, the output is a folder that gets what the launcher itself
downloads: steam.json and icons/ with both icons of each achievement. Copy
it to ux0:/data/hardrpg/achievements/<game-key>/ on the Vita.
"""

import argparse
import json
from pathlib import Path
import re
import time
import urllib.parse
import urllib.request


CATALOG_LIMIT = 256 * 1024
ICON_LIMIT = 256 * 1024
ICON_URL = "https://cdn.akamai.steamstatic.com/steamcommunity/public/images/apps/{}/{}"
ICON_NAME = re.compile(r"[0-9a-f]{1,76}\.jpg")


def public_catalog(appid, language):
    query = urllib.parse.urlencode({"l": language})
    url = f"https://steamcommunity.com/achievements/{appid}?{query}"
    with urllib.request.urlopen(url, timeout=25) as response:
        page = response.read(4 * 1024 * 1024).decode("utf-8")
    payload = re.search(r'<script\b[^>]*\bid="valve-ssr-data"[^>]*>(.*?)</script>', page, re.S)
    if payload is None:
        raise ValueError("Steam did not return achievement metadata")
    context = json.loads(payload.group(1))["renderContext"]
    queries = json.loads(context["queryData"])["queries"]
    for item in queries:
        if item.get("queryKey") == ["achievements", appid, "schema_v2"]:
            data = item["state"]["data"]
            return (
                (a["api_name"], a["name"], a.get("description", ""), a.get("hidden", False),
                 a.get("icon_achieved", ""), a.get("icon_unachieved", ""))
                for g in data["groups"] if g["ispublic"] and not g["archived"]
                for a in g["achievements"] if not a["archived"]
            )
    raise ValueError("Steam did not return a public achievement schema for this App ID")


def api_catalog(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    return (
        (a["name"], a["displayName"], a.get("description", ""), a.get("hidden", 0) == 1,
         a.get("icon", "").rsplit("/", 1)[-1], a.get("icongray", "").rsplit("/", 1)[-1])
        for a in data["game"]["availableGameStats"]["achievements"]
    )


def save_icon(appid, name, folder):
    """Download one icon into folder; True when it is there."""
    if not ICON_NAME.fullmatch(name):
        return False
    path = folder / name
    if path.exists() and path.stat().st_size > 0:
        return True
    with urllib.request.urlopen(ICON_URL.format(appid, name), timeout=25) as response:
        data = response.read(ICON_LIMIT + 1)
    if len(data) > ICON_LIMIT or data[:2] != b"\xff\xd8":
        return False
    temp = path.with_suffix(".part")
    temp.write_bytes(data)
    temp.replace(path)
    return True


def write_record(appid, language, catalog, icons, folder):
    """steam.json and icons/ as the launcher writes them."""
    folder.mkdir(parents=True, exist_ok=True)
    icon_dir = folder / "icons"
    icon_dir.mkdir(exist_ok=True)
    complete = True
    rows = []
    for id_, row in catalog.items():
        achieved, unachieved = icons.get(id_, ("", ""))
        for name in (achieved, unachieved):
            try:
                complete = save_icon(appid, name, icon_dir) and complete
            except OSError:
                complete = False
        rows.append(dict(row, icon=achieved if ICON_NAME.fullmatch(achieved) else "",
                         iconLocked=unachieved if ICON_NAME.fullmatch(unachieved) else ""))
    record = dict(source="steam", appid=appid, language=language, fetched=int(time.time()),
                  achievements=rows)
    if not complete:
        record["incomplete"] = True
    (folder / "steam.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return complete


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("appid", type=int, help="Steam App ID")
    cli.add_argument("output", type=Path, help="catalog file to create (with --record, a folder)")
    cli.add_argument("--language", default="english", help="Steam Community display language")
    cli.add_argument("--schema", type=Path, help="import a saved GetSchemaForGame JSON response")
    cli.add_argument("--record", action="store_true",
                     help="write steam.json and the icons into the output folder, as the launcher does")
    args = cli.parse_args()
    if args.appid <= 0:
        cli.error("appid must be positive")

    try:
        rows = api_catalog(args.schema) if args.schema else public_catalog(args.appid, args.language)
        catalog = {}
        icons = {}
        for id_, name, description, hidden, icon, locked in rows:
            if not 0 < len(id_.encode("utf-8")) <= 100 or any(ord(c) < 32 or ord(c) == 127 for c in id_):
                raise ValueError("Steam returned an unsupported achievement ID")
            catalog[id_] = dict(id=id_, name=name, description=description, hidden=bool(hidden))
            icons[id_] = (icon, locked)
        serialized = json.dumps(list(catalog.values()), ensure_ascii=False, indent=2) + "\n"
        if not catalog:
            raise ValueError("Steam did not return any achievements")
        if len(catalog) > 1024 or len(serialized.encode("utf-8")) > CATALOG_LIMIT:
            raise ValueError("catalog exceeds the HardRPG size limit")
        if args.record:
            if not write_record(args.appid, args.language, catalog, icons, args.output):
                print("Some icons could not be downloaded; the launcher tries them again.")
            print(f"Saved {len(catalog)} achievements and their icons to {args.output}")
            return
        # Keep existing catalogs until the caller chooses where to put the new one.
        with args.output.open("x", encoding="utf-8") as file:
            file.write(serialized)
    except (OSError, ValueError, TypeError, KeyError) as exc:
        cli.exit(1, f"Could not import achievements: {exc}\n")
    print(f"Exported {len(catalog)} achievements to {args.output}")


if __name__ == "__main__":
    main()
