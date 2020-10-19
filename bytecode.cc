#include "bytecode.h"

namespace SSVM {
namespace NAPI {

void Bytecode::setPath(const std::string &IPath) noexcept {
  Path = IPath;
  Mode = InputMode::FilePath;
}

void Bytecode::setData(const std::vector<uint8_t> &IData) noexcept {
  Data = IData;
  if (isWasm()) {
    Mode = InputMode::WasmBytecode;
  } else if (isELF()) {
    Mode = InputMode::ELFBytecode;
  } else if (isMachO()) {
    Mode = InputMode::MachOBytecode;
  } else {
    Mode = InputMode::Invalid;
  }
}

bool Bytecode::isFile() const noexcept {
  if (Mode == InputMode::FilePath) {
    return true;
  }
  return false;
}

bool Bytecode::isWasm() const noexcept {
  if (Data[0] == 0x00 && Data[1] == 0x61 && Data[2] == 0x73 &&
      Data[3] == 0x6d) {
    return true;
  }
  return false;
}

bool Bytecode::isELF() const noexcept {
  if (Data[0] == 0x7f && Data[1] == 0x45 && Data[2] == 0x4c &&
      Data[3] == 0x46) {
    return true;
  }
  return false;
}

bool Bytecode::isMachO() const noexcept {
  if ((Data[0] == 0xfe && // Mach-O 32 bit
       Data[1] == 0xed && Data[2] == 0xfa && Data[3] == 0xce) ||
      (Data[0] == 0xfe && // Mach-O 64 bit
       Data[1] == 0xed && Data[2] == 0xfa && Data[3] == 0xcf) ||
      (Data[0] == 0xca && // Mach-O Universal
       Data[1] == 0xfe && Data[2] == 0xba && Data[3] == 0xbe)) {
    return true;
  }
  return false;
}

bool Bytecode::isCompiled() const noexcept {
  if (Mode == InputMode::MachOBytecode || Mode == InputMode::ELFBytecode) {
    return true;
  }
  return false;
}

bool Bytecode::isValidData() const noexcept {
  if (isFile()) {
    return false;
  }
  if (isWasm() || isELF() || isMachO()) {
    return true;
  }
  return false;
}

} // namespace NAPI
} // namespace SSVM
