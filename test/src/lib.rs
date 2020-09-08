use wasm_bindgen::prelude::*;
use num_integer::lcm;

#[wasm_bindgen]
pub fn lcm_s32(a: i32, b: i32) -> i32 {
  let r = lcm(a, b);
  return r;
}
