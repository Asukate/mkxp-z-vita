#include "native_launcher/np2kai_handoff.h"
#include <iostream>
int main(int argc, char **argv) {
  try {
    if (argc != 4)
      return 2;
    auto r = hardrpg::np2kaiRequest(hardrpg::readEasyRpgRequest(argv[1]), argv[2], {argv[3]});
    json5pp::value out(json5pp::object({}));
    auto &o = out.as_object();
    o["project"] = r.project;
    o["disk"] = r.disk;
    o["font"] = r.font;
    o["save"] = r.save;
    json5pp::value options(json5pp::object({}));
    for (auto &p : r.options)
      options.as_object()[p.first] = p.second;
    o["options"] = options;
    std::cout << out.stringify();
  } catch (const std::exception &e) {
    std::cerr << e.what();
    return 1;
  }
}
