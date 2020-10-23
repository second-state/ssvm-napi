const assert = require('assert');
const fs = require('fs');
const ssvm = require('../..');

describe('aot', function() {
  let inputName = 'pkg/integers_lib_bg.wasm';

  describe('compile', function() {
    this.timeout(0);

    // AOT filename MUST ends with '.so'
    let outputName = 'pkg/integers_lib_bg.compile.so';

    beforeEach(function() {
      if (fs.existsSync(outputName)) {
        fs.unlinkSync(outputName); // Alt solution for fs.rmSync (added in v14.14)
      }
    });

    let cases = new Map([
      ['from filename', (s) => s],
      ['from bytearray', (s) => fs.readFileSync(s)],
    ]);

    cases.forEach(function(f, caseName) {
      it(caseName, function() {
        let vm = new ssvm.VM(f(inputName), {
          EnableAOT: true,
          args: process.argv,
          env: process.env,
          preopens: { '/': __dirname },
        });

        assert.ok(vm.Compile(outputName));
      });
    });

    afterEach(function() {
      if (fs.existsSync(outputName)) {
        fs.unlinkSync(outputName); // Alt solution for fs.rmSync (added in v14.14)
      }
    });
  });

  describe('run', function() {
    // AOT filename MUST ends with '.so'
    let aotName = 'pkg/integers_lib_bg.run.so';

    before(function() {
      this.timeout(0);

      if (fs.existsSync(aotName)) {
        fs.unlinkSync(aotName); // Alt solution for fs.rmSync (added in v14.14)
      }

      let vm = new ssvm.VM(inputName, {
        EnableAOT: true,
        args: process.argv,
        env: process.env,
        preopens: { '/': __dirname },
      });

      assert.ok(vm.Compile(aotName));
    });

    let cases = new Map([
      ['from filename', (s) => s],
      ['from bytearray', (s) => fs.readFileSync(s)],
    ]);

    cases.forEach(function(f, caseName) {
      it(caseName, function() {
        let vm = new ssvm.VM(f(aotName), {
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

    after(function() {
      if (fs.existsSync(aotName)) {
        fs.unlinkSync(aotName); // Alt solution for fs.rmSync (added in v14.14)
      }
    });
  });
});
