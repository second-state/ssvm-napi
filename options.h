#pragma once

#include <string>
#include <vector>

namespace SSVM {
namespace NAPI {

class SSVMOptions {
private:
  bool ReactorMode;
  bool AOTMode;
  std::vector<std::string> WasiCmdArgs, WasiDirs, WasiEnvs;

public:
  void setReactorMode(bool Value = true) { ReactorMode = Value; }
  void setAOTMode(bool Value = true) { AOTMode = Value; }
  void setWasiCmdArgs(const std::vector<std::string> &WCA) {
    WasiCmdArgs = WCA;
  }
  void setWasiDirs(const std::vector<std::string> &WD) { WasiDirs = WD; }
  void setWasiEnvs(const std::vector<std::string> &WE) { WasiEnvs = WE; }
  bool isReactorMode() const noexcept { return ReactorMode; }
  bool isAOTMode() const noexcept { return AOTMode; }
  const std::vector<std::string> &getWasiCmdArgs() const { return WasiCmdArgs; }
  std::vector<std::string> &getWasiCmdArgs() { return WasiCmdArgs; }
  const std::vector<std::string> &getWasiDirs() const { return WasiDirs; }
  std::vector<std::string> &getWasiDirs() { return WasiDirs; }
  const std::vector<std::string> &getWasiEnvs() const { return WasiEnvs; }
  std::vector<std::string> &getWasiEnvs() { return WasiEnvs; }
};

} // namespace NAPI
} // namespace SSVM
