#include "ssvmaddon.h"
#include "support/log.h"

Napi::FunctionReference SSVMAddon::Constructor;

Napi::Object SSVMAddon::Init(Napi::Env Env, Napi::Object Exports) {
  Napi::HandleScope Scope(Env);

  Napi::Function Func =
      DefineClass(Env, "VM",
                  {InstanceMethod("RunInt", &SSVMAddon::RunInt),
                   InstanceMethod("RunString", &SSVMAddon::RunString),
                   InstanceMethod("RunUint8Array", &SSVMAddon::RunUint8Array),
                   InstanceMethod("GetMemoryBuffer", &SSVMAddon::GetMemoryBuffer)});

  Constructor = Napi::Persistent(Func);
  Constructor.SuppressDestruct();

  Exports.Set("VM", Func);
  return Exports;
}

SSVMAddon::SSVMAddon(const Napi::CallbackInfo &Info)
    : Napi::ObjectWrap<SSVMAddon>(Info), VM(this->Configure), MemInst(nullptr) {
  Napi::Env Env = Info.Env();
  Napi::HandleScope Scope(Env);
  SSVM::Log::setErrorLoggingLevel();

  if (Info.Length() <= 0 || !Info[0].IsString()) {
    Napi::Error::New(Env, "wasm file expected").ThrowAsJavaScriptException();
    return;
  }

  std::string Path = Info[0].As<Napi::String>().Utf8Value();
  if (!(this->VM.loadWasm(Path))) {
    Napi::Error::New(Env, "wasm file load failed").ThrowAsJavaScriptException();
    return;
  }

  if (!(this->VM.validate())) {
    Napi::Error::New(Env, "wasm file validate failed")
        .ThrowAsJavaScriptException();
    return;
  }

  if (!(this->VM.instantiate())) {
    Napi::Error::New(Env, "wasm file instantiate failed")
        .ThrowAsJavaScriptException();
    return;
  }

  // Get memory instance
  auto &Store = this->VM.getStoreManager();
  auto *ModInst = *(Store.getActiveModule());
  uint32_t MemAddr = *(ModInst->getMemAddr(0));
  this->MemInst = *Store.getMemory(MemAddr);
}

void SSVMAddon::PrepareResource(const Napi::CallbackInfo &Info,
                                std::vector<SSVM::ValVariant> &Args) {
  for (std::size_t I = 1; I < Info.Length(); I++) {
    Napi::Value Arg = Info[I];
    uint32_t MallocSize = 0, MallocAddr = 0;
    if (Arg.IsNumber()) {
      Args.emplace_back(Arg.As<Napi::Number>().Uint32Value());
      continue;
    } else if (Arg.IsString()) {
      std::string StrArg = Arg.As<Napi::String>().Utf8Value();
      MallocSize = StrArg.length();
    } else if (Arg.IsTypedArray() &&
               Arg.As<Napi::TypedArray>().TypedArrayType() ==
                   napi_uint8_array) {
      Napi::ArrayBuffer DataBuffer = Arg.As<Napi::TypedArray>().ArrayBuffer();
      MallocSize = DataBuffer.ByteLength();
    } else {
      // TODO: support other types
      Napi::TypeError::New(Info.Env(), "unsupported argument type")
          .ThrowAsJavaScriptException();
      return;
    }

    // Malloc
    std::vector<SSVM::ValVariant> Params, Rets;
    Params.emplace_back(MallocSize);
    Rets = *(this->VM.execute("__wbindgen_malloc", Params));
    MallocAddr = std::get<uint32_t>(Rets[0]);

    // Prepare arguments and memory data
    Args.emplace_back(MallocAddr);
    Args.emplace_back(MallocSize);

    // Setup memory
    if (Arg.IsString()) {
      std::string StrArg = Arg.As<Napi::String>().Utf8Value();
      std::vector<uint8_t> StrArgVec(StrArg.begin(), StrArg.end());
      this->MemInst->setBytes(StrArgVec, MallocAddr, 0, StrArgVec.size());
    } else if (Arg.IsTypedArray() &&
               Arg.As<Napi::TypedArray>().TypedArrayType() ==
                   napi_uint8_array) {
      Napi::ArrayBuffer DataBuffer = Arg.As<Napi::TypedArray>().ArrayBuffer();
      uint8_t *Data = (uint8_t *)DataBuffer.Data();
      this->MemInst->setArray(Data, MallocAddr, DataBuffer.ByteLength());
    }
  }
}

void SSVMAddon::ReleaseResource(const uint32_t Offset, const uint32_t Size) {
  std::vector<SSVM::ValVariant> Params = {Offset, Size};
  this->VM.execute("__wbindgen_free", Params);
}

Napi::Value SSVMAddon::RunInt(const Napi::CallbackInfo &Info) {
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  std::vector<SSVM::ValVariant> Args, Rets;
  this->PrepareResource(Info, Args);
  auto Res = this->VM.execute(FuncName, Args);

  if (Res) {
    Rets = *Res;
    return Napi::Number::New(Info.Env(), std::get<uint32_t>(Rets[0]));
  } else {
    Napi::Error::New(Info.Env(), "SSVM execution failed")
        .ThrowAsJavaScriptException();
    return Napi::Value();
  }
}

Napi::Value SSVMAddon::RunString(const Napi::CallbackInfo &Info) {
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  uint32_t ResultMemAddr = 8;
  std::vector<SSVM::ValVariant> Args, Rets;
  Args.emplace_back(ResultMemAddr);
  this->PrepareResource(Info, Args);
  auto Res = this->VM.execute(FuncName, Args);
  if (!Res) {
    Napi::Error::New(Info.Env(), "SSVM execution failed")
        .ThrowAsJavaScriptException();
    return Napi::Value();
  }

  std::vector<uint8_t> ResultMem = *(this->MemInst->getBytes(ResultMemAddr, 8));
  uint32_t ResultDataAddr = ResultMem[0] | (ResultMem[1] << 8) |
                            (ResultMem[2] << 16) | (ResultMem[3] << 24);
  uint32_t ResultDataLen = ResultMem[4] | (ResultMem[5] << 8) |
                           (ResultMem[6] << 16) | (ResultMem[7] << 24);
  std::vector<uint8_t> ResultData =
      *(this->MemInst->getBytes(ResultDataAddr, ResultDataLen));
  this->ReleaseResource(ResultDataAddr, ResultDataLen);

  std::string ResultString(ResultData.begin(), ResultData.end());
  return Napi::String::New(Info.Env(), ResultString);
}

Napi::Value SSVMAddon::RunUint8Array(const Napi::CallbackInfo &Info) {
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  uint32_t ResultMemAddr = 8;
  std::vector<SSVM::ValVariant> Args, Rets;
  Args.emplace_back(ResultMemAddr);
  this->PrepareResource(Info, Args);
  auto Res = this->VM.execute(FuncName, Args);
  if (!Res) {
    Napi::Error::New(Info.Env(), "SSVM execution failed")
        .ThrowAsJavaScriptException();
    return Napi::Value();
  }

  std::vector<uint8_t> ResultMem = *(this->MemInst->getBytes(ResultMemAddr, 8));
  uint32_t ResultDataAddr = ResultMem[0] | (ResultMem[1] << 8) |
                            (ResultMem[2] << 16) | (ResultMem[3] << 24);
  uint32_t ResultDataLen = ResultMem[4] | (ResultMem[5] << 8) |
                           (ResultMem[6] << 16) | (ResultMem[7] << 24);
  this->ResultData = *(this->MemInst->getBytes(ResultDataAddr, ResultDataLen));
  this->ReleaseResource(ResultDataAddr, ResultDataLen);

  Napi::ArrayBuffer ResultArrayBuffer =
      Napi::ArrayBuffer::New(Info.Env(), &(this->ResultData[0]), ResultDataLen);
  Napi::Uint8Array ResultTypedArray = Napi::Uint8Array::New(
      Info.Env(), ResultDataLen, ResultArrayBuffer, 0, napi_uint8_array);
  return ResultTypedArray;
}

Napi::Value SSVMAddon::GetMemoryBuffer(const Napi::CallbackInfo &Info) {
  std::vector<uint8_t> ResultMem = this->MemInst->getDataVector();
  Napi::ArrayBuffer ResultArrayBuffer =
      Napi::ArrayBuffer::New(Info.Env(), &(ResultMem[0]), ResultMem.size());
  Napi::Uint8Array ResultTypedArray = Napi::Uint8Array::New(
      Info.Env(), ResultMem.size(), ResultArrayBuffer, 0, napi_uint8_array);
  return ResultTypedArray;
}
