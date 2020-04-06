{
  "targets": [
    {
      "target_name": "<(module_name)",
      "cflags_cc": [ "-std=c++17", "-lboost" ],
      "cflags!": [ "-fno-exceptions", "-fno-rtti" ],
      "cflags_cc!": [ "-fno-exceptions", "-fno-rtti" ],
      "libraries": [
        "/usr/lib/x86_64-linux-gnu/libboost_system.so",
      ],
      "sources": [
        "addon.cc",
        "ssvmaddon.cc",
        "ssvm-core/lib/interpreter/engine/control.cpp",
        "ssvm-core/lib/interpreter/engine/memory.cpp",
        "ssvm-core/lib/interpreter/engine/provider.cpp",
        "ssvm-core/lib/interpreter/engine/variable.cpp",
        "ssvm-core/lib/interpreter/engine/engine.cpp",
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
        "ssvm-core/lib/ast/segment.cpp",
        "ssvm-core/lib/ast/section.cpp",
        "ssvm-core/lib/ast/type.cpp",
        "ssvm-core/lib/ast/description.cpp",
        "ssvm-core/lib/ast/expression.cpp",
        "ssvm-core/lib/ast/module.cpp",
        "ssvm-core/lib/ast/instruction.cpp",
        "ssvm-core/lib/support/log.cpp",
        "ssvm-core/lib/expvm/vm.cpp",
        "ssvm-core/lib/validator/validator.cpp",
        "ssvm-core/lib/validator/formchecker.cpp",
        "ssvm-core/lib/loader/loader.cpp",
        "ssvm-core/lib/loader/filemgr.cpp",
        "ssvm-core/thirdparty/easyloggingpp/easylogging++.cc",
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "ssvm-core/include",
        "ssvm-core/thirdparty",
        "ssvm-core/thirdparty/evmc/include",
        "ssvm-core/thirdparty/googletest/include",
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
