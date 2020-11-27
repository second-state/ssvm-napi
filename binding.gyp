{
  "targets": [
    {
      "target_name": "<(module_name)",
      "cflags_cc": [ "-std=c++17" ],
      "cflags!": [ "-fno-exceptions", "-fno-rtti" ],
      "cflags_cc!": [ "-fno-exceptions", "-fno-rtti" ],
      "link_settings": {
          "libraries": [
              "/usr/lib/llvm-10/lib/libLLVM.so",
              "/usr/lib/llvm-10/lib/liblldELF.a",
              "/usr/lib/llvm-10/lib/liblldCommon.a",
              "/usr/lib/llvm-10/lib/liblldCore.a",
              "/usr/lib/llvm-10/lib/liblldDriver.a",
              "/usr/lib/llvm-10/lib/liblldReaderWriter.a",
              "/usr/lib/llvm-10/lib/liblldYAML.a",
          ],
      },
      "sources": [
        "addon.cc",
        "bytecode.cc",
        "options.cc",
        "ssvmaddon.cc",
        "utils.cc",
        "ssvm-core/lib/aot/compiler.cpp",
        "ssvm-core/lib/ast/description.cpp",
        "ssvm-core/lib/ast/expression.cpp",
        "ssvm-core/lib/ast/instruction.cpp",
        "ssvm-core/lib/ast/module.cpp",
        "ssvm-core/lib/ast/section.cpp",
        "ssvm-core/lib/ast/segment.cpp",
        "ssvm-core/lib/ast/type.cpp",
        "ssvm-core/lib/common/hexstr.cpp",
        "ssvm-core/lib/common/log.cpp",
        "ssvm-core/lib/host/ssvm_process/processfunc.cpp",
        "ssvm-core/lib/host/ssvm_process/processmodule.cpp",
        "ssvm-core/lib/host/wasi/wasienv.cpp",
        "ssvm-core/lib/host/wasi/wasifunc.cpp",
        "ssvm-core/lib/host/wasi/wasimodule.cpp",
        "ssvm-core/lib/interpreter/engine/control.cpp",
        "ssvm-core/lib/interpreter/engine/engine.cpp",
        "ssvm-core/lib/interpreter/engine/memory.cpp",
        "ssvm-core/lib/interpreter/engine/variable.cpp",
        "ssvm-core/lib/interpreter/instantiate/data.cpp",
        "ssvm-core/lib/interpreter/instantiate/elem.cpp",
        "ssvm-core/lib/interpreter/instantiate/export.cpp",
        "ssvm-core/lib/interpreter/instantiate/function.cpp",
        "ssvm-core/lib/interpreter/instantiate/global.cpp",
        "ssvm-core/lib/interpreter/instantiate/import.cpp",
        "ssvm-core/lib/interpreter/instantiate/memory.cpp",
        "ssvm-core/lib/interpreter/instantiate/module.cpp",
        "ssvm-core/lib/interpreter/instantiate/table.cpp",
        "ssvm-core/lib/interpreter/interpreter.cpp",
        "ssvm-core/lib/loader/filemgr.cpp",
        "ssvm-core/lib/loader/ldmgr.cpp",
        "ssvm-core/lib/loader/loader.cpp",
        "ssvm-core/lib/validator/formchecker.cpp",
        "ssvm-core/lib/validator/validator.cpp",
        "ssvm-core/lib/vm/vm.cpp",
        "ssvm-core/thirdparty/easyloggingpp/easylogging++.cc",
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "ssvm-core/include",
        "ssvm-core/thirdparty",
        "ssvm-core/thirdparty/googletest/include",
        "/usr/lib/llvm-10/include",
      ],
      'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
    },
    {
      "target_name": "action_after_build",
      "type": "none",
      "dependencies": [ "<(module_name)" ],
      "copies": [
        {
          "files": [ "<(PRODUCT_DIR)/<(module_name).node" ],
          "destination": "<(module_path)"
        }
      ]
    }
  ]
}
