#include "wasmedgeaddon.h"

#include <limits>
#include <wasmedge.h>

#include <boost/functional/hash.hpp>
#include <iostream>

Napi::FunctionReference WasmEdgeAddon::Constructor;

Napi::Object WasmEdgeAddon::Init(Napi::Env Env, Napi::Object Exports) {
  Napi::HandleScope Scope(Env);

  WASMEDGE::NAPI::checkLibCXXVersion();

  Napi::Function Func = DefineClass(
      Env, "VM",
      {InstanceMethod("GetStatistics", &WasmEdgeAddon::GetStatistics),
       InstanceMethod("Start", &WasmEdgeAddon::RunStart),
       InstanceMethod("Compile", &WasmEdgeAddon::RunCompile),
       InstanceMethod("Run", &WasmEdgeAddon::Run),
       InstanceMethod("RunInt", &WasmEdgeAddon::RunInt),
       InstanceMethod("RunUInt", &WasmEdgeAddon::RunUInt),
       InstanceMethod("RunInt64", &WasmEdgeAddon::RunInt64),
       InstanceMethod("RunUInt64", &WasmEdgeAddon::RunUInt64),
       InstanceMethod("RunString", &WasmEdgeAddon::RunString),
       InstanceMethod("RunUint8Array", &WasmEdgeAddon::RunUint8Array)});

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

inline uint32_t castFromBytesToU32(const uint8_t *bytes, int Idx) {
  return bytes[Idx] | (bytes[Idx + 1] << 8) | (bytes[Idx + 2] << 16) |
         (bytes[Idx + 3] << 24);
}

inline uint64_t castFromU32ToU64(uint32_t L, uint32_t H) {
  return static_cast<uint64_t>(L) | (static_cast<uint64_t>(H) << 32);
}

inline bool endsWith(const std::string &S, const std::string &Suffix) {
  return S.length() >= Suffix.length() &&
         S.compare(S.length() - Suffix.length(), std::string::npos, Suffix) ==
             0;
}

} // namespace

WasmEdgeAddon::WasmEdgeAddon(const Napi::CallbackInfo &Info)
    : Napi::ObjectWrap<WasmEdgeAddon>(Info), Configure(nullptr), VM(nullptr),
      MemInst(nullptr), WasiMod(nullptr), Inited(false) {
  Napi::Env Env = Info.Env();
  Napi::HandleScope Scope(Env);

  if (checkInputWasmFormat(Info)) {
    napi_throw_error(
        Info.Env(), "Error",
        WASMEDGE::NAPI::ErrorMsgs.at(ErrorType::ExpectWasmFileOrBytecode)
            .c_str());
    return;
  }

  // Assume the WasmBindgen is enabled by default.
  // Assume the WASI options object is {}

  // Check if a Wasi options object is given or not
  if (isWasiOptionsProvided(Info)) {
    // Get a WASI options object
    Napi::Object WasiOptions = Info[1].As<Napi::Object>();
    if (!Options.parse(WasiOptions)) {
      napi_throw_error(
          Info.Env(), "Error",
          WASMEDGE::NAPI::ErrorMsgs.at(ErrorType::ParseOptionsFailed).c_str());
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
    size_t Length = Info[0].As<Napi::TypedArray>().ElementLength();
    size_t Offset = Info[0].As<Napi::TypedArray>().ByteOffset();
    // Wasm binary format
    Napi::ArrayBuffer DataBuffer = Info[0].As<Napi::TypedArray>().ArrayBuffer();
    BC.setData(std::move(std::vector<uint8_t>(
        static_cast<uint8_t *>(DataBuffer.Data()) + Offset,
        static_cast<uint8_t *>(DataBuffer.Data()) + Offset + Length)));

    if (!BC.isValidData()) {
      napi_throw_error(
          Info.Env(), "Error",
          WASMEDGE::NAPI::ErrorMsgs.at(ErrorType::UnknownBytecodeFormat)
              .c_str());
      return;
    }
  } else {
    napi_throw_error(
        Info.Env(), "Error",
        WASMEDGE::NAPI::ErrorMsgs.at(ErrorType::InvalidInputFormat).c_str());
    return;
  }
}

void WasmEdgeAddon::InitVM(const Napi::CallbackInfo &Info) {
  if (Inited) {
    return;
  }

  Store = WasmEdge_StoreCreate();
  Configure = WasmEdge_ConfigureCreate();
  Stat = WasmEdge_StatisticsCreate();
  WasmEdge_ConfigureAddProposal(Configure,
                                WasmEdge_Proposal_BulkMemoryOperations);
  WasmEdge_ConfigureAddProposal(Configure, WasmEdge_Proposal_ReferenceTypes);
  WasmEdge_ConfigureAddProposal(Configure, WasmEdge_Proposal_SIMD);
  WasmEdge_ConfigureAddHostRegistration(Configure,
                                        WasmEdge_HostRegistration_Wasi);
  WasmEdge_ConfigureAddHostRegistration(
      Configure, WasmEdge_HostRegistration_WasmEdge_Process);
  VM = WasmEdge_VMCreate(Configure, Store);

  WasmEdge_LogSetErrorLevel();

  WasmEdge_ImportObjectContext *ProcObject = WasmEdge_VMGetImportModuleContext(
      VM, WasmEdge_HostRegistration_WasmEdge_Process);
  std::vector<const char *> AllowCmds;
  AllowCmds.reserve(Options.getAllowedCmds().size());
  for (auto &cmd : Options.getAllowedCmds()) {
    AllowCmds.push_back(cmd.c_str());
  }
  WasmEdge_ImportObjectInitWasmEdgeProcess(ProcObject, AllowCmds.data(),
                                           AllowCmds.size(),
                                           Options.isAllowedCmdsAll());

  Inited = true;
}

void WasmEdgeAddon::FiniVM() {
  if (!Inited) {
    return;
  }

  Stat = WasmEdge_VMGetStatisticsContext(VM);
  WasmEdge_VMDelete(VM);
  VM = nullptr;
  WasmEdge_StoreDelete(Store);
  Store = nullptr;
  WasmEdge_ConfigureDelete(Configure);
  Configure = nullptr;
  MemInst = nullptr;
  WasiMod = nullptr;

  Inited = false;
}

void WasmEdgeAddon::InitWasi(const Napi::CallbackInfo &Info,
                             const std::string &FuncName) {
  WasiMod =
      WasmEdge_VMGetImportModuleContext(VM, WasmEdge_HostRegistration_Wasi);

  /// Origin input can be Bytecode or FilePath
  if (Options.isAOTMode()) {
    if (BC.isFile() && endsWith(BC.getPath(), ".so")) {
      // BC is already the compiled filename, do nothing
    } else if (!BC.isCompiled()) {
      Compile();
    }
    /// After Compile(), {Bytecode, FilePath} -> {FilePath}
  }

  if (Options.isReactorMode()) {
    LoadWasm(Info);
  }

  std::vector<const char *> WasiCmdArgs;
  WasiCmdArgs.reserve(Options.getWasiCmdArgs().size());
  for (auto &cmd : Options.getWasiCmdArgs()) {
    WasiCmdArgs.push_back(cmd.c_str());
  }
  std::vector<const char *> WasiEnvs;
  WasiEnvs.reserve(Options.getWasiEnvs().size());
  for (auto &env : Options.getWasiEnvs()) {
    WasiEnvs.push_back(env.c_str());
  }
  std::vector<const char *> WasiDirs;
  WasiDirs.reserve(Options.getWasiDirs().size());
  for (auto &dir : Options.getWasiDirs()) {
    WasiDirs.push_back(dir.c_str());
  }
  WasmEdge_ImportObjectInitWASI(WasiMod, WasiCmdArgs.data(), WasiCmdArgs.size(),
                                WasiEnvs.data(), WasiEnvs.size(),
                                WasiDirs.data(), WasiDirs.size(), nullptr, 0);

  if (Options.isAOTMode()) {
    InitReactor(Info);
  }
}

void WasmEdgeAddon::ThrowNapiError(const Napi::CallbackInfo &Info,
                                   ErrorType Type) {
  FiniVM();
  napi_throw_error(Info.Env(), "Error",
                   WASMEDGE::NAPI::ErrorMsgs.at(Type).c_str());
}

bool WasmEdgeAddon::Compile() {
  /// Calculate hash and path.
  Cache.init(BC.getData());

  /// If the compiled bytecode existed, return directly.
  if (!Cache.isCached()) {
    /// Cache not found. Compile wasm bytecode
    if (auto Res = CompileBytecodeTo(Cache.getPath()); !Res) {
      return false;
    }
  }

  /// After compiled Bytecode, the output will be written to a FilePath.
  BC.setPath(Cache.getPath());
  return true;
}

bool WasmEdgeAddon::CompileBytecodeTo(const std::string &Path) {
  /// Make sure BC is in FilePath mode
  BC.setFileMode();

  if (Options.isMeasuring()) {
    WasmEdge_ConfigureCompilerSetCostMeasuring(Configure, true);
    WasmEdge_ConfigureCompilerSetInstructionCounting(Configure, true);
  }
  WasmEdge_CompilerContext *CompilerCxt = WasmEdge_CompilerCreate(Configure);
  WasmEdge_Result Res =
      WasmEdge_CompilerCompile(CompilerCxt, BC.getPath().c_str(), Path.c_str());
  if (!WasmEdge_ResultOK(Res)) {
    std::cerr << "WasmEdge Compile failed. Error: "
              << WasmEdge_ResultGetMessage(Res);
    return false;
  }
  return true;
}

void WasmEdgeAddon::PrepareResource(const Napi::CallbackInfo &Info,
                                    std::vector<WasmEdge_Value> &Args,
                                    IntKind IntT) {
  for (std::size_t I = 1; I < Info.Length(); I++) {
    Napi::Value Arg = Info[I];
    uint32_t MallocSize = 0, MallocAddr = 0;
    if (Arg.IsNumber()) {
      switch (IntT) {
      case IntKind::SInt32:
      case IntKind::UInt32:
      case IntKind::Default:
        Args.emplace_back(
            WasmEdge_ValueGenI32(Arg.As<Napi::Number>().Int32Value()));
        break;
      case IntKind::SInt64:
      case IntKind::UInt64: {
        if (Args.size() == 0) {
          // Set memory offset for return value
          Args.emplace_back(WasmEdge_ValueGenI32(0));
        }
        uint64_t V = static_cast<uint64_t>(Arg.As<Napi::Number>().Int64Value());
        Args.emplace_back(WasmEdge_ValueGenI32(castFromU64ToU32(V)));
        Args.emplace_back(WasmEdge_ValueGenI32(castFromU64ToU32(V >> 32)));
        break;
      }
      default:
        napi_throw_error(
            Info.Env(), "Error",
            WASMEDGE::NAPI::ErrorMsgs.at(ErrorType::NAPIUnkownIntType).c_str());
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
      napi_throw_error(
          Info.Env(), "Error",
          WASMEDGE::NAPI::ErrorMsgs.at(ErrorType::UnsupportedArgumentType)
              .c_str());
      return;
    }

    // Malloc
    WasmEdge_Value Params = WasmEdge_ValueGenI32(MallocSize);
    WasmEdge_Value Rets;
    WasmEdge_String FuncName =
        WasmEdge_StringCreateByCString("__wbindgen_malloc");
    WasmEdge_Result Res =
        WasmEdge_VMExecute(VM, FuncName, &Params, 1, &Rets, 1);
    WasmEdge_StringDelete(FuncName);
    if (!WasmEdge_ResultOK(Res)) {
      napi_throw_error(Info.Env(), "Error", WasmEdge_ResultGetMessage(Res));
      return;
    }
    MallocAddr = (uint32_t)WasmEdge_ValueGetI32(Rets);

    // Prepare arguments and memory data
    Args.emplace_back(WasmEdge_ValueGenI32(MallocAddr));
    Args.emplace_back(WasmEdge_ValueGenI32(MallocSize));

    // Setup memory
    if (Arg.IsString()) {
      std::string StrArg = Arg.As<Napi::String>().Utf8Value();
      std::vector<uint8_t> StrArgVec(StrArg.begin(), StrArg.end());
      WasmEdge_MemoryInstanceSetData(MemInst, StrArgVec.data(), MallocAddr,
                                     StrArgVec.size());
    } else if (Arg.IsTypedArray() &&
               Arg.As<Napi::TypedArray>().TypedArrayType() ==
                   napi_uint8_array) {
      Napi::ArrayBuffer DataBuffer = Arg.As<Napi::TypedArray>().ArrayBuffer();
      uint8_t *Data = (uint8_t *)DataBuffer.Data();
      WasmEdge_MemoryInstanceSetData(MemInst, Data, MallocAddr,
                                     DataBuffer.ByteLength());
    }
  }
}

void WasmEdgeAddon::PrepareResource(const Napi::CallbackInfo &Info,
                                    std::vector<WasmEdge_Value> &Args) {
  PrepareResource(Info, Args, IntKind::Default);
}

void WasmEdgeAddon::ReleaseResource(const Napi::CallbackInfo &Info,
                                    const uint32_t Offset,
                                    const uint32_t Size) {
  WasmEdge_Value Params[2] = {WasmEdge_ValueGenI32(Offset),
                              WasmEdge_ValueGenI32(Size)};
  WasmEdge_String WasmFuncName =
      WasmEdge_StringCreateByCString("__wbindgen_free");
  WasmEdge_Result Res =
      WasmEdge_VMExecute(VM, WasmFuncName, Params, 2, nullptr, 0);
  WasmEdge_StringDelete(WasmFuncName);

  if (!WasmEdge_ResultOK(Res)) {
    napi_throw_error(
        Info.Env(), "Error",
        WASMEDGE::NAPI::ErrorMsgs.at(ErrorType::WasmBindgenFreeFailed).c_str());
    return;
  }
}

Napi::Value WasmEdgeAddon::RunStart(const Napi::CallbackInfo &Info) {
  InitVM(Info);

  std::string FuncName = "_start";
  const std::vector<std::string> &WasiCmdArgs = Options.getWasiCmdArgs();
  Options.getWasiCmdArgs().erase(WasiCmdArgs.begin(), WasiCmdArgs.begin() + 2);

  InitWasi(Info, FuncName);

  // command mode
  WasmEdge_String WasmFuncName =
      WasmEdge_StringCreateByCString(FuncName.c_str());
  WasmEdge_Value Ret;
  WasmEdge_Result Res = WasmEdge_VMRunWasmFromFile(
      VM, BC.getPath().c_str(), WasmFuncName, nullptr, 0, &Ret, 1);
  WasmEdge_StringDelete(WasmFuncName);

  if (!WasmEdge_ResultOK(Res)) {
    ThrowNapiError(Info, ErrorType::ExecutionFailed);
    return Napi::Value();
  }
  auto ErrCode = WasmEdge_ResultGetCode(Res);
  FiniVM();
  return Napi::Number::New(Info.Env(), ErrCode);
}

void WasmEdgeAddon::InitReactor(const Napi::CallbackInfo &Info) {
  using namespace std::literals::string_literals;
  WasmEdge_String InitFunc = WasmEdge_StringCreateByCString("_initialize");

  bool HasInit = false;

  uint32_t FuncListLen = WasmEdge_VMGetFunctionListLength(VM);
  WasmEdge_String FuncList[FuncListLen];
  WasmEdge_VMGetFunctionList(VM, FuncList, nullptr, FuncListLen);
  for (std::size_t I = 1; I < FuncListLen; I++) {
    if (WasmEdge_StringIsEqual(FuncList[I], InitFunc)) {
      HasInit = true;
    }
  }

  if (HasInit) {
    WasmEdge_Value Ret;
    WasmEdge_Result Res = WasmEdge_VMExecute(VM, InitFunc, nullptr, 0, &Ret, 1);
    if (!WasmEdge_ResultOK(Res)) {
      napi_throw_error(
          Info.Env(), "Error",
          WASMEDGE::NAPI::ErrorMsgs.at(ErrorType::InitReactorFailed).c_str());
    }
  }
  WasmEdge_StringDelete(InitFunc);
}

void WasmEdgeAddon::Run(const Napi::CallbackInfo &Info) {
  InitVM(Info);

  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  InitWasi(Info, FuncName);

  std::vector<WasmEdge_Value> Args;
  PrepareResource(Info, Args);
  WasmEdge_String WasmFuncName =
      WasmEdge_StringCreateByCString(FuncName.c_str());
  WasmEdge_Value Ret;
  WasmEdge_Result Res =
      WasmEdge_VMExecute(VM, WasmFuncName, Args.data(), Args.size(), &Ret, 1);
  WasmEdge_StringDelete(WasmFuncName);

  if (!WasmEdge_ResultOK(Res)) {
    ThrowNapiError(Info, ErrorType::ExecutionFailed);
  }

  FiniVM();
}

Napi::Value WasmEdgeAddon::RunCompile(const Napi::CallbackInfo &Info) {
  std::string FileName;
  if (Info.Length() > 0) {
    FileName = Info[0].As<Napi::String>().Utf8Value();
  }

  return Napi::Value::From(Info.Env(), CompileBytecodeTo(FileName));
}

Napi::Value WasmEdgeAddon::RunIntImpl(const Napi::CallbackInfo &Info,
                                      IntKind IntT) {
  InitVM(Info);
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  InitWasi(Info, FuncName);

  std::vector<WasmEdge_Value> Args;
  PrepareResource(Info, Args, IntT);
  WasmEdge_String WasmFuncName =
      WasmEdge_StringCreateByCString(FuncName.c_str());
  WasmEdge_Value Ret;
  WasmEdge_Result Res =
      WasmEdge_VMExecute(VM, WasmFuncName, Args.data(), Args.size(), &Ret, 1);
  WasmEdge_StringDelete(WasmFuncName);

  if (WasmEdge_ResultOK(Res)) {
    switch (IntT) {
    case IntKind::SInt32:
    case IntKind::UInt32:
    case IntKind::Default:
      FiniVM();
      return Napi::Number::New(Info.Env(), (uint32_t)WasmEdge_ValueGetI32(Ret));
    case IntKind::SInt64:
    case IntKind::UInt64:
      uint8_t ResultMem[8];
      Res = WasmEdge_MemoryInstanceGetData(MemInst, ResultMem, 0, 8);
      if (WasmEdge_ResultOK(Res)) {
        uint32_t L = castFromBytesToU32(ResultMem, 0);
        uint32_t H = castFromBytesToU32(ResultMem, 4);
        FiniVM();
        return Napi::Number::New(Info.Env(), castFromU32ToU64(L, H));
      }
      [[fallthrough]];
    default:
      ThrowNapiError(Info, ErrorType::NAPIUnkownIntType);
      return Napi::Value();
    }
  } else {
    ThrowNapiError(Info, ErrorType::ExecutionFailed);
    return Napi::Value();
  }
}

Napi::Value WasmEdgeAddon::RunInt(const Napi::CallbackInfo &Info) {
  return RunIntImpl(Info, IntKind::SInt32);
}

Napi::Value WasmEdgeAddon::RunUInt(const Napi::CallbackInfo &Info) {
  return RunIntImpl(Info, IntKind::UInt32);
}

Napi::Value WasmEdgeAddon::RunInt64(const Napi::CallbackInfo &Info) {
  return RunIntImpl(Info, IntKind::SInt64);
}

Napi::Value WasmEdgeAddon::RunUInt64(const Napi::CallbackInfo &Info) {
  return RunIntImpl(Info, IntKind::UInt64);
}

Napi::Value WasmEdgeAddon::RunString(const Napi::CallbackInfo &Info) {
  InitVM(Info);
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  InitWasi(Info, FuncName);

  WasmEdge_Result Res;
  std::vector<WasmEdge_Value> Args;
  uint32_t ResultMemAddr = 8;
  Args.emplace_back(WasmEdge_ValueGenI32(ResultMemAddr));
  PrepareResource(Info, Args);
  WasmEdge_String WasmFuncName =
      WasmEdge_StringCreateByCString(FuncName.c_str());
  WasmEdge_Value Ret;
  Res = WasmEdge_VMExecute(VM, WasmFuncName, Args.data(), Args.size(), &Ret, 1);
  WasmEdge_StringDelete(WasmFuncName);

  if (!WasmEdge_ResultOK(Res)) {
    ThrowNapiError(Info, ErrorType::ExecutionFailed);
    return Napi::Value();
  }

  uint8_t ResultMem[8];
  Res = WasmEdge_MemoryInstanceGetData(MemInst, ResultMem, ResultMemAddr, 8);
  uint32_t ResultDataAddr = 0;
  uint32_t ResultDataLen = 0;
  if (WasmEdge_ResultOK(Res)) {
    ResultDataAddr = ResultMem[0] | (ResultMem[1] << 8) | (ResultMem[2] << 16) |
                     (ResultMem[3] << 24);
    ResultDataLen = ResultMem[4] | (ResultMem[5] << 8) | (ResultMem[6] << 16) |
                    (ResultMem[7] << 24);
  } else {
    ThrowNapiError(Info, ErrorType::BadMemoryAccess);
    return Napi::Value();
  }

  std::vector<uint8_t> ResultData(ResultDataLen);
  Res = WasmEdge_MemoryInstanceGetData(MemInst, ResultData.data(),
                                       ResultDataAddr, ResultDataLen);
  if (WasmEdge_ResultOK(Res)) {
    ReleaseResource(Info, ResultDataAddr, ResultDataLen);
  } else {
    ThrowNapiError(Info, ErrorType::BadMemoryAccess);
    return Napi::Value();
  }

  std::string ResultString(ResultData.begin(), ResultData.end());
  FiniVM();
  return Napi::String::New(Info.Env(), ResultString);
}

Napi::Value WasmEdgeAddon::RunUint8Array(const Napi::CallbackInfo &Info) {
  InitVM(Info);
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  InitWasi(Info, FuncName);

  WasmEdge_Result Res;
  std::vector<WasmEdge_Value> Args;
  uint32_t ResultMemAddr = 8;
  Args.emplace_back(WasmEdge_ValueGenI32(ResultMemAddr));
  PrepareResource(Info, Args);
  WasmEdge_String WasmFuncName =
      WasmEdge_StringCreateByCString(FuncName.c_str());
  WasmEdge_Value Ret;
  Res = WasmEdge_VMExecute(VM, WasmFuncName, Args.data(), Args.size(), &Ret, 1);
  WasmEdge_StringDelete(WasmFuncName);

  if (!WasmEdge_ResultOK(Res)) {
    ThrowNapiError(Info, ErrorType::ExecutionFailed);
    return Napi::Value();
  }

  uint8_t ResultMem[8];
  Res = WasmEdge_MemoryInstanceGetData(MemInst, ResultMem, ResultMemAddr, 8);
  uint32_t ResultDataAddr = 0;
  uint32_t ResultDataLen = 0;
  if (WasmEdge_ResultOK(Res)) {
    ResultDataAddr = ResultMem[0] | (ResultMem[1] << 8) | (ResultMem[2] << 16) |
                     (ResultMem[3] << 24);
    ResultDataLen = ResultMem[4] | (ResultMem[5] << 8) | (ResultMem[6] << 16) |
                    (ResultMem[7] << 24);
  } else {
    ThrowNapiError(Info, ErrorType::BadMemoryAccess);
    return Napi::Value();
  }

  std::vector<uint8_t> ResultData(ResultDataLen);
  Res = WasmEdge_MemoryInstanceGetData(MemInst, ResultData.data(),
                                       ResultDataAddr, ResultDataLen);
  if (WasmEdge_ResultOK(Res)) {
    ReleaseResource(Info, ResultDataAddr, ResultDataLen);
  } else {
    ThrowNapiError(Info, ErrorType::BadMemoryAccess);
    return Napi::Value();
  }

  Napi::ArrayBuffer ResultArrayBuffer =
      Napi::ArrayBuffer::New(Info.Env(), &(ResultData[0]), ResultDataLen);
  Napi::Uint8Array ResultTypedArray = Napi::Uint8Array::New(
      Info.Env(), ResultDataLen, ResultArrayBuffer, 0, napi_uint8_array);
  FiniVM();
  return ResultTypedArray;
}

void WasmEdgeAddon::LoadWasm(const Napi::CallbackInfo &Info) {
  Napi::Env Env = Info.Env();
  Napi::HandleScope Scope(Env);

  if (BC.isCompiled()) {
    Cache.dumpToFile(BC.getData());
    BC.setPath(Cache.getPath());
  }

  WasmEdge_Result Res;
  if (BC.isFile()) {
    Res = WasmEdge_VMLoadWasmFromFile(VM, BC.getPath().c_str());
    if (!WasmEdge_ResultOK(Res)) {
      ThrowNapiError(Info, ErrorType::LoadWasmFailed);
      return;
    }
  } else if (BC.isValidData()) {
    Res = WasmEdge_VMLoadWasmFromBuffer(VM, BC.getData().data(),
                                        BC.getData().size());
    if (!WasmEdge_ResultOK(Res)) {
      ThrowNapiError(Info, ErrorType::LoadWasmFailed);
      return;
    }
  }

  Res = WasmEdge_VMValidate(VM);
  if (!WasmEdge_ResultOK(Res)) {
    ThrowNapiError(Info, ErrorType::ValidateWasmFailed);
    return;
  }

  Res = WasmEdge_VMInstantiate(VM);
  if (!WasmEdge_ResultOK(Res)) {
    ThrowNapiError(Info, ErrorType::InstantiateWasmFailed);
    return;
  }

  // Get memory instance
  uint32_t MemLen = WasmEdge_StoreListMemoryLength(Store);
  WasmEdge_String MemNames[MemLen];
  WasmEdge_StoreListMemory(Store, MemNames, MemLen);
  MemInst = WasmEdge_StoreFindMemory(Store, MemNames[0]);
}

Napi::Value WasmEdgeAddon::GetStatistics(const Napi::CallbackInfo &Info) {
  Napi::Object RetStat = Napi::Object::New(Info.Env());
  if (!Options.isMeasuring()) {
    RetStat.Set("Measure", Napi::Boolean::New(Info.Env(), false));
  } else {
    RetStat.Set("Measure", Napi::Boolean::New(Info.Env(), true));
    RetStat.Set(
        "InstructionCount",
        Napi::Number::New(Info.Env(), WasmEdge_StatisticsGetInstrCount(Stat)));
    RetStat.Set(
        "TotalGasCost",
        Napi::Number::New(Info.Env(), WasmEdge_StatisticsGetTotalCost(Stat)));
    RetStat.Set("InstructionPerSecond",
                Napi::Number::New(Info.Env(),
                                  WasmEdge_StatisticsGetInstrPerSecond(Stat)));
  }

  return RetStat;
}
