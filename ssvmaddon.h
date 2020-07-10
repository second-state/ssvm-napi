#ifndef SSVMADDON_H
#define SSVMADDON_H

#include "vm/configure.h"
#include "vm/vm.h"
#include "host/wasi/wasimodule.h"
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
  SSVM::Host::WasiModule *WasiMod;
  bool WBMode;
  bool Inited;
  InputMode IMode;
  std::string InputPath;
  std::string CurAotBinPath;
  std::vector<uint8_t> InputBytecode;
  std::vector<uint8_t> ResultData;
  std::vector<std::string> ArgsVec;
  Napi::Object WasiOptions;

  std::unordered_map<std::size_t, std::string> CodeCache;

  /// Setup related functions
  void InitVM(const Napi::CallbackInfo &Info);
  /// WasmBindgen related functions
  void EnableWasmBindgen(const Napi::CallbackInfo &Info);
  void PrepareResourceWB(const Napi::CallbackInfo &Info,
      std::vector<SSVM::ValVariant> &Args);
  void ReleaseResourceWB(const uint32_t Offset, const uint32_t Size);
  /// Wasi related functions
  void PrepareResource(const Napi::CallbackInfo &Info);
  void ReleaseResource();
  /// Run functions
  Napi::Value Run(const Napi::CallbackInfo &Info);
  Napi::Value RunInt(const Napi::CallbackInfo &Info);
  Napi::Value RunString(const Napi::CallbackInfo &Info);
  Napi::Value RunUint8Array(const Napi::CallbackInfo &Info);
  /// Aot related functions
  void Compile(const Napi::CallbackInfo &Info);
  void RunAot(const Napi::CallbackInfo &Info);
  Napi::Value GetAotBinary(const Napi::CallbackInfo &Info);
};

#endif
