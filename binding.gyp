{
  "targets": [
    {
      "target_name": "<(module_name)",
      "cflags_cc": [ "-std=c++17" ],
      "cflags!": [ "-fno-exceptions", "-fno-rtti" ],
      "cflags_cc!": [ "-fno-exceptions", "-fno-rtti" ],
      "link_settings": {
          "libraries": [
              "/usr/local/lib/libwasmedge_c.so",
          ],
      },
      "sources": [
        "src/addon.cc",
        "src/bytecode.cc",
        "src/options.cc",
        "src/wasmedgeaddon.cc",
        "src/utils.cc",
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "src",
        "/usr/local/include",
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
