#ifndef SSVMADDON_H
#define SSVMADDON_H

#include "vm/configure.h"
#include "vm/vm.h"
#include "host/wasi/wasimodule.h"
#include <napi.h>
#include <string>
#include <vector>

class SSVMAddon : public Napi::ObjectWrap<SSVMAddon> {
public:
  static Napi::Object Init(Napi::Env Env, Napi::Object Exports);
  SSVMAddon(const Napi::CallbackInfo &Info);
  ~SSVMAddon(){
    delete Configure;
    delete VM;
  };

private:
  enum class InputMode {
    File,
    Bytecode
  };
  static Napi::FunctionReference Constructor;
  SSVM::VM::Configure *Configure;
  SSVM::VM::VM *VM;
  SSVM::Runtime::Instance::MemoryInstance *MemInst;
  size_t WasiEnvDefaultLength;
  SSVM::Host::WasiModule *WasiMod;
  bool WBMode;
  InputMode IMode;
  std::string InputPath;
  std::vector<uint8_t> InputBytecode;
  std::vector<uint8_t> ResultData;

  void EnableWasmBindgen(const Napi::CallbackInfo &Info);
  void PrepareResource(const Napi::CallbackInfo &Info);
  void ReleaseResource();
  void PrepareResourceWB(const Napi::CallbackInfo &Info,
      std::vector<SSVM::ValVariant> &Args);
  void ReleaseResourceWB(const uint32_t Offset, const uint32_t Size);
  Napi::Value Run(const Napi::CallbackInfo &Info);
  Napi::Value RunInt(const Napi::CallbackInfo &Info);
  Napi::Value RunString(const Napi::CallbackInfo &Info);
  Napi::Value RunUint8Array(const Napi::CallbackInfo &Info);
};

#endif
