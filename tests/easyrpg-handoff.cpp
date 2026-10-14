#include "native_launcher/easyrpg_handoff.h"
#include <iostream>
int main(int argc, char **argv) {
  try {
    if (argc != 4)
      return 2;
    auto args = hardrpg::easyRpgArguments(hardrpg::readEasyRpgRequest(argv[1]),
                                          argv[2], {argv[3]});
    json5pp::value result(std::initializer_list<json5pp::value>{});
    for (const auto &arg : args)
      result.as_array().push_back(arg);
    std::cout << result.stringify();
  } catch (const std::exception &e) {
    std::cerr << e.what();
    return 1;
  }
}
