#ifndef SSVMADDON_H
#define SSVMADDON_H

#include "vm/configure.h"
#include "vm/vm.h"
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
  static Napi::FunctionReference Constructor;
  SSVM::VM::Configure *Configure;
  SSVM::VM::VM *VM;
  SSVM::Runtime::Instance::MemoryInstance *MemInst;
  std::vector<uint8_t> ResultData;

  void PrepareResource(const Napi::CallbackInfo &Info,
                       std::vector<SSVM::ValVariant> &Args);
  void ReleaseResource(const uint32_t Offset, const uint32_t Size);
  Napi::Value RunInt(const Napi::CallbackInfo &Info);
  Napi::Value RunString(const Napi::CallbackInfo &Info);
  Napi::Value RunUint8Array(const Napi::CallbackInfo &Info);
};

#endif
