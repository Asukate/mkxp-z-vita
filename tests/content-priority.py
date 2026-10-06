#!/usr/bin/env python3
"""Read colliding and missing assets through the production mount order + PhysFS."""
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
SOURCE = r'''
#include "src/filesystem/vita-content.h"
#include <physfs.h>
#include <stdexcept>
#include <string>
#include <iostream>
static void expect(const char *path, const char *wanted) {
  auto *file = PHYSFS_openRead(path);
  if (!file) throw std::runtime_error(std::string("Missing asset: ") + path);
  char buffer[128] = {};
  auto size = PHYSFS_readBytes(file, buffer, sizeof(buffer));
  PHYSFS_close(file);
  if (std::string(buffer, size) != wanted)
    throw std::runtime_error(std::string("Wrong content priority: ") + path);
}
int main(int argc, char **argv) {
  PHYSFS_init(argv[0]);
  std::string root = argv[1];
  auto mounts = VitaContent::mountOrder(root+"/game", root+"/archive.zip",
      {root+"/patch"}, {root+"/rtp"}, root+"/assets.zip");
  for (auto &path : mounts)
    if (!PHYSFS_mount(path.c_str(), nullptr, 1)) throw std::runtime_error("mount failed");
  expect("Graphics/System/Window.png", "archived decorated window");
  expect("Graphics/System/IconSet.png", "explicit patch icons");
  expect("Graphics/Pictures/Loose.png", "loose game supplement");
  expect("Graphics/Pictures/Asset.png", "assets supplement");
  expect("Audio/SE/Missing.ogg", "RTP fallback");
  expect("Graphics/Pictures/Shared.png", "loose game supplement");
  PHYSFS_deinit();
  // An unencrypted game still takes priority over supplemental assets and RTP.
  PHYSFS_init(argv[0]);
  mounts = VitaContent::mountOrder(root+"/game", "", {}, {root+"/rtp"}, root+"/assets.zip");
  for(auto &path : mounts) PHYSFS_mount(path.c_str(), nullptr, 1);
  expect("Graphics/System/Window.png", "loose plain window");
  expect("Graphics/Pictures/Shared.png", "loose game supplement");
  PHYSFS_deinit();
  std::cout << "PASS: archived assets, explicit patches, loose supplements and RTP fallbacks\n";
}
'''
with tempfile.TemporaryDirectory(prefix='hardrpg-content-') as name:
    work = Path(name)
    entries = {
        'game': {'Graphics/System/Window.png': 'loose plain window',
                 'Graphics/System/IconSet.png': 'loose icons',
                 'Graphics/Pictures/Loose.png': 'loose game supplement',
                 'Graphics/Pictures/Shared.png': 'loose game supplement'},
        'patch': {'Graphics/System/IconSet.png': 'explicit patch icons'},
        'rtp': {'Graphics/System/Window.png': 'RTP window',
                'Audio/SE/Missing.ogg': 'RTP fallback'},
    }
    for folder, files in entries.items():
        for relative, data in files.items():
            path = work / folder / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data)
    for archive, files in {
        'archive.zip': {'Graphics/System/Window.png': 'archived decorated window',
                        'Graphics/System/IconSet.png': 'archived icons'},
        'assets.zip': {'Graphics/Pictures/Asset.png': 'assets supplement',
                       'Graphics/Pictures/Shared.png': 'assets supplement'},
    }.items():
        with zipfile.ZipFile(work / archive, 'w') as z:
            for path, data in files.items():
                z.writestr(path, data)
    cpp = work / 'content.cpp'
    cpp.write_text(SOURCE)
    subprocess.run(['c++', '-std=c++14', '-I'+str(ROOT), str(cpp), '-lphysfs', '-o', str(work/'content')], check=True)
    subprocess.run([str(work/'content'), str(work)], check=True)
