#include "wasmedgeaddon.h"

#include <napi.h>

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  return WasmEdgeAddon::Init(env, exports);
}

NODE_API_MODULE(addon, InitAll)
