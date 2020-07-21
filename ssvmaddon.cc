#include "ssvmaddon.h"
#if 0
#include "aot/compiler.h"

#include <cstdlib> // std::mkstemp
#include <fstream> // std::ifstream, std::ofstream

#include <boost/functional/hash.hpp>
#endif

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
#if 0
        InstanceMethod("Run", &SSVMAddon::Run),
        // Disable aot, will split into another tool
        InstanceMethod("RunAot", &SSVMAddon::RunAot),
        InstanceMethod("Compile", &SSVMAddon::Compile),
        InstanceMethod("GetAotBinary", &SSVMAddon::GetAotBinary),
#endif
        InstanceMethod("GetStatistics", &SSVMAddon::GetStatistics),
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

#if 0
// Disable aot, will split into another tool
std::string createTmpFile() {
  char *TmpName = strdup("/tmp/ssvmTmpCode-XXXXXX");
  mkstemp(TmpName);
  std::ofstream f(TmpName);
  f.close();
  return std::string(TmpName);
}

std::string copyTmpFile(std::string &FilePath) {
  std::string SoName = FilePath+std::string(".so");
  std::filesystem::copy_file(FilePath, SoName);
  return SoName;
}

bool isELF(const std::vector<uint8_t> &Bytecode) {
  if (Bytecode[0] == 0x7f &&
      Bytecode[1] == 0x45 &&
      Bytecode[2] == 0x4c &&
      Bytecode[3] == 0x46) {
    return true;
  }
  return false;
}

bool isMachO(const std::vector<uint8_t> &Bytecode) {
  if ((Bytecode[0] == 0xfe && // Mach-O 32 bit
        Bytecode[1] == 0xed &&
        Bytecode[2] == 0xfa &&
        Bytecode[3] == 0xce) ||
      (Bytecode[0] == 0xfe && // Mach-O 64 bit
        Bytecode[1] == 0xed &&
        Bytecode[2] == 0xfa &&
        Bytecode[3] == 0xcf) ||
      (Bytecode[0] == 0xca && // Mach-O Universal
        Bytecode[1] == 0xfe &&
        Bytecode[2] == 0xba &&
        Bytecode[3] == 0xbe)) {
    return true;
  }
  return false;
}

bool isCompiled(const SSVMAddon::InputMode &IMode) {
  if (IMode == SSVMAddon::InputMode::MachOBytecode ||
      IMode == SSVMAddon::InputMode::ELFBytecode) {
    return true;
  }
  return false;
}

std::string dumpToFile(const std::vector<uint8_t> &Bytecode) {
  std::string TmpFilePath = createTmpFile();
  TmpFilePath.append(".so");
  std::ofstream TmpFile(TmpFilePath);
  std::ostream_iterator<uint8_t> OutIter(TmpFile);
  std::copy(Bytecode.begin(), Bytecode.end(), OutIter);
  TmpFile.close();
  return TmpFilePath;
}
#endif

inline bool checkInputWasmFormat(const Napi::CallbackInfo &Info) {
  return Info.Length() <= 0 || (!Info[0].IsString() && !Info[0].IsTypedArray());
}

inline bool isWasiOptionsProvided(const Napi::CallbackInfo &Info) {
  return Info.Length() == 2 && Info[1].IsObject();
}

inline bool isWasmBindgenDisabled(const Napi::Object &WasiOptions) {
  return WasiOptions.Has("DisableWasmBindgen")
    && WasiOptions.Get("DisableWasmBindgen").IsBoolean()
    && WasiOptions.Get("DisableWasmBindgen").As<Napi::Boolean>().Value();
}

} // namespace details

SSVMAddon::SSVMAddon(const Napi::CallbackInfo &Info)
  : Napi::ObjectWrap<SSVMAddon>(Info), Configure(nullptr),
  VM(nullptr), MemInst(nullptr),
  WasiMod(nullptr), WBMode(true), Inited(false) {

    Napi::Env Env = Info.Env();
    Napi::HandleScope Scope(Env);

    if (checkInputWasmFormat(Info)) {
      Napi::Error::New(Env, "Expected a Wasm file or a Wasm binary sequence.").ThrowAsJavaScriptException();
      return;
    }

    // Assume the WasmBindgen is enabled by default.
    // Assume the WASI options object is {}

    // Check if a Wasi options object is given or not
    if (isWasiOptionsProvided(Info)) {
      // Get a WASI options object
      Napi::Object WasiOptions = Info[1].As<Napi::Object>();
      if (!parseCmdArgs(WasiCmdArgs, WasiOptions)) {
        Napi::Error::New(Info.Env(),
            "Parse commandline arguments from Wasi options failed.")
          .ThrowAsJavaScriptException();
        return;
      }
      if (!parseDirs(WasiDirs, WasiOptions)) {
        Napi::Error::New(Info.Env(),
            "Parse preopens from Wasi options failed.")
          .ThrowAsJavaScriptException();
        return;
      }
      if (!parseEnvs(WasiEnvs, WasiOptions)) {
        Napi::Error::New(Info.Env(),
            "Parse environment variables from Wasi options failed.")
          .ThrowAsJavaScriptException();
        return;
      }

      if (isWasmBindgenDisabled(WasiOptions)) {
        WBMode = false;
      }
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
#if 0
      } else if (isELF(InputBytecode)) {
        IMode = InputMode::ELFBytecode;
      } else if (isMachO(InputBytecode)) {
        IMode = InputMode::MachOBytecode;
#endif
      } else {
        Napi::Error::New(Env, "Unknown bytecode format.").ThrowAsJavaScriptException();
        return;
      }
    } else {
      Napi::Error::New(Env, "Wasm bytecode is not a valid Uint8Array or not a valid file path.").ThrowAsJavaScriptException();
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
  VM = new SSVM::VM::VM(*Configure);

  SSVM::Log::setErrorLoggingLevel();

  WasiMod = dynamic_cast<SSVM::Host::WasiModule *>(
      VM->getImportModule(SSVM::VM::Configure::VMType::Wasi));

  if (WBMode) {
    EnableWasmBindgen(Info);
  }
}

#if 0
void SSVMAddon::Compile(const Napi::CallbackInfo &Info) {
  SSVM::Loader::Loader Loader;
  std::vector<SSVM::Byte> Data;

  if (IMode == InputMode::FilePath) {
    /// File mode
    /// We have to load bytecode from given file.
    std::filesystem::path P = std::filesystem::absolute(std::filesystem::path(InputPath));
    if (auto Res = Loader.loadFile(P.string())) {
      Data = std::move(*Res);
    } else {
      const auto Err = static_cast<uint32_t>(Res.error());
      Napi::Error::New(Info.Env(), "Load file failed.").ThrowAsJavaScriptException();
      return;
    }
  }

  /// Check hash.
  std::size_t CodeHash = boost::hash_range(Data.begin(), Data.end());
  if (CodeCache.find(CodeHash) != CodeCache.end()) {
    return;
  }

  std::unique_ptr<SSVM::AST::Module> Module;
  if (auto Res = Loader.parseModule(Data)) {
    Module = std::move(*Res);
  } else {
    const auto Err = static_cast<uint32_t>(Res.error());
    Napi::Error::New(Info.Env(), "Parse module failed.").ThrowAsJavaScriptException();
    return;
  }

  CurAotBinPath = createTmpFile();
  SSVM::AOT::Compiler Compiler;
  if (auto Res = Compiler.compile(Data, *Module, CurAotBinPath); !Res) {
    const auto Err = static_cast<uint32_t>(Res.error());
    Napi::Error::New(Info.Env(), "Compile failed.").ThrowAsJavaScriptException();
    return;
  }
  CurAotBinPath = copyTmpFile(CurAotBinPath);
  CodeCache[CodeHash] = CurAotBinPath;
}

Napi::Value SSVMAddon::GetAotBinary(const Napi::CallbackInfo &Info) {
  std::ifstream AotBinary(CurAotBinPath, std::ifstream::binary);
  if (AotBinary) {
    /// Determine file size
    AotBinary.seekg(0, AotBinary.end);
    std::size_t BinSize = AotBinary.tellg();
    AotBinary.seekg(0, AotBinary.beg);

    /// Allocate buffer memory
    char *Buf = new char [BinSize];

    /// Get file content and close file
    AotBinary.read(Buf, BinSize);
    AotBinary.close();

    Napi::ArrayBuffer ResultArrayBuffer =
      Napi::ArrayBuffer::New(Info.Env(), Buf, BinSize);
    Napi::Uint8Array ResultTypedArray = Napi::Uint8Array::New(
        Info.Env(), BinSize, ResultArrayBuffer, 0, napi_uint8_array);
    delete Buf;
    return ResultTypedArray;
  } else {
    return Info.Env().Undefined();
  }
}

void SSVMAddon::RunAot(const Napi::CallbackInfo &Info) {
  if (!isCompiled(IMode)) {
    Compile(Info);
  } else { /// ELF, MachO from InputBytecode
    CurAotBinPath = dumpToFile(InputBytecode);
  }


  SSVM::VM::Configure Conf;
  Conf.addVMType(SSVM::VM::Configure::VMType::Wasi);
  SSVM::VM::VM VM(Conf);

  SSVM::Log::setErrorLoggingLevel();

  SSVM::Host::WasiModule *WasiMod = dynamic_cast<SSVM::Host::WasiModule *>(
      VM.getImportModule(SSVM::VM::Configure::VMType::Wasi));

  /// Handle arguments from the wasi options
  if (Info.Length() > 0) {
    Napi::Object WasiOptions = Info[0].As<Napi::Object>();
    if (!parseCmdArgs(WasiCmdArgs, WasiOptions)) {
      Napi::Error::New(Info.Env(),
          "Parse commandline arguments from Wasi options failed.")
        .ThrowAsJavaScriptException();
      return;
    }
    if (!parseDirs(WasiDirs, WasiOptions)) {
      Napi::Error::New(Info.Env(),
          "Parse preopens from Wasi options failed.")
        .ThrowAsJavaScriptException();
      return;
    }
    if (!parseEnvs(WasiEnvs, WasiOptions)) {
      Napi::Error::New(Info.Env(),
          "Parse environment variables from Wasi options failed.")
        .ThrowAsJavaScriptException();
      return;
    }
  }

  /// Set up the wasi modules with given wasi options
  WasiMod->getEnv().init(WasiDirs, FuncName, WasiCmdArgs, WasiEnvs);

  /// Execute
  if (auto Result = VM.runWasmFile(CurAotBinPath, "_start")) {
    return;
  } else {
    std::cerr << "Failed. Error code : "
              << static_cast<uint32_t>(Result.error()) << '\n';
    Napi::TypeError::New(Info.Env(), "Execution failed.")
      .ThrowAsJavaScriptException();
    return;
  }
}
#endif

#if 0
void SSVMAddon::PrepareResource(const Napi::CallbackInfo &Info) {
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
      ArgsVec.push_back(std::to_string(Arg.As<Napi::Number>().Uint32Value()));
    } else if (Arg.IsString()) {
      ArgsVec.push_back(Arg.As<Napi::String>().Utf8Value());
    } else if (Arg.IsTypedArray() &&
        Arg.As<Napi::TypedArray>().TypedArrayType() ==
        napi_uint8_array) {
      Napi::ArrayBuffer DataBuffer = Arg.As<Napi::TypedArray>().ArrayBuffer();
      std::string ArrayArg = std::string(
          static_cast<char*>(DataBuffer.Data()),
          static_cast<char*>(DataBuffer.Data()) + DataBuffer.ByteLength());
      ArgsVec.push_back(ArrayArg);
    } else {
      // TODO: support other types
      Napi::TypeError::New(Info.Env(),
          "unsupported argument type in WASI Options")
        .ThrowAsJavaScriptException();
      return;
    }
  }
  if (IMode == InputMode::FilePath) {
    WasiMod->getEnv().init({}/* Dir name */, InputPath, ArgsVec);
  } else {
    WasiMod->getEnv().init({}/* Dir name */, std::string(InputBytecode.begin(), InputBytecode.end()), ArgsVec);
  }
}
#endif

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

#if 0
void SSVMAddon::ReleaseResource() {
  ArgsVec.erase(ArgsVec.begin(), ArgsVec.end());
}
#endif

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

#if 0
Napi::Value SSVMAddon::Run(const Napi::CallbackInfo &Info) {
  InitVM(Info);
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
  return Napi::Number::New(Info.Env(), 0);
}
#endif

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

Napi::Value SSVMAddon::RunInt(const Napi::CallbackInfo &Info) {
  InitVM(Info);
  if (!WBMode) {
    Napi::Error::New(Info.Env(), "RunInt function only supports WasmBindgen mode")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  WasiMod->getEnv().init(WasiDirs, FuncName, WasiCmdArgs, WasiEnvs);

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
  InitVM(Info);
  if (!WBMode) {
    Napi::Error::New(Info.Env(), "RunString function only supports WasmBindgen mode")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  WasiMod->getEnv().init(WasiDirs, FuncName, WasiCmdArgs, WasiEnvs);

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
  InitVM(Info);
  if (!WBMode) {
    Napi::Error::New(Info.Env(), "RunUin8Array function only supports WasmBindgen mode")
      .ThrowAsJavaScriptException();
    return Napi::Value();
  }
  std::string FuncName = "";
  if (Info.Length() > 0) {
    FuncName = Info[0].As<Napi::String>().Utf8Value();
  }

  WasiMod->getEnv().init(WasiDirs, FuncName, WasiCmdArgs, WasiEnvs);

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

  if ((IMode == InputMode::FilePath && !(VM->loadWasm(InputPath)))
      || (IMode == InputMode::WasmBytecode && !(VM->loadWasm(InputBytecode)))) {
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
