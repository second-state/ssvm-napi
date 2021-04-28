#pragma once

#include <napi.h>
#include <string>
#include <vector>

namespace SSVM {
namespace NAPI {

static inline std::string kAllowedCommandsString [[maybe_unused]] = "AllowCommands";
static inline std::string kAllowedCommandsAllString [[maybe_unused]] = "AllowAllCommands";
static inline std::string kCmdArgsString [[maybe_unused]] = "args";
static inline std::string kPreOpensString [[maybe_unused]] = "preopens";
static inline std::string kEnvString [[maybe_unused]] = "env";
static inline std::string kEnableAOTString [[maybe_unused]] = "EnableAOT";
static inline std::string kEnableMeasurementString [[maybe_unused]] = "EnableMeasurement";

class SSVMOptions {
private:
  bool ReactorMode;
  bool AOTMode;
  bool Measure;
  bool AllowedCmdsAll;
  std::vector<std::string> WasiCmdArgs, WasiDirs, WasiEnvs, AllowedCmds;

public:
  void setReactorMode(bool Value = true) { ReactorMode = Value; }
  void setAOTMode(bool Value = true) { AOTMode = Value; }
  void setMeasure(bool Value = true) { Measure = Value; }
  void setAllowedCmdsAll(bool Value = true) { AllowedCmdsAll = Value; }
  void setWasiCmdArgs(const std::vector<std::string> &WCA) {
    WasiCmdArgs = WCA;
  }
  void setAllowedCmds(const std::vector<std::string> &AC) {
    AllowedCmds = AC;
  }
  void setWasiDirs(const std::vector<std::string> &WD) { WasiDirs = WD; }
  void setWasiEnvs(const std::vector<std::string> &WE) { WasiEnvs = WE; }
  bool isReactorMode() const noexcept { return ReactorMode; }
  bool isAOTMode() const noexcept { return AOTMode; }
  bool isMeasuring() const noexcept { return Measure; }
  bool isAllowedCmdsAll() const noexcept { return AllowedCmdsAll; }
  const std::vector<std::string> &getWasiCmdArgs() const { return WasiCmdArgs; }
  std::vector<std::string> &getWasiCmdArgs() { return WasiCmdArgs; }
  const std::vector<std::string> &getAllowedCmds() const { return AllowedCmds; }
  std::vector<std::string> &getAllowedCmds() { return AllowedCmds; }
  const std::vector<std::string> &getWasiDirs() const { return WasiDirs; }
  std::vector<std::string> &getWasiDirs() { return WasiDirs; }
  const std::vector<std::string> &getWasiEnvs() const { return WasiEnvs; }
  std::vector<std::string> &getWasiEnvs() { return WasiEnvs; }
  bool parse(const Napi::Object &Options);
};

} // namespace NAPI
} // namespace SSVM
