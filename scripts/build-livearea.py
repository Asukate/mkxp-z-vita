#!/usr/bin/env python3
"""Render the LiveArea version label without modifying the artwork."""
import argparse
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def render(template: bytes, version: str) -> bytes:
    root = ET.fromstring(template)
    labels = [node for node in root.iter('str') if node.text == '@HARDRPG_VERSION@']
    if len(labels) != 1 or not version.strip():
        raise ValueError('LiveArea needs one version label and a nonempty version')
    labels[0].text = version
    return ET.tostring(root, encoding='utf-8', xml_declaration=True) + b'\n'


def release_version(header: Path) -> str:
    match = re.search(r'^#define HARDRPG_VERSION "([^"]+)"$', header.read_text(), re.M)
    if not match:
        raise ValueError('Missing release version')
    return match[1]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('template', type=Path)
    parser.add_argument('output', type=Path)
    version = parser.add_mutually_exclusive_group(required=True)
    version.add_argument('--version')
    version.add_argument('--release', type=Path)
    args = parser.parse_args()
    label = release_version(args.release) if args.release else args.version
    args.output.write_bytes(render(args.template.read_bytes(), label))
