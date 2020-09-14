const assert = require('assert');
const { lcm_s32, lcm_u32, lcm_s64, lcm_u64 } = require('../pkg/integers_lib.js');

describe('s32', function() {
  it('lcm', function() {
    assert.equal(lcm_s32(123, 1011), 41451);
  });
  it('lcm s32 overflow', function() {
    assert.notEqual(lcm_s32(2147483647, 2), 4294967294);
  });
});

describe('u32', function() {
  it('lcm', function() {
    assert.equal(lcm_u32(123, 1011), 41451);
  });
  it('lcm s32 overflow', function() {
    assert.equal(lcm_u32(2147483647, 2), 4294967294);
  });
  it('lcm u32 overflow', function() {
    assert.notEqual(lcm_u32(4294967295, 2), 8589934590);
  });
});

describe('s64', function() {
  it('lcm', function() {
    assert.equal(lcm_s64(123, 1011), 41451);
  });
  it('lcm s64 overflow', function() {
    assert.notEqual(lcm_s64(9223372036854775807, 2), 18446744073709551614);
  });
});

describe('u64', function() {
  it('lcm', function() {
    assert.equal(lcm_u64(123, 1011), 41451);
  });
  it('lcm s64 overflow', function() {
    assert.equal(lcm_u64(9223372036854775807, 2), 18446744073709551614);
  });
  it('lcm u64 overflow', function() {
    assert.notEqual(lcm_u64(18446744073709551615, 2), 36893488147419103230);
  });
});
