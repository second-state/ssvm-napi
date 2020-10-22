use wasm_bindgen::prelude::*;
use num_integer::lcm;

#[wasm_bindgen]
pub fn lcm_s32(a: i32, b: i32) -> i32 {
  let r = lcm(a, b);
  return r;
}

#[wasm_bindgen]
pub fn lcm_u32(a: u32, b: u32) -> u32 {
  let r = lcm(a, b);
  return r;
}

#[wasm_bindgen]
pub fn lcm_s64(a: i64, b: i64) -> i64 {
  let r = lcm(a, b);
  return r;
}

#[wasm_bindgen]
pub fn lcm_u64(a: u64, b: u64) -> u64 {
  let r = lcm(a, b);
  return r;
}
