var binary = require('@mapbox/node-pre-gyp');
var path = require('path')
var binding_path =
    binary.find(path.resolve(path.join(__dirname, './package.json')));

const os = require('os');
process.dlopen(module, binding_path,
               os.constants.dlopen.RTLD_LAZY | os.constants.dlopen.RTLD_GLOBAL);
