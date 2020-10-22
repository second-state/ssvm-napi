// Note: This test was supposed to be part of 'js/test-aot.js', but
//       putting two `new ssvm.VM` in the same file caused SSVM to
//       crash because of the implementation of interpreter (singleton).
const assert = require('assert');
const fs = require('fs');
const ssvm = require('../..');

describe('aot runtime', function() {
  // Filename MUST ends with '.so'
  let filename = 'data/integers_lib.aot.so';

  before(function() {
    assert.ok(fs.existsSync(filename));
  });

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
});
