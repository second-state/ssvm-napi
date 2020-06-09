#include "ssvmaddon.h"
#include "support/log.h"
#include "support/span.h"

Napi::FunctionReference SSVMAddon::Constructor;

Napi::Object SSVMAddon::Init(Napi::Env Env, Napi::Object Exports) {
  Napi::HandleScope Scope(Env);

  Napi::Function Func =
    DefineClass(Env, "VM",
        {InstanceMethod("Run", &SSVMAddon::Run),
        InstanceMethod("RunInt", &SSVMAddon::RunInt),
        InstanceMethod("RunString", &SSVMAddon::RunString),
        InstanceMethod("RunUint8Array", &SSVMAddon::RunUint8Array)});

  Constructor = Napi::Persistent(Func);
  Constructor.SuppressDestruct();

  Exports.Set("VM", Func);
  return Exports;
}

SSVMAddon::SSVMAddon(const Napi::CallbackInfo &Info)
  : Napi::ObjectWrap<SSVMAddon>(Info), Configure(nullptr),
  VM(nullptr), MemInst(nullptr), WasiEnvDefaultLength(0),
  WasiMod(nullptr), WBMode(false) {

    Napi::Env Env = Info.Env();
    Napi::HandleScope Scope(Env);

    if (Info.Length() <= 0 || (!Info[0].IsString() && !Info[0].IsTypedArray())) {
      Napi::Error::New(Env, "Expected a Wasm file or a Wasm binary sequence.").ThrowAsJavaScriptException();
      return;
    }
    if (Info.Length() == 1) {
      // WasmBindgen mode
      // Assume the WASI options object is {}
      WBMode = true;
    } else if (Info.Length() == 2 && Info[1].IsObject()) {
      // Get a WASI options object
      Napi::Object Options = Info[1].As<Napi::Object>();
      if (Options.Has("DisableWasmBindgen")
          && Options.Get("DisableWasmBindgen").IsBoolean()
          && Options.Get("DisableWasmBindgen").As<Napi::Boolean>().Value()) {
        WBMode = false;
      } else {
        WBMode = true;
      }
    } else {
      Napi::Error::New(Env, "Too many arguments for initializing SSVM-js").ThrowAsJavaScriptException();
      return;
    }

    Configure = new SSVM::VM::Configure();
    Configure->addVMType(SSVM::VM::Configure::VMType::Wasi);
    VM = new SSVM::VM::VM(*Configure);

    SSVM::Log::setErrorLoggingLevel();

    if (Info[0].IsString()) {
      IMode = InputMode::File;
      InputPath = Info[0].As<Napi::String>().Utf8Value();
    } else if (Info[0].IsTypedArray()
        && Info[0].As<Napi::TypedArray>().TypedArrayType() == napi_uint8_array) {
      IMode = InputMode::Bytecode;
      Napi::ArrayBuffer DataBuffer = Info[0].As<Napi::TypedArray>().ArrayBuffer();
      InputBytecode = std::vector<uint8_t>(
          static_cast<uint8_t*>(DataBuffer.Data()),
          static_cast<uint8_t*>(DataBuffer.Data()) + DataBuffer.ByteLength());
    } else {
      Napi::Error::New(Env, "Wasm bytecode is not a valid Uint8Array.").ThrowAsJavaScriptException();
      return;
    }

    WasiMod = dynamic_cast<SSVM::Host::WasiModule *>(
        VM->getImportModule(SSVM::VM::Configure::VMType::Wasi));

    std::vector<std::string> &CmdArgsVec = WasiMod->getEnv().getCmdArgs();
    if (IMode == InputMode::File) {
      CmdArgsVec.push_back(InputPath);
    } else {
      CmdArgsVec.push_back(
          std::string(InputBytecode.begin(), InputBytecode.end()));
    }
    WasiEnvDefaultLength = CmdArgsVec.size();

    if (WBMode) {
      EnableWasmBindgen(Info);
    }
  }

void SSVMAddon::PrepareResource(const Napi::CallbackInfo &Info) {
  std::vector<std::string> &CmdArgsVec = WasiMod->getEnv().getCmdArgs();
  // TODO: Implement WASI options support
  // The WASI options object will have these properties:
  // {
  //   "args": [],
  //   "env": {},
  //   "preopens": {},
  //   "returnOnExit": boolean
  // }
  for (std::size_t I = 1; I < Info.Length(); I++) {
    Napi::Value Arg = Info[I];
    if (Arg.IsNumber()) {
      CmdArgsVec.push_back(std::to_string(Arg.As<Napi::Number>().Uint32Value()));
    } else if (Arg.IsString()) {
      CmdArgsVec.push_back(Arg.As<Napi::String>().Utf8Value());
    } else if (Arg.IsTypedArray() &&
        Arg.As<Napi::TypedArray>().TypedArrayType() ==
        napi_uint8_array) {
      Napi::ArrayBuffer DataBuffer = Arg.As<Napi::TypedArray>().ArrayBuffer();
      std::string ArrayArg = std::string(
          static_cast<char*>(DataBuffer.Data()),
          static_cast<char*>(DataBuffer.Data()) + DataBuffer.ByteLength());
      CmdArgsVec.push_back(ArrayArg);
    } else {
      // TODO: support other types
      Napi::TypeError::New(Info.Env(),
          "unsupported argument type in WASI Options")
        .ThrowAsJavaScriptException();
      return;
    }
  }
}

void SSVMAddon::PrepareResourceWB(const Napi::CallbackInfo &Info,
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
    auto Res = VM->execute("__wbindgen_malloc", Params);
    if (!Res) {
      std::string FatalLocation("SSVMAddon.cc::PrepareResourceWB::__wbindgen_malloc");
      std::string FatalError("SSVM-js malloc failed: wasm-bindgen helper function <__wbindgen_malloc> not found.\n");
      napi_fatal_error(FatalLocation.c_str(), FatalLocation.size(), FatalError.c_str(), FatalError.size());
      return;
    }
    Rets = *Res;
    MallocAddr = std::get<uint32_t>(Rets[0]);

    // Prepare arguments and memory data
    Args.emplace_back(MallocAddr);
    Args.emplace_back(MallocSize);

    // Setup memory
    if (Arg.IsString()) {
      std::string StrArg = Arg.As<Napi::String>().Utf8Value();
      std::vector<uint8_t> StrArgVec(StrArg.begin(), StrArg.end());
      MemInst->setBytes(StrArgVec, MallocAddr, 0, StrArgVec.size());
    } else if (Arg.IsTypedArray() &&
        Arg.As<Napi::TypedArray>().TypedArrayType() ==
        napi_uint8_array) {
      Napi::ArrayBuffer DataBuffer = Arg.As<Napi::TypedArray>().ArrayBuffer();
      uint8_t *Data = (uint8_t *)DataBuffer.Data();
      MemInst->setArray(Data, MallocAddr, DataBuffer.ByteLength());
    }
  }
}

void SSVMAddon::ReleaseResource() {
  std::vector<std::string> &CmdArgsVec = WasiMod->getEnv().getCmdArgs();
  CmdArgsVec.erase(CmdArgsVec.begin()+WasiEnvDefaultLength, CmdArgsVec.end());
}

void SSVMAddon::ReleaseResourceWB(const uint32_t Offset, const uint32_t Size) {
  std::vector<SSVM::ValVariant> Params = {Offset, Size};
  auto Res = VM->execute("__wbindgen_free", Params);
  if (!Res) {
    std::string FatalLocation("SSVMAddon.cc::ReleaseResourceWB::__wbindgen_free");
    std::string FatalError("SSVM-js free failed: wasm-bindgen helper function <__wbindgen_free> not found.\n");
    napi_fatal_error(FatalLocation.c_str(), FatalLocation.size(), FatalError.c_str(), FatalError.size());
    return;
  }
}

Napi::Value SSVMAddon::Run(const Napi::CallbackInfo &Info) {
  if (WBMode) {
    Napi::Error::New(Info.Env(), "Run function only supports WASI mode")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  PrepareResource(Info);
  SSVM::Expect<std::vector<SSVM::ValVariant>> Res;
  if (IMode == InputMode::File) {
    Res = VM->runWasmFile(InputPath, FuncName);
  } else {
    Res = VM->runWasmFile(InputBytecode, FuncName);
  }
  if (!Res) {
    Napi::Error::New(Info.Env(), "SSVM execution failed")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  return Napi::Number::New(Info.Env(), 0);
}

Napi::Value SSVMAddon::RunInt(const Napi::CallbackInfo &Info) {
  if (!WBMode) {
    Napi::Error::New(Info.Env(), "RunInt function only supports WasmBindgen mode")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  std::vector<SSVM::ValVariant> Args, Rets;
  PrepareResourceWB(Info, Args);
  auto Res = VM->execute(FuncName, Args);

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
  if (!WBMode) {
    Napi::Error::New(Info.Env(), "RunInt function only supports WasmBindgen mode")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  uint32_t ResultMemAddr = 8;
  std::vector<SSVM::ValVariant> Args, Rets;
  Args.emplace_back(ResultMemAddr);
  PrepareResourceWB(Info, Args);
  auto Res = VM->execute(FuncName, Args);
  if (!Res) {
    Napi::Error::New(Info.Env(), "SSVM execution failed")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  Rets = *Res;

  uint32_t ResultDataAddr = 0;
  uint32_t ResultDataLen = 0;
  if (auto ResultMem = MemInst->getBytes(ResultMemAddr, 8)) {
    ResultDataAddr = (*ResultMem)[0] | ((*ResultMem)[1] << 8) |
      ((*ResultMem)[2] << 16) | ((*ResultMem)[3] << 24);
    ResultDataLen = (*ResultMem)[4] | ((*ResultMem)[5] << 8) |
      ((*ResultMem)[6] << 16) | ((*ResultMem)[7] << 24);
  } else {
    Napi::Error::New(Info.Env(),
        "Access to forbidden memory address when retrieving address and length of result data")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  if (auto Res = MemInst->getBytes(ResultDataAddr, ResultDataLen)) {
    ResultData = std::vector<uint8_t>((*Res).begin(), (*Res).end());
    ReleaseResourceWB(ResultDataAddr, ResultDataLen);
  } else {
    Napi::Error::New(Info.Env(),
        "Access to forbidden memory address when retrieving result data")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }

  std::string ResultString(ResultData.begin(), ResultData.end());
  return Napi::String::New(Info.Env(), ResultString);
}

Napi::Value SSVMAddon::RunUint8Array(const Napi::CallbackInfo &Info) {
  if (!WBMode) {
    Napi::Error::New(Info.Env(), "RunInt function only supports WasmBindgen mode")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  uint32_t ResultMemAddr = 8;
  std::vector<SSVM::ValVariant> Args, Rets;
  Args.emplace_back(ResultMemAddr);
  PrepareResourceWB(Info, Args);
  auto Res = VM->execute(FuncName, Args);
  if (!Res) {
    Napi::Error::New(Info.Env(), "SSVM execution failed")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  Rets = *Res;

  uint32_t ResultDataAddr = 0;
  uint32_t ResultDataLen = 0;
  /// Retrieve address and length of result data
  if (auto ResultMem = MemInst->getBytes(ResultMemAddr, 8)) {
    ResultDataAddr = (*ResultMem)[0] | ((*ResultMem)[1] << 8) |
      ((*ResultMem)[2] << 16) | ((*ResultMem)[3] << 24);
    ResultDataLen = (*ResultMem)[4] | ((*ResultMem)[5] << 8) |
      ((*ResultMem)[6] << 16) | ((*ResultMem)[7] << 24);
  } else {
    Napi::Error::New(Info.Env(),
        "Access to forbidden memory address when retrieving address and length of result data")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  /// Get result data
  if (auto Res = MemInst->getBytes(ResultDataAddr, ResultDataLen)) {
    ResultData = std::vector<uint8_t>((*Res).begin(), (*Res).end());
    ReleaseResourceWB(ResultDataAddr, ResultDataLen);
  } else {
    Napi::Error::New(Info.Env(),
        "Access to forbidden memory address when retrieving result data")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }

  Napi::ArrayBuffer ResultArrayBuffer =
    Napi::ArrayBuffer::New(Info.Env(), &(ResultData[0]), ResultDataLen);
  Napi::Uint8Array ResultTypedArray = Napi::Uint8Array::New(
      Info.Env(), ResultDataLen, ResultArrayBuffer, 0, napi_uint8_array);
  return ResultTypedArray;
}

void SSVMAddon::EnableWasmBindgen(const Napi::CallbackInfo &Info) {
  Napi::Env Env = Info.Env();
  Napi::HandleScope Scope(Env);

  if ((IMode == InputMode::File && !(VM->loadWasm(InputPath)))
      || (IMode == InputMode::Bytecode && !(VM->loadWasm(InputBytecode)))) {
    Napi::Error::New(Env, "Wasm bytecode/file cannot be loaded correctly.")
      .ThrowAsJavaScriptException();
    return;
  }

  if (!(VM->validate())) {
    Napi::Error::New(Env, "Wasm bytecode/file failed at validation stage.")
      .ThrowAsJavaScriptException();
    return;
  }

  if (!(VM->instantiate())) {
    Napi::Error::New(Env, "Wasm bytecode/file cannot be instantiated.")
      .ThrowAsJavaScriptException();
    return;
  }

  // Get memory instance
  auto &Store = VM->getStoreManager();
  auto *ModInst = *(Store.getActiveModule());
  uint32_t MemAddr = *(ModInst->getMemAddr(0));
  MemInst = *Store.getMemory(MemAddr);
}

