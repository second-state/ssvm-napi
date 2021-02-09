#pragma once

#include <napi.h>
#include <string>
#include <vector>

namespace SSVM {
namespace NAPI {

class SSVMOptions {
private:
  bool ReactorMode;
  bool AOTMode;
  bool Measure;
  std::vector<std::string> WasiCmdArgs, WasiDirs, WasiEnvs;

public:
  void setReactorMode(bool Value = true) { ReactorMode = Value; }
  void setAOTMode(bool Value = true) { AOTMode = Value; }
  void setMeasure(bool Value = true) { Measure = Value; }
  void setWasiCmdArgs(const std::vector<std::string> &WCA) {
    WasiCmdArgs = WCA;
  }
  void setWasiDirs(const std::vector<std::string> &WD) { WasiDirs = WD; }
  void setWasiEnvs(const std::vector<std::string> &WE) { WasiEnvs = WE; }
  bool isReactorMode() const noexcept { return ReactorMode; }
  bool isAOTMode() const noexcept { return AOTMode; }
  bool isMeasuring() const noexcept { return Measure; }
  const std::vector<std::string> &getWasiCmdArgs() const { return WasiCmdArgs; }
  std::vector<std::string> &getWasiCmdArgs() { return WasiCmdArgs; }
  const std::vector<std::string> &getWasiDirs() const { return WasiDirs; }
  std::vector<std::string> &getWasiDirs() { return WasiDirs; }
  const std::vector<std::string> &getWasiEnvs() const { return WasiEnvs; }
  std::vector<std::string> &getWasiEnvs() { return WasiEnvs; }
  bool parse(const Napi::Object &Options);
};

} // namespace NAPI
} // namespace SSVM
