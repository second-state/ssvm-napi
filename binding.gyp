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
        "ssvm-core/lib/executor/worker/control.cpp",
        "ssvm-core/lib/executor/worker/memory.cpp",
        "ssvm-core/lib/executor/worker/variable.cpp",
        "ssvm-core/lib/executor/worker/provider.cpp",
        "ssvm-core/lib/executor/executor.cpp",
        "ssvm-core/lib/executor/worker.cpp",
        "ssvm-core/lib/executor/instantiate/export.cpp",
        "ssvm-core/lib/executor/instantiate/import.cpp",
        "ssvm-core/lib/executor/instantiate/memory.cpp",
        "ssvm-core/lib/executor/instantiate/global.cpp",
        "ssvm-core/lib/executor/instantiate/type.cpp",
        "ssvm-core/lib/executor/instantiate/module.cpp",
        "ssvm-core/lib/executor/instantiate/table.cpp",
        "ssvm-core/lib/executor/instantiate/function.cpp",
        "ssvm-core/lib/executor/instance/memory.cpp",
        "ssvm-core/lib/executor/instance/global.cpp",
        "ssvm-core/lib/executor/instance/entity.cpp",
        "ssvm-core/lib/executor/instance/module.cpp",
        "ssvm-core/lib/executor/instance/table.cpp",
        "ssvm-core/lib/executor/instance/function.cpp",
        "ssvm-core/lib/ast/load/segment.cpp",
        "ssvm-core/lib/ast/load/section.cpp",
        "ssvm-core/lib/ast/load/type.cpp",
        "ssvm-core/lib/ast/load/description.cpp",
        "ssvm-core/lib/ast/load/expression.cpp",
        "ssvm-core/lib/ast/load/module.cpp",
        "ssvm-core/lib/ast/load/instruction.cpp",
        "ssvm-core/lib/support/log.cpp",
        "ssvm-core/lib/vm/environment.cpp",
        "ssvm-core/lib/vm/vm.cpp",
        "ssvm-core/lib/validator/validator.cpp",
        "ssvm-core/lib/validator/vm.cpp",
        "ssvm-core/lib/loader/loader.cpp",
        "ssvm-core/lib/loader/filemgr.cpp",
        "ssvm-core/lib/proxy/proxy.cpp",
        "ssvm-core/lib/proxy/cmdparser.cpp",
        "ssvm-core/thirdparty/easyloggingpp/easylogging++.cc",
        "ssvm-core/thirdparty/keccak/Keccak.cpp",
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
