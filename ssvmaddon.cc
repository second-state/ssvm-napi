#include "ssvmaddon.h"

#include "aot/compiler.h"
#include "loader/loader.h"
#include "support/filesystem.h"
#include "support/log.h"
#include "support/span.h"
#include "utils.h"

#include <limits>

#include <boost/functional/hash.hpp>

Napi::FunctionReference SSVMAddon::Constructor;

Napi::Object SSVMAddon::Init(Napi::Env Env, Napi::Object Exports) {
  Napi::HandleScope Scope(Env);

  SSVM::NAPI::checkLibCXXVersion();

  Napi::Function Func =
      DefineClass(Env, "VM",
                  {InstanceMethod("GetStatistics", &SSVMAddon::GetStatistics),
                   InstanceMethod("Start", &SSVMAddon::Start),
                   InstanceMethod("Run", &SSVMAddon::Run),
                   InstanceMethod("RunInt", &SSVMAddon::RunInt),
                   InstanceMethod("RunUInt", &SSVMAddon::RunUInt),
                   InstanceMethod("RunInt64", &SSVMAddon::RunInt64),
                   InstanceMethod("RunUInt64", &SSVMAddon::RunUInt64),
                   InstanceMethod("RunString", &SSVMAddon::RunString),
                   InstanceMethod("RunUint8Array", &SSVMAddon::RunUint8Array)});

  Constructor = Napi::Persistent(Func);
  Constructor.SuppressDestruct();

  Exports.Set("VM", Func);
  return Exports;
}

namespace {
inline bool checkInputWasmFormat(const Napi::CallbackInfo &Info) {
  return Info.Length() <= 0 || (!Info[0].IsString() && !Info[0].IsTypedArray());
}

inline bool isWasiOptionsProvided(const Napi::CallbackInfo &Info) {
  return Info.Length() == 2 && Info[1].IsObject();
}

inline uint32_t castFromU64ToU32(uint64_t V) {
  return static_cast<uint32_t>(
      V & static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()));
}

inline uint32_t castFromBytesToU32(const SSVM::Span<SSVM::Byte> &Bytes,
                                   int Idx) {
  return Bytes[Idx] | (Bytes[Idx + 1] << 8) | (Bytes[Idx + 2] << 16) |
         (Bytes[Idx + 3] << 24);
}

inline uint64_t castFromU32ToU64(uint32_t L, uint32_t H) {
  return static_cast<uint64_t>(L) | (static_cast<uint64_t>(H) << 32);
}

} // namespace

SSVMAddon::SSVMAddon(const Napi::CallbackInfo &Info)
    : Napi::ObjectWrap<SSVMAddon>(Info), Configure(nullptr), VM(nullptr),
      MemInst(nullptr), WasiMod(nullptr), Inited(false) {

  Napi::Env Env = Info.Env();
  Napi::HandleScope Scope(Env);

  if (checkInputWasmFormat(Info)) {
    napi_throw_error(Env, "Error",
                     "Expected a Wasm file or a Wasm binary sequence.");
    return;
  }

  // Assume the WasmBindgen is enabled by default.
  // Assume the WASI options object is {}

  // Check if a Wasi options object is given or not
  if (isWasiOptionsProvided(Info)) {
    // Get a WASI options object
    Napi::Object WasiOptions = Info[1].As<Napi::Object>();
    if (!Options.parse(WasiOptions)) {
      napi_throw_error(Env, "Error", "Parse Wasi options failed.");
      return;
    }
  }

  // Handle input wasm
  if (Info[0].IsString()) {
    // Wasm file path
    BC.setPath(std::move(Info[0].As<Napi::String>().Utf8Value()));
  } else if (Info[0].IsTypedArray() &&
             Info[0].As<Napi::TypedArray>().TypedArrayType() ==
                 napi_uint8_array) {
    // Wasm binary format
    Napi::ArrayBuffer DataBuffer = Info[0].As<Napi::TypedArray>().ArrayBuffer();
    BC.setData(std::move(std::vector<uint8_t>(
        static_cast<uint8_t *>(DataBuffer.Data()),
        static_cast<uint8_t *>(DataBuffer.Data()) + DataBuffer.ByteLength())));

    if (!BC.isValidData()) {
      napi_throw_error(Env, "Error", "Unknown bytecode format.");
      return;
    }
  } else {
    napi_throw_error(
        Env, "Error",
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

  /// Origin input can be Bytecode or FilePath
  if (Options.isAOTMode() && !BC.isCompiled()) {
    Compile();
    /// After Compile(), {Bytecode, FilePath} -> {FilePath}
  }

  if (Options.isReactorMode()) {
    LoadWasm(Info);
  }

  if (Options.isAOTMode()) {
    InitReactor(Info);
  }
}

bool SSVMAddon::Compile() {
  SSVM::Loader::Loader Loader;

  /// BC can be Bytecode or FilePath
  if (BC.isFile()) {
    /// File mode
    /// We have to load bytecode from given file first.
    std::filesystem::path P =
        std::filesystem::absolute(std::filesystem::path(BC.getPath()));
    if (auto Res = Loader.loadFile(P.string())) {
      BC.setData(std::move(*Res));
    } else {
      const auto Err = static_cast<uint32_t>(Res.error());
      std::cerr << "SSVM::NAPI::AOT::Load file failed. Error code: " << Err;
      return false;
    }
  }

  /// Now, BC must be Bytecode only
  /// Calculate hash and path.
  Cache.init(BC.getData());
  /// If the compiled bytecode existed, return directly.
  if (!Cache.isCached()) {
    /// Cache not found. Compile wasm bytecode
    std::unique_ptr<SSVM::AST::Module> Module;
    if (auto Res = Loader.parseModule(BC.getData())) {
      Module = std::move(*Res);
    } else {
      const auto Err = static_cast<uint32_t>(Res.error());
      std::cerr << "SSVM::NAPI::AOT::Parse module failed. Error code: " << Err;
      return false;
    }

    SSVM::AOT::Compiler Compiler;
    if (Options.isMeasuring()) {
      Compiler.setInstructionCounting();
      Compiler.setGasMeasuring();
    }
    if (auto Res = Compiler.compile(BC.getData(), *Module, Cache.getPath()); !Res) {
      const auto Err = static_cast<uint32_t>(Res.error());
      std::cerr << "SSVM::NAPI::AOT::Compile failed. Error code: " << Err;
      return false;
    }
  }

  /// After compiled Bytecode, the output will be written to a FilePath.
  BC.setPath(Cache.getPath());
  return true;
}

void SSVMAddon::PrepareResource(const Napi::CallbackInfo &Info,
                                std::vector<SSVM::ValVariant> &Args,
                                IntKind IntT) {
  for (std::size_t I = 1; I < Info.Length(); I++) {
    Napi::Value Arg = Info[I];
    uint32_t MallocSize = 0, MallocAddr = 0;
    if (Arg.IsNumber()) {
      switch (IntT) {
      case IntKind::SInt32:
      case IntKind::UInt32:
      case IntKind::Default:
        Args.emplace_back(Arg.As<Napi::Number>().Uint32Value());
        break;
      case IntKind::SInt64:
      case IntKind::UInt64: {
        if (Args.size() == 0) {
          // Set memory offset for return value
          Args.emplace_back<uint32_t>(0);
        }
        uint64_t V = static_cast<uint64_t>(Arg.As<Napi::Number>().Int64Value());
        Args.emplace_back(castFromU64ToU32(V));
        Args.emplace_back(castFromU64ToU32(V >> 32));
        break;
      }
      default:
        napi_throw_error(
            Info.Env(), "Error",
            "SSVM-Napi implementation error: unknown integer type");
        return;
      }
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
      napi_throw_error(Info.Env(), "Error", "unsupported argument type");
      return;
    }

    // Malloc
    std::vector<SSVM::ValVariant> Params, Rets;
    Params.emplace_back(MallocSize);
    auto Res = VM->execute("__wbindgen_malloc", Params);
    if (!Res) {
      std::string MallocError("SSVMAddon.cc::PrepareResource::__wbindgen_malloc"
                              "SSVM-js malloc failed: wasm-bindgen helper "
                              "function <__wbindgen_malloc> not found.\n");
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

void SSVMAddon::PrepareResource(const Napi::CallbackInfo &Info,
                                std::vector<SSVM::ValVariant> &Args) {
  PrepareResource(Info, Args, IntKind::Default);
}

void SSVMAddon::ReleaseResource(const Napi::CallbackInfo &Info,
                                const uint32_t Offset, const uint32_t Size) {
  std::vector<SSVM::ValVariant> Params = {Offset, Size};
  auto Res = VM->execute("__wbindgen_free", Params);
  if (!Res) {
    std::string FreeError("SSVMAddon.cc::ReleaseResource::__wbindgen_free"
                          "SSVM-js free failed: wasm-bindgen helper function "
                          "<__wbindgen_free> not found.\n");
    napi_throw_error(Info.Env(), "Error", FreeError.c_str());
    return;
  }
}

Napi::Value SSVMAddon::Start(const Napi::CallbackInfo &Info) {
  InitVM(Info);

  std::string FuncName = "_start";
  std::vector<std::string> MainCmdArgs = Options.getWasiCmdArgs();
  MainCmdArgs.erase(MainCmdArgs.begin(), MainCmdArgs.begin() + 2);
  WasiMod->getEnv().init(Options.getWasiDirs(), FuncName, MainCmdArgs,
                         Options.getWasiEnvs());

  // command mode
  auto Result = VM->runWasmFile(BC.getPath(), "_start");
  if (!Result) {
    Napi::Error::New(Info.Env(), "SSVM execution failed")
        .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  auto ErrCode = WasiMod->getEnv().getExitCode();
  WasiMod->getEnv().fini();
  return Napi::Number::New(Info.Env(), ErrCode);
}

void SSVMAddon::InitReactor(const Napi::CallbackInfo &Info) {
  using namespace std::literals::string_literals;
  const auto InitFunc = "_initialize"s;

  bool HasInit = false;

  for (const auto &Func : VM->getFunctionList()) {
    if (Func.first == InitFunc) {
      HasInit = true;
    }
  }

  if (HasInit) {
    if (auto Result = VM->execute(InitFunc); !Result) {
      napi_throw_error(Info.Env(), "Error", "SSVM cannot initialize wasi env");
    }
  }
}

void SSVMAddon::Run(const Napi::CallbackInfo &Info) {
  InitVM(Info);

  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  WasiMod->getEnv().init(Options.getWasiDirs(), FuncName,
                         Options.getWasiCmdArgs(), Options.getWasiEnvs());

  std::vector<SSVM::ValVariant> Args, Rets;
  PrepareResource(Info, Args);
  auto Res = VM->execute(FuncName, Args);

  if (!Res) {
    napi_throw_error(Info.Env(), "Error", "SSVM execution failed");
  }

  WasiMod->getEnv().fini();
}

Napi::Value SSVMAddon::RunIntImpl(const Napi::CallbackInfo &Info,
                                  IntKind IntT) {
  InitVM(Info);
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  WasiMod->getEnv().init(Options.getWasiDirs(), FuncName,
                         Options.getWasiCmdArgs(), Options.getWasiEnvs());

  std::vector<SSVM::ValVariant> Args, Rets;
  PrepareResource(Info, Args, IntT);
  auto Res = VM->execute(FuncName, Args);

  if (Res) {
    Rets = *Res;
    WasiMod->getEnv().fini();
    switch (IntT) {
    case IntKind::SInt32:
    case IntKind::UInt32:
    case IntKind::Default:
      return Napi::Number::New(Info.Env(), std::get<uint32_t>(Rets[0]));
    case IntKind::SInt64:
    case IntKind::UInt64:
      if (auto ResMem = MemInst->getBytes(0, 8)) {
        uint32_t L = castFromBytesToU32(*ResMem, 0);
        uint32_t H = castFromBytesToU32(*ResMem, 4);
        return Napi::Number::New(Info.Env(), castFromU32ToU64(L, H));
      }
      [[fallthrough]];
    default:
      napi_throw_error(Info.Env(), "Error",
                       "SSVM-Napi implementation error: unknown integer type");
      return Napi::Value();
    }
  } else {
    napi_throw_error(Info.Env(), "Error", "SSVM execution failed");
    return Napi::Value();
  }
}

Napi::Value SSVMAddon::RunInt(const Napi::CallbackInfo &Info) {
  return RunIntImpl(Info, IntKind::SInt32);
}

Napi::Value SSVMAddon::RunUInt(const Napi::CallbackInfo &Info) {
  return RunIntImpl(Info, IntKind::UInt32);
}

Napi::Value SSVMAddon::RunInt64(const Napi::CallbackInfo &Info) {
  return RunIntImpl(Info, IntKind::SInt64);
}

Napi::Value SSVMAddon::RunUInt64(const Napi::CallbackInfo &Info) {
  return RunIntImpl(Info, IntKind::UInt64);
}

Napi::Value SSVMAddon::RunString(const Napi::CallbackInfo &Info) {
  InitVM(Info);
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  WasiMod->getEnv().init(Options.getWasiDirs(), FuncName,
                         Options.getWasiCmdArgs(), Options.getWasiEnvs());

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
    napi_throw_error(Info.Env(), "Error",
                     "Access to forbidden memory address when retrieving "
                     "address and length of result data");
    return Napi::Value();
  }

  std::vector<uint8_t> ResultData;
  if (auto Res = MemInst->getBytes(ResultDataAddr, ResultDataLen)) {
    ResultData = std::vector<uint8_t>((*Res).begin(), (*Res).end());
    ReleaseResource(Info, ResultDataAddr, ResultDataLen);
  } else {
    napi_throw_error(
        Info.Env(), "Error",
        "Access to forbidden memory address when retrieving result data");
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

  WasiMod->getEnv().init(Options.getWasiDirs(), FuncName,
                         Options.getWasiCmdArgs(), Options.getWasiEnvs());

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
    napi_throw_error(Info.Env(), "Error",
                     "Access to forbidden memory address when retrieving "
                     "address and length of result data");
    return Napi::Value();
  }
  /// Get result data
  std::vector<uint8_t> ResultData;
  if (auto Res = MemInst->getBytes(ResultDataAddr, ResultDataLen)) {
    ResultData = std::vector<uint8_t>((*Res).begin(), (*Res).end());
    ReleaseResource(Info, ResultDataAddr, ResultDataLen);
  } else {
    napi_throw_error(
        Info.Env(), "Error",
        "Access to forbidden memory address when retrieving result data");
    return Napi::Value();
  }

  Napi::ArrayBuffer ResultArrayBuffer =
      Napi::ArrayBuffer::New(Info.Env(), &(ResultData[0]), ResultDataLen);
  Napi::Uint8Array ResultTypedArray = Napi::Uint8Array::New(
      Info.Env(), ResultDataLen, ResultArrayBuffer, 0, napi_uint8_array);
  WasiMod->getEnv().fini();
  return ResultTypedArray;
}

void SSVMAddon::LoadWasm(const Napi::CallbackInfo &Info) {
  Napi::Env Env = Info.Env();
  Napi::HandleScope Scope(Env);

  if (BC.isFile()) {
    if (!VM->loadWasm(BC.getPath())) {
      napi_throw_error(Info.Env(), "Error",
                       "Wasm bytecode/file cannot be loaded correctly.");
      return;
    }
  } else if (BC.isValidData()) {
    if (Options.isAOTMode() && !(VM->loadWasm(BC.getPath()))) {
      napi_throw_error(Info.Env(), "Error",
                       "Wasm bytecode/file cannot be loaded correctly.");
      return;
    } else if (!Options.isAOTMode() && !(VM->loadWasm(BC.getData()))) {
      napi_throw_error(Info.Env(), "Error",
                       "Wasm bytecode/file cannot be loaded correctly.");
      return;
    }
  }

  if (!(VM->validate())) {
    napi_throw_error(Info.Env(), "Error",
                     "Wasm bytecode/file failed at validation stage.");
    return;
  }

  if (!(VM->instantiate())) {
    napi_throw_error(Info.Env(), "Error",
                     "Wasm bytecode/file cannot be instantiated.");
    return;
  }

  // Get memory instance
  auto &Store = VM->getStoreManager();
  auto *ModInst = *(Store.getActiveModule());
  uint32_t MemAddr = *(ModInst->getMemAddr(0));
  MemInst = *Store.getMemory(MemAddr);
}

Napi::Value SSVMAddon::GetStatistics(const Napi::CallbackInfo &Info) {
  Napi::Object RetStat = Napi::Object::New(Info.Env());
  if (!Options.isMeasuring()) {
    RetStat.Set("Measure", Napi::Boolean::New(Info.Env(), false));
  } else {
    Stat = VM->getStatistics();

    RetStat.Set("Measure", Napi::Boolean::New(Info.Env(), true));
    RetStat.Set("TotalExecutionTime",
                Napi::Number::New(Info.Env(), Stat.getTotalExecTime()));
    RetStat.Set("WasmExecutionTime",
                Napi::Number::New(Info.Env(), Stat.getWasmExecTime()));
    RetStat.Set("HostFunctionExecutionTime",
                Napi::Number::New(Info.Env(), Stat.getHostFuncExecTime()));
    RetStat.Set("InstructionCount",
                Napi::Number::New(Info.Env(), Stat.getInstrCount()));
    RetStat.Set("TotalGasCost",
                Napi::Number::New(Info.Env(), Stat.getTotalGasCost()));
    RetStat.Set("InstructionPerSecond",
                Napi::Number::New(Info.Env(), Stat.getInstrPerSecond()));
  }

  return RetStat;
}
