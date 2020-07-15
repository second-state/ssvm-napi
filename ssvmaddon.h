#ifndef SSVMADDON_H
#define SSVMADDON_H

#include "vm/configure.h"
#include "vm/vm.h"
#include "host/wasi/wasimodule.h"
#include "common/statistics.h"
#include <napi.h>
#include <string>
#include <vector>
#include <unordered_map>

class SSVMAddon : public Napi::ObjectWrap<SSVMAddon> {
public:
  static Napi::Object Init(Napi::Env Env, Napi::Object Exports);
  SSVMAddon(const Napi::CallbackInfo &Info);
  ~SSVMAddon(){
    delete Configure;
    delete VM;
  };

  enum class InputMode {
    FilePath,
    WasmBytecode,
    ELFBytecode,
    MachOBytecode
  };

private:
  static Napi::FunctionReference Constructor;
  SSVM::VM::Configure *Configure;
  SSVM::VM::VM *VM;
  SSVM::Runtime::Instance::MemoryInstance *MemInst;
  SSVM::Statistics::Statistics Stat;
  SSVM::Host::WasiModule *WasiMod;
  bool WBMode;
  bool Inited;
  InputMode IMode;
  std::string InputPath;
  std::string CurAotBinPath;
  std::vector<uint8_t> InputBytecode;
  std::vector<uint8_t> ResultData;
  std::vector<std::string> ArgsVec;
  std::vector<std::string> WasiCmdArgs, WasiDirs, WasiEnvs;

  std::unordered_map<std::size_t, std::string> CodeCache;

  /// Setup related functions
  void InitVM(const Napi::CallbackInfo &Info);
  /// Prepare wasi module from given wasi options
  bool parseCmdArgs(std::vector<std::string> &CmdArgs, const Napi::Object &Options);
  bool parseDirs(std::vector<std::string> &Dirs, const Napi::Object &Options);
  bool parseEnvs(std::vector<std::string> &Envs, const Napi::Object &Options);
  /// WasmBindgen related functions
  void EnableWasmBindgen(const Napi::CallbackInfo &Info);
  void PrepareResourceWB(const Napi::CallbackInfo &Info,
      std::vector<SSVM::ValVariant> &Args);
  void ReleaseResourceWB(const uint32_t Offset, const uint32_t Size);
  /// Run functions
  Napi::Value RunInt(const Napi::CallbackInfo &Info);
  Napi::Value RunString(const Napi::CallbackInfo &Info);
  Napi::Value RunUint8Array(const Napi::CallbackInfo &Info);
  /// Statistics
  Napi::Value GetStatistics(const Napi::CallbackInfo &Info);
#if 0
  /// Wasi related functions
  void PrepareResource(const Napi::CallbackInfo &Info);
  void ReleaseResource();
  Napi::Value Run(const Napi::CallbackInfo &Info);
  /// Disable aot, it will be split into another tool
  /// Aot related functions
  void Compile(const Napi::CallbackInfo &Info);
  void RunAot(const Napi::CallbackInfo &Info);
  Napi::Value GetAotBinary(const Napi::CallbackInfo &Info);
#endif
};

#endif
