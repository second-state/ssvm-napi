#include "ssvmaddon.h"

#include "loader/loader.h"
#include "support/filesystem.h"
#include "support/log.h"
#include "support/span.h"

Napi::FunctionReference SSVMAddon::Constructor;

Napi::Object SSVMAddon::Init(Napi::Env Env, Napi::Object Exports) {
  Napi::HandleScope Scope(Env);

#ifdef __GLIBCXX__
#ifdef __aarch64__
  std::string GLibPath = "/usr/lib/aarch64-linux-gnu";
#endif
#ifdef __x86_64__
  std::string GLibPath = "/usr/lib/x86_64-linux-gnu";
#endif
  std::string GLibCXXName = "libstdc++.so.6.0.";
  std::string CurrentGLibVer = "unknown";
  bool IsVersionCompatible = false;
  for (const auto & Entry : std::filesystem::directory_iterator(GLibPath)) {
    std::string LibName = Entry.path().filename().string();
    size_t Pos = LibName.find(GLibCXXName);
    if (Pos != std::string::npos) {
      CurrentGLibVer = LibName;
      std::string GV = LibName.substr(GLibCXXName.length(), LibName.length());
      if (std::stoi(GV) >= 28) {
        IsVersionCompatible = true;
      }
    }
  }
  if (!IsVersionCompatible) {
    std::cerr << "====================================================================\n"
      << "Error: libstdc++ version mismatched!\n"
      << "Your current version is " << CurrentGLibVer << " which is less than libstdc++6.0.28\n"
      << "SSVM relies on >=libstdc++6.0.28 (GLIBCXX >= 3.4.28)\n"
      << "Please upgrade the libstdc++6 library.\n\n"
      << "For more details, refer to our environment set up document: https://www.secondstate.io/articles/setup-rust-nodejs/\n"
      << "====================================================================\n";
  }
#endif

  Napi::Function Func =
    DefineClass(Env, "VM", {
        InstanceMethod("GetStatistics", &SSVMAddon::GetStatistics),
        InstanceMethod("Start", &SSVMAddon::Start),
        InstanceMethod("Run", &SSVMAddon::Run),
        InstanceMethod("RunInt", &SSVMAddon::RunInt),
        InstanceMethod("RunString", &SSVMAddon::RunString),
        InstanceMethod("RunUint8Array", &SSVMAddon::RunUint8Array)});

  Constructor = Napi::Persistent(Func);
  Constructor.SuppressDestruct();

  Exports.Set("VM", Func);
  return Exports;
}

namespace {
bool isWasm(const std::vector<uint8_t> &Bytecode) {
  if (Bytecode[0] == 0x00 &&
      Bytecode[1] == 0x61 &&
      Bytecode[2] == 0x73 &&
      Bytecode[3] == 0x6d) {
    return true;
  }
  return false;
}

inline bool checkInputWasmFormat(const Napi::CallbackInfo &Info) {
  return Info.Length() <= 0 || (!Info[0].IsString() && !Info[0].IsTypedArray());
}

inline bool isWasiOptionsProvided(const Napi::CallbackInfo &Info) {
  return Info.Length() == 2 && Info[1].IsObject();
}
} // namespace details

SSVMAddon::SSVMAddon(const Napi::CallbackInfo &Info)
  : Napi::ObjectWrap<SSVMAddon>(Info), Configure(nullptr),
  VM(nullptr), MemInst(nullptr),
  WasiMod(nullptr), Inited(false), EnableWasiStart(false) {

    Napi::Env Env = Info.Env();
    Napi::HandleScope Scope(Env);

    if (checkInputWasmFormat(Info)) {
      napi_throw_error(
          Env,
          "Error",
          "Expected a Wasm file or a Wasm binary sequence.");
      return;
    }

    // Assume the WasmBindgen is enabled by default.
    // Assume the WASI options object is {}

    // Check if a Wasi options object is given or not
    if (isWasiOptionsProvided(Info)) {
      // Get a WASI options object
      Napi::Object WasiOptions = Info[1].As<Napi::Object>();
      if (!parseCmdArgs(WasiCmdArgs, WasiOptions)) {
        napi_throw_error(
            Env,
            "Error",
            "Parse commandline arguments from Wasi options failed.");
        return;
      }
      if (!parseDirs(WasiDirs, WasiOptions)) {
        napi_throw_error(
            Env,
            "Error",
            "Parse preopens from Wasi options failed.");
        return;
      }
      if (!parseEnvs(WasiEnvs, WasiOptions)) {
        napi_throw_error(
            Env,
            "Error",
            "Parse environment variables from Wasi options failed.");
        return;
      }
      EnableWasiStart = parseWasiStartFlag(WasiOptions);
    }

    // Handle input wasm
    if (Info[0].IsString()) {
      // Wasm file path
      IMode = InputMode::FilePath;
      InputPath = Info[0].As<Napi::String>().Utf8Value();
    } else if (Info[0].IsTypedArray()
        && Info[0].As<Napi::TypedArray>().TypedArrayType() == napi_uint8_array) {
      // Wasm binary format
      Napi::ArrayBuffer DataBuffer = Info[0].As<Napi::TypedArray>().ArrayBuffer();
      InputBytecode = std::vector<uint8_t>(
          static_cast<uint8_t*>(DataBuffer.Data()),
          static_cast<uint8_t*>(DataBuffer.Data()) + DataBuffer.ByteLength());

      if (isWasm(InputBytecode)) {
        IMode = InputMode::WasmBytecode;
      } else {
        napi_throw_error(
            Env,
            "Error",
            "Unknown bytecode format.");
        return;
      }
    } else {
      napi_throw_error(
          Env,
          "Error",
          "Wasm bytecode is not a valid Uint8Array or not a valid file path.");
      return;
    }
  }

void SSVMAddon::InitVM(const Napi::CallbackInfo &Info) {
  if (Inited) {
    return;
  }
  Inited = true;

  Configure = new SSVM::VM::Configure();
  Configure->addVMType(SSVM::VM::Configure::VMType::Wasi);
  Configure->addVMType(SSVM::VM::Configure::VMType::SSVM_Process);
  VM = new SSVM::VM::VM(*Configure);

  SSVM::Log::setErrorLoggingLevel();

  WasiMod = dynamic_cast<SSVM::Host::WasiModule *>(
      VM->getImportModule(SSVM::VM::Configure::VMType::Wasi));

  if (!EnableWasiStart) {
    EnableWasmBindgen(Info);
  }
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
      napi_throw_error(
          Info.Env(),
          "Error",
          "unsupported argument type");
      return;
    }

    // Malloc
    std::vector<SSVM::ValVariant> Params, Rets;
    Params.emplace_back(MallocSize);
    auto Res = VM->execute("__wbindgen_malloc", Params);
    if (!Res) {
      std::string MallocError(
          "SSVMAddon.cc::PrepareResource::__wbindgen_malloc"
          "SSVM-js malloc failed: wasm-bindgen helper function <__wbindgen_malloc> not found.\n");
      napi_throw_error(Info.Env(), "Error", MallocError.c_str());
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

void SSVMAddon::ReleaseResource(const Napi::CallbackInfo &Info, const uint32_t Offset, const uint32_t Size) {
  std::vector<SSVM::ValVariant> Params = {Offset, Size};
  auto Res = VM->execute("__wbindgen_free", Params);
  if (!Res) {
    std::string FreeError(
        "SSVMAddon.cc::ReleaseResource::__wbindgen_free"
        "SSVM-js free failed: wasm-bindgen helper function <__wbindgen_free> not found.\n");
    napi_throw_error(Info.Env(), "Error", FreeError.c_str());
    return;
  }
}

bool SSVMAddon::parseWasiStartFlag(const Napi::Object &WasiOptions) {
  if (WasiOptions.Has("EnableWasiStartFunction")
      && WasiOptions.Get("EnableWasiStartFunction").IsBoolean()) {
    return WasiOptions.Get("EnableWasiStartFunction").As<Napi::Boolean>().Value();
  }
  return false;
}

bool SSVMAddon::parseCmdArgs(
    std::vector<std::string> &CmdArgs,
    const Napi::Object &WasiOptions) {
  CmdArgs.clear();
  if (WasiOptions.Has("args")
      && WasiOptions.Get("args").IsArray()) {
    Napi::Array Args = WasiOptions.Get("args").As<Napi::Array>();
    for (uint32_t i = 0; i < Args.Length(); i++) {
      Napi::Value Arg = Args[i];
      if (Arg.IsNumber()) {
        CmdArgs.push_back(std::to_string(Arg.As<Napi::Number>().Uint32Value()));
      } else if (Arg.IsString()) {
        CmdArgs.push_back(Arg.As<Napi::String>().Utf8Value());
      } else if (Arg.IsTypedArray() &&
          Arg.As<Napi::TypedArray>().TypedArrayType() ==
          napi_uint8_array) {
        Napi::ArrayBuffer DataBuffer = Arg.As<Napi::TypedArray>().ArrayBuffer();
        std::string ArrayArg = std::string(
            static_cast<char*>(DataBuffer.Data()),
            static_cast<char*>(DataBuffer.Data()) + DataBuffer.ByteLength());
        CmdArgs.push_back(ArrayArg);
      } else {
        // TODO: support other types
        return false;
      }
    }
  }
  return true;
}

bool SSVMAddon::parseDirs(
    std::vector<std::string> &Dirs,
    const Napi::Object &WasiOptions) {
  Dirs.clear();
  if (WasiOptions.Has("preopens")
      && WasiOptions.Get("preopens").IsObject()) {
    Napi::Object Preopens = WasiOptions.Get("preopens").As<Napi::Object>();
    Napi::Array Keys = Preopens.GetPropertyNames();
    for (uint32_t i = 0; i < Keys.Length(); i++) {
      // Dir format: <guest_path>:<host_path>
      Napi::Value Key = Keys[i];
      if (!Key.IsString()) {
        // host path must be a string
        return false;
      }
      std::string Dir = Key.As<Napi::String>().Utf8Value();
      Dir.append(":");
      Napi::Value Value = Preopens.Get(Key);
      if (!Value.IsString()) {
        // guest path must be a string
        return false;
      }
      Dir.append(Value.As<Napi::String>().Utf8Value());
      Dirs.push_back(Dir);
    }
  }
  return true;
}

bool SSVMAddon::parseEnvs(
    std::vector<std::string> &Envs,
    const Napi::Object &WasiOptions) {
  Envs.clear();
  if (WasiOptions.Has("env")
      && WasiOptions.Get("env").IsObject()) {
    Napi::Object Environs = WasiOptions.Get("env").As<Napi::Object>();
    Napi::Array Keys = Environs.GetPropertyNames();
    for (uint32_t i = 0; i < Keys.Length(); i++) {
      // Environ format: <KEY>=<VALUE>
      Napi::Value Key = Keys[i];
      if (!Key.IsString()) {
        // key must be a string
        return false;
      }
      std::string Env = Key.As<Napi::String>().Utf8Value();
      Env.append("=");
      Napi::Value Value = Environs.Get(Key);
      if (!Value.IsString()) {
        // value must be a string
        return false;
      }
      Env.append(Value.As<Napi::String>().Utf8Value());
      Envs.push_back(Env);
    }
  }
  return true;
}

Napi::Value SSVMAddon::Start(const Napi::CallbackInfo &Info) {
  InitVM(Info);
  std::string FuncName = "_start";
  std::vector<std::string> MainCmdArgs = WasiCmdArgs;
  MainCmdArgs.erase(MainCmdArgs.begin(), MainCmdArgs.begin()+2);
  WasiMod->getEnv().init(WasiDirs, FuncName, MainCmdArgs, WasiEnvs);

  SSVM::Expect<std::vector<SSVM::ValVariant>> Res;
  if (IMode == InputMode::FilePath) {
    Res = VM->runWasmFile(InputPath, FuncName);
  } else {
    Res = VM->runWasmFile(InputBytecode, FuncName);
  }
  if (!Res) {
    Napi::Error::New(Info.Env(), "SSVM execution failed")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  WasiMod->getEnv().fini();
  return Napi::Number::New(Info.Env(), 0);
}

void SSVMAddon::Run(const Napi::CallbackInfo &Info) {
  InitVM(Info);
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  WasiMod->getEnv().init(WasiDirs, FuncName, WasiCmdArgs, WasiEnvs);

  std::vector<SSVM::ValVariant> Args, Rets;
  PrepareResource(Info, Args);
  auto Res = VM->execute(FuncName, Args);

  if (!Res) {
    napi_throw_error(Info.Env(), "Error", "SSVM execution failed");
  }

  WasiMod->getEnv().fini();
}

Napi::Value SSVMAddon::RunInt(const Napi::CallbackInfo &Info) {
  InitVM(Info);
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  WasiMod->getEnv().init(WasiDirs, FuncName, WasiCmdArgs, WasiEnvs);

  std::vector<SSVM::ValVariant> Args, Rets;
  PrepareResource(Info, Args);
  auto Res = VM->execute(FuncName, Args);

  if (Res) {
    Rets = *Res;
    WasiMod->getEnv().fini();
    return Napi::Number::New(Info.Env(), std::get<uint32_t>(Rets[0]));
  } else {
    napi_throw_error(Info.Env(), "Error", "SSVM execution failed");
    return Napi::Value();
  }
}

Napi::Value SSVMAddon::RunString(const Napi::CallbackInfo &Info) {
  InitVM(Info);
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  WasiMod->getEnv().init(WasiDirs, FuncName, WasiCmdArgs, WasiEnvs);

  uint32_t ResultMemAddr = 8;
  std::vector<SSVM::ValVariant> Args, Rets;
  Args.emplace_back(ResultMemAddr);
  PrepareResource(Info, Args);
  auto Res = VM->execute(FuncName, Args);
  if (!Res) {
    napi_throw_error(Info.Env(), "Error", "SSVM execution failed");
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
    napi_throw_error(Info.Env(), "Error", "Access to forbidden memory address when retrieving address and length of result data");
    return Napi::Value();
  }
  if (auto Res = MemInst->getBytes(ResultDataAddr, ResultDataLen)) {
    ResultData = std::vector<uint8_t>((*Res).begin(), (*Res).end());
    ReleaseResource(Info, ResultDataAddr, ResultDataLen);
  } else {
    napi_throw_error(Info.Env(), "Error", "Access to forbidden memory address when retrieving result data");
    return Napi::Value();
  }

  std::string ResultString(ResultData.begin(), ResultData.end());
  WasiMod->getEnv().fini();
  return Napi::String::New(Info.Env(), ResultString);
}

Napi::Value SSVMAddon::RunUint8Array(const Napi::CallbackInfo &Info) {
  InitVM(Info);
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  WasiMod->getEnv().init(WasiDirs, FuncName, WasiCmdArgs, WasiEnvs);

  uint32_t ResultMemAddr = 8;
  std::vector<SSVM::ValVariant> Args, Rets;
  Args.emplace_back(ResultMemAddr);
  PrepareResource(Info, Args);
  auto Res = VM->execute(FuncName, Args);
  if (!Res) {
    napi_throw_error(Info.Env(), "Error", "SSVM execution failed");
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
    napi_throw_error(Info.Env(), "Error", "Access to forbidden memory address when retrieving address and length of result data");
    return Napi::Value();
  }
  /// Get result data
  if (auto Res = MemInst->getBytes(ResultDataAddr, ResultDataLen)) {
    ResultData = std::vector<uint8_t>((*Res).begin(), (*Res).end());
    ReleaseResource(Info, ResultDataAddr, ResultDataLen);
  } else {
    napi_throw_error(Info.Env(), "Error", "Access to forbidden memory address when retrieving result data");
    return Napi::Value();
  }

  Napi::ArrayBuffer ResultArrayBuffer =
    Napi::ArrayBuffer::New(Info.Env(), &(ResultData[0]), ResultDataLen);
  Napi::Uint8Array ResultTypedArray = Napi::Uint8Array::New(
      Info.Env(), ResultDataLen, ResultArrayBuffer, 0, napi_uint8_array);
  WasiMod->getEnv().fini();
  return ResultTypedArray;
}

void SSVMAddon::EnableWasmBindgen(const Napi::CallbackInfo &Info) {
  Napi::Env Env = Info.Env();
  Napi::HandleScope Scope(Env);

  if ((IMode == InputMode::FilePath && !(VM->loadWasm(InputPath)))
      || (IMode == InputMode::WasmBytecode && !(VM->loadWasm(InputBytecode)))) {
    napi_throw_error(Info.Env(), "Error", "Wasm bytecode/file cannot be loaded correctly.");
    return;
  }

  if (!(VM->validate())) {
    napi_throw_error(Info.Env(), "Error", "Wasm bytecode/file failed at validation stage.");
    return;
  }

  if (!(VM->instantiate())) {
    napi_throw_error(Info.Env(), "Error", "Wasm bytecode/file cannot be instantiated.");
    return;
  }

  // Get memory instance
  auto &Store = VM->getStoreManager();
  auto *ModInst = *(Store.getActiveModule());
  uint32_t MemAddr = *(ModInst->getMemAddr(0));
  MemInst = *Store.getMemory(MemAddr);
}

Napi::Value SSVMAddon::GetStatistics(const Napi::CallbackInfo &Info) {
  Stat = VM->getStatistics();
  Napi::Object RetStat = Napi::Object::New(Info.Env());

  RetStat.Set("TotalExecutionTime", Napi::Number::New(Info.Env(), Stat.getTotalExecTime()));
  RetStat.Set("WasmExecutionTime", Napi::Number::New(Info.Env(), Stat.getWasmExecTime()));
  RetStat.Set("HostFunctionExecutionTime", Napi::Number::New(Info.Env(), Stat.getHostFuncExecTime()));
  RetStat.Set("InstructionCount", Napi::Number::New(Info.Env(), Stat.getInstrCount()));
  RetStat.Set("TotalGasCost", Napi::Number::New(Info.Env(), Stat.getTotalGasCost()));
  RetStat.Set("InstructionPerSecond", Napi::Number::New(Info.Env(), Stat.getInstrPerSecond()));

  return RetStat;
}
