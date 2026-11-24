#!/usr/bin/env python3
"""Export a Steam achievement catalog for HardRPG."""

import argparse
import json
from pathlib import Path
import re
import urllib.parse
import urllib.request


CATALOG_LIMIT = 256 * 1024


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
                (a["api_name"], a["name"], a.get("description", ""), a.get("hidden", False))
                for g in data["groups"] if g["ispublic"] and not g["archived"]
                for a in g["achievements"] if not a["archived"]
            )
    raise ValueError("Steam did not return a public achievement schema for this App ID")


def api_catalog(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    return (
        (a["name"], a["displayName"], a.get("description", ""), a.get("hidden", 0) == 1)
        for a in data["game"]["availableGameStats"]["achievements"]
    )


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("appid", type=int, help="Steam App ID")
    cli.add_argument("output", type=Path, help="catalog file to create")
    cli.add_argument("--language", default="english", help="Steam Community display language")
    cli.add_argument("--schema", type=Path, help="import a saved GetSchemaForGame JSON response")
    args = cli.parse_args()
    if args.appid <= 0:
        cli.error("appid must be positive")

    try:
        rows = api_catalog(args.schema) if args.schema else public_catalog(args.appid, args.language)
        catalog = {}
        for id_, name, description, hidden in rows:
            if not 0 < len(id_.encode("utf-8")) <= 100 or any(ord(c) < 32 or ord(c) == 127 for c in id_):
                raise ValueError("Steam returned an unsupported achievement ID")
            catalog[id_] = dict(id=id_, name=name, description=description, hidden=bool(hidden))
        serialized = json.dumps(list(catalog.values()), ensure_ascii=False, indent=2) + "\n"
        if not catalog:
            raise ValueError("Steam did not return any achievements")
        if len(catalog) > 1024 or len(serialized.encode("utf-8")) > CATALOG_LIMIT:
            raise ValueError("catalog exceeds the HardRPG size limit")
        # Keep existing catalogs until the caller chooses where to put the new one.
        with args.output.open("x", encoding="utf-8") as file:
            file.write(serialized)
    except (OSError, ValueError, TypeError, KeyError) as exc:
        cli.exit(1, f"Could not import achievements: {exc}\n")
    print(f"Exported {len(catalog)} achievements to {args.output}")


if __name__ == "__main__":
    main()
