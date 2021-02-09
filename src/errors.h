#pragma once

#include <limits>
#include <napi.h>
#include <string>

namespace SSVM {
namespace NAPI {

enum class ErrorType {
  ExpectWasmFileOrBytecode,
  ParseOptionsFailed,
  UnknownBytecodeFormat,
  UnsupportedArgumentType,
  InvalidInputFormat,
  LoadWasmFailed,
  ValidateWasmFailed,
  InstantiateWasmFailed,
  ExecutionFailed,
  BadMemoryAccess,
  InitReactorFailed,
  WasmBindgenMallocFailed,
  WasmBindgenFreeFailed,
  NAPIUnkownIntType
};

const std::map<ErrorType, std::string> ErrorMsgs = {
    {ErrorType::ExpectWasmFileOrBytecode,
     "Expected a Wasm file or a Wasm binary sequence."},
    {ErrorType::ParseOptionsFailed, "Parse options failed."},
    {ErrorType::UnknownBytecodeFormat, "Unknown bytecode format."},
    {ErrorType::InvalidInputFormat,
     "Input Wasm is not a valid Uint8Array or not a valid file path."},
    {ErrorType::LoadWasmFailed,
     "Wasm bytecode/file cannot be loaded correctly."},
    {ErrorType::ValidateWasmFailed,
     "Wasm bytecode/file failed at validation stage."},
    {ErrorType::InstantiateWasmFailed,
     "Wasm bytecode/file cannot be instantiated."},
    {ErrorType::ExecutionFailed, "SSVM execution failed"},
    {ErrorType::BadMemoryAccess,
     "Access to forbidden memory address when retrieving address and "
     "length of result data"},
    {ErrorType::InitReactorFailed, "Initialize wasi reactor mode failed"},
    {ErrorType::WasmBindgenMallocFailed,
     "Failed to call wasm-bindgen helper function __wbindgen_malloc"},
    {ErrorType::WasmBindgenFreeFailed,
     "Failed to call wasm-bindgen helper function __wbindgen_free"},
    {ErrorType::NAPIUnkownIntType,
     "SSVM-Napi implementation error: unknown integer type"},
    {ErrorType::UnsupportedArgumentType, "Unsupported argument type"}};

} // namespace NAPI
} // namespace SSVM
