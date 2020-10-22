const assert = require('assert');
const fs = require('fs');
const ssvm = require('../..');

describe('aot compile', function() {
  // Filename MUST ends with '.so'
  let filename = 'pkg/integers_lib_bg.wasm.so';

  this.timeout(0);

  before(function() {
    if (fs.existsSync(filename)) {
      fs.unlinkSync(filename); // Alt solution for fs.rmSync (added in v14.14)
    }
  });

  it('from bytearray', function() {
    let bytearray = fs.readFileSync('pkg/integers_lib_bg.wasm');
    let vm = new ssvm.VM(bytearray, {
      EnableAOT: true,
      args: process.argv,
      env: process.env,
      preopens: { '/': __dirname },
    });

    assert.ok(vm.Compile(filename));
  });

  // XXX: disabled because it breaks SSVM interpreter singleton
  /*
  it('from aot filename', function() {
    let vm = new ssvm.VM(filename, {
      EnableAOT: true,
      args: process.argv,
      env: process.env,
      preopens: { '/': __dirname },
    });

    assert.equal(vm.RunInt('lcm_s32', 123, 1011), 41451);
    assert.equal(vm.RunUInt('lcm_u32', 2147483647, 2), 4294967294);
    assert.equal(vm.RunInt64('lcm_s64', 2147483647, 2), 4294967294);
    assert.equal(vm.RunUInt64('lcm_u64', 9223372036854775807, 2), 18446744073709551614);
  });
  */

  after(function() {
    if (fs.existsSync(filename)) {
      fs.unlinkSync(filename); // Alt solution for fs.rmSync (added in v14.14)
    }
  });
});
