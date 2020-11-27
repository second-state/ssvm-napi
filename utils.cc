#include "utils.h"

#include "common/filesystem.h"

#include <iostream>
#include <string>

namespace SSVM {
namespace NAPI {

bool checkLibCXXVersion() {
#ifdef __GLIBCXX__
#ifdef __aarch64__
  std::string GLibPath = "/usr/lib/aarch64-linux-gnu";
#endif
#ifdef __x86_64__
  std::string GLibPath = "/usr/lib/x86_64-linux-gnu";
#endif
  std::string GLibCXXName = "libstdc++.so.6.0.";
  std::string CurrentGLibVer = "unknown";
  bool IsVersionCompatible = false;
  for (const auto &Entry : std::filesystem::directory_iterator(GLibPath)) {
    std::string LibName = Entry.path().filename().string();
    size_t Pos = LibName.find(GLibCXXName);
    if (Pos != std::string::npos) {
      CurrentGLibVer = LibName;
      std::string GV = LibName.substr(GLibCXXName.length(), LibName.length());
      if (std::stoi(GV) >= 28) {
        IsVersionCompatible = true;
      }
    }
  }
  if (!IsVersionCompatible) {
    std::cerr << "============================================================="
                 "=======\n"
              << "Error: libstdc++ version mismatched!\n"
              << "Your current version is " << CurrentGLibVer
              << " which is less than libstdc++6.0.28\n"
              << "SSVM relies on >=libstdc++6.0.28 (GLIBCXX >= 3.4.28)\n"
              << "Please upgrade the libstdc++6 library.\n\n"
              << "For more details, refer to our environment set up document: "
                 "https://www.secondstate.io/articles/setup-rust-nodejs/\n"
              << "============================================================="
                 "=======\n";
    return false;
  } else {
    return true;
  }
#endif
  return false;
}

} // namespace NAPI
} // namespace SSVM
