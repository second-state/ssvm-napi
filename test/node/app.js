const assert = require('assert');
const { lcm_s32 } = require('../pkg/integers_lib.js');

describe('s32', function() {
  it('lcm', function() {
    assert.equal(lcm_s32(123, 1011), 41451);
  });
  it('lcm s32 overflow', function() {
    assert.notEqual(lcm_s32(2147483647, 2), 4294967294);
  });
});
