#pragma once

#include <fstream> // std::ifstream, std::ofstream
#include <iterator>
#include <limits>
#include <string>

#include <boost/functional/hash.hpp>

namespace SSVM {
namespace NAPI {

class SSVMCache {
private:
  std::string Path;
  size_t CodeHash;

public:
  inline void init(const std::vector<uint8_t> &Data) {
    Path = std::string("/tmp/ssvm.tmp.") + std::to_string(hash(Data)) +
           std::string(".so");
  }

  inline size_t hash(const std::vector<uint8_t> &Data) {
    CodeHash = boost::hash_range(Data.begin(), Data.end());
    return CodeHash;
  }

  inline bool isCached() const {
    std::ifstream File(Path.c_str());
    bool Cached = File.good();
    File.close();
    return Cached;
  }

  inline const std::string &getPath() const noexcept { return Path; }

  inline void dumpToFile(const std::vector<uint8_t> &Data) {
    init(Data);
    std::ofstream File(Path.c_str());
    std::ostream_iterator<uint8_t> OutIter(File);
    std::copy(Data.begin(), Data.end(), OutIter);
    File.close();
  }
};

} // namespace NAPI
} // namespace SSVM
