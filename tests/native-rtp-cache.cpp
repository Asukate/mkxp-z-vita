#include "native_launcher/model.h"
#include <cassert>
#include <physfs.h>

static unsigned mounts = 0;
extern "C" int __real_PHYSFS_mount(const char *, const char *, int);
extern "C" int __wrap_PHYSFS_mount(const char *path, const char *point,
                                   int append) {
  ++mounts;
  return __real_PHYSFS_mount(path, point, append);
}

int main(int argc, char **argv) {
  assert(argc == 3);
  hardrpg::Paths paths;
  paths.root = argv[1];
  hardrpg::Model model(paths);
  hardrpg::Mount mount;
  auto before = mounts;
  assert(model.inspectRtp(argv[2], "RPGVXAce", mount));
  assert(mount.root == "Wrapped/RPGVXAce");
  assert(mounts ==
         before + 1); // One central-directory parse for the whole pack.
  assert(model.inspectRtp(argv[2], "RPGVXAce", mount));
  assert(mounts == before + 1); // Re-entering Settings reuses the result.
  model.clearRtpCache();
  assert(model.inspectRtp(argv[2], "RPGVXAce", mount));
  assert(mounts ==
         before + 2); // Explicit refresh really checks the archive again.
}
