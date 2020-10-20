#ifndef SSVMADDON_H
#define SSVMADDON_H

#include "bytecode.h"
#include "cache.h"
#include "options.h"

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

  enum class IntKind {
    Default,
    SInt32,
    UInt32,
    SInt64,
    UInt64
  };

private:
  static Napi::FunctionReference Constructor;
  SSVM::VM::Configure *Configure;
  SSVM::VM::VM *VM;
  SSVM::Runtime::Instance::MemoryInstance *MemInst;
  SSVM::Statistics::Statistics Stat;
  SSVM::Host::WasiModule *WasiMod;
  SSVM::NAPI::Bytecode BC;
  SSVM::NAPI::SSVMOptions Options;
  SSVM::NAPI::SSVMCache Cache;
  bool Inited;
  std::vector<uint8_t> ResultData;

  /// Setup related functions
  void InitVM(const Napi::CallbackInfo &Info);
  void LoadWasm(const Napi::CallbackInfo &Info);
  /// WasmBindgen related functions
  void PrepareResource(const Napi::CallbackInfo &Info,
      std::vector<SSVM::ValVariant> &Args, IntKind IntT);
  void PrepareResource(const Napi::CallbackInfo &Info,
      std::vector<SSVM::ValVariant> &Args);
  void ReleaseResource(const Napi::CallbackInfo &Info, const uint32_t Offset, const uint32_t Size);
  /// Run functions
  void Run(const Napi::CallbackInfo &Info);
  Napi::Value RunIntImpl(const Napi::CallbackInfo &Info, IntKind IntT);
  Napi::Value RunInt(const Napi::CallbackInfo &Info);
  Napi::Value RunUInt(const Napi::CallbackInfo &Info);
  Napi::Value RunInt64(const Napi::CallbackInfo &Info);
  Napi::Value RunUInt64(const Napi::CallbackInfo &Info);
  Napi::Value RunString(const Napi::CallbackInfo &Info);
  Napi::Value RunUint8Array(const Napi::CallbackInfo &Info);
  Napi::Value Start(const Napi::CallbackInfo &Info);
  /// Statistics
  Napi::Value GetStatistics(const Napi::CallbackInfo &Info);
  /// AoT functions
  bool Compile();
  void CallAOTInit(const Napi::CallbackInfo &Info);
};

#endif
