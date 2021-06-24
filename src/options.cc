#include "options.h"

namespace WASMEDGE {
namespace NAPI {

namespace {

bool parseWasiStartFlag(const Napi::Object &Options) {
  if (Options.Has("EnableWasiStartFunction") &&
      Options.Get("EnableWasiStartFunction").IsBoolean()) {
    return Options.Get("EnableWasiStartFunction").As<Napi::Boolean>().Value();
  }
  return false;
}

bool parseAllowedCmds(std::vector<std::string> &AllowedCmds, const Napi::Object &Options) {
  AllowedCmds.clear();
  if (Options.Has(kAllowedCommandsString) && Options.Get(kAllowedCommandsString).IsArray()) {
    Napi::Array Cmds = Options.Get(kAllowedCommandsString).As<Napi::Array>();
    for (uint32_t I = 0; I < Cmds.Length(); I++) {
      Napi::Value Cmd = Cmds[I];
      if (Cmd.IsString()) {
        AllowedCmds.push_back(Cmd.As<Napi::String>().Utf8Value());
      } else {
        // Invalid inputs
        return false;
      }
    }
  }
  return true;
}

bool parseAllowedCmdsAll(const Napi::Object &Options) {
  if (Options.Has(kAllowedCommandsAllString) && Options.Get(kAllowedCommandsAllString).IsBoolean()) {
    return Options.Get(kAllowedCommandsAllString).As<Napi::Boolean>().Value();
  }
  return false;
}


bool parseCmdArgs(std::vector<std::string> &CmdArgs,
                  const Napi::Object &Options) {
  CmdArgs.clear();
  if (Options.Has(kCmdArgsString) && Options.Get(kCmdArgsString).IsArray()) {
    Napi::Array Args = Options.Get(kCmdArgsString).As<Napi::Array>();
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
            static_cast<char *>(DataBuffer.Data()),
            static_cast<char *>(DataBuffer.Data()) + DataBuffer.ByteLength());
        CmdArgs.push_back(ArrayArg);
      } else {
        // TODO: support other types
        return false;
      }
    }
  }
  return true;
}

bool parseDirs(std::vector<std::string> &Dirs, const Napi::Object &Options) {
  Dirs.clear();
  if (Options.Has(kPreOpensString) && Options.Get(kPreOpensString).IsObject()) {
    Napi::Object Preopens = Options.Get(kPreOpensString).As<Napi::Object>();
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

bool parseEnvs(std::vector<std::string> &Envs, const Napi::Object &Options) {
  Envs.clear();
  if (Options.Has(kEnvString) && Options.Get(kEnvString).IsObject()) {
    Napi::Object Environs = Options.Get(kEnvString).As<Napi::Object>();
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

bool parseAOTConfig(const Napi::Object &Options) {
  if (Options.Has(kEnableAOTString) && Options.Get(kEnableAOTString).IsBoolean()) {
    return Options.Get(kEnableAOTString).As<Napi::Boolean>().Value();
  }
  return false;
}

bool parseMeasure(const Napi::Object &Options) {
  if (Options.Has(kEnableMeasurementString) &&
      Options.Get(kEnableMeasurementString).IsBoolean()) {
    return Options.Get(kEnableMeasurementString).As<Napi::Boolean>().Value();
  }
  return false;
}

} // namespace

bool Options::parse(const Napi::Object &Options) {
  if (!parseCmdArgs(getWasiCmdArgs(), Options) ||
      !parseDirs(getWasiDirs(), Options) ||
      !parseEnvs(getWasiEnvs(), Options) ||
      !parseAllowedCmds(getAllowedCmds(), Options)) {
    return false;
  }
  setReactorMode(!parseWasiStartFlag(Options));
  setAOTMode(parseAOTConfig(Options));
  setMeasure(parseMeasure(Options));
  setAllowedCmdsAll(parseAllowedCmdsAll(Options));
  return true;
}

} // namespace NAPI
} // namespace WASMEDGE
