# How to run wasm applications with ssvm-napi (General Wasm32-wasi and interpreter mode)

## Environment Setup for Rust, Nodejs, and ssvmup

```bash
$ sudo apt-get update
$ sudo apt-get -y upgrade
$ sudo apt install build-essential curl wget git vim libboost-all-dev

$ curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
$ source $HOME/.cargo/env
$ cargo install cargo-wasi

$ curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.35.3/install.sh | bash

$ export NVM_DIR="$HOME/.nvm"
$ [ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"
$ [ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"

$ nvm install v14.2.0
$ nvm use v14.2.0

$ npm i -g ssvmup
```

## Example 1. RSA Key Pair Application

### Create a new rust project

```bash
cargo new rsa-example
cd rsa-example
```

### Modify the cargo config file

Replace the content of `Cargo.toml` file with the following code section:

```toml
[package]
name = "rsa_example"
version = "0.1.0"
authors = ["ubuntu"]
edition = "2018"
# See more keys and their definitions at https://doc.rust-lang.org/cargo/reference/manifest.html
[dependencies]
rand = "0.7.3"
rsa = { version = "0.2.0", features = ["serde1"] }
serde = { version = "1.0.110", features = ["derive"] }
serde_json = "1.0.53"
clap = "2.33.1"
```

### Write Rust code

Below is the entire content of the `src/main.rs` file.

```rust
use clap::{Arg, App, SubCommand};
use rsa::{PublicKey, RSAPublicKey, RSAPrivateKey, PaddingScheme};
use rand::rngs::OsRng;
use serde::{Serialize, Deserialize};
#[derive(Serialize, Deserialize)]
struct RSAKeyPair {
  rsa_private_key: RSAPrivateKey,
  rsa_public_key: RSAPublicKey
}
pub fn generate_key_pair (bits: i32) -> String {
  let mut rng = OsRng;
  let private_key = RSAPrivateKey::new(&mut rng, bits as usize).expect("failed to generate a key");
  let public_key = private_key.to_public_key();
  let key_pair = RSAKeyPair {rsa_private_key: private_key, rsa_public_key: public_key};
  return serde_json::to_string(&key_pair).unwrap();
}
pub fn decrypt (pk: &str, data: &[u8]) -> Vec<u8> {
  let private_key: RSAPrivateKey = serde_json::from_str(pk).unwrap();
  return private_key.decrypt(PaddingScheme::PKCS1v15, data).expect("failed to decrypt");
}
pub fn encrypt (pk: &str, data: &[u8]) -> Vec<u8> {
  let mut rng = OsRng;
  let public_key: RSAPublicKey = serde_json::from_str(pk).unwrap();
  return public_key.encrypt(&mut rng, PaddingScheme::PKCS1v15, data).expect("failed to encrypt");
}
fn main() {
  let matches = App::new("RSA Example")
    .subcommand(SubCommand::with_name("generate_key_pair")
                .arg(Arg::with_name("bits")
                     .short("b")
                     .long("bits")
                     .value_name("BITS")
                     .index(1)
                     ))
    .subcommand(SubCommand::with_name("decrypt")
                .arg(Arg::with_name("private_key")
                     .short("k")
                     .long("key")
                     .value_name("PRIVATE_KEY_JSON")
                     .required(true)
                     .index(1)
                     )
                .arg(Arg::with_name("data")
                     .short("d")
                     .long("data")
                     .value_name("DATA_JSON")
                     .required(true)
                     .index(2)
                     ))
    .subcommand(SubCommand::with_name("encrypt")
                .arg(Arg::with_name("public_key")
                     .short("k")
                     .long("key")
                     .value_name("PUBLIC_KEY_JSON")
                     .required(true)
                     .index(1)
                     )
                .arg(Arg::with_name("data")
                     .short("d")
                     .long("data")
                     .value_name("DATA_JSON")
                     .required(true)
                     .index(2)
                     ))
    .get_matches();
  if let Some(matches) = matches.subcommand_matches("generate_key_pair") {
    let bits = matches
      .value_of("bits")
      .and_then(|x| x.parse::<i32>().ok())
      .unwrap_or(2048);
    println!("{}", generate_key_pair(bits));
  } else if let Some(matches) = matches.subcommand_matches("encrypt") {
    let pk = matches
      .value_of("public_key")
      .unwrap();
    let data = matches
      .value_of("data")
      .unwrap();
    println!("data:'{}'", data);
    let data_bytes = data.as_bytes();
    let result = encrypt(pk, &data_bytes);
    let result_json = serde_json::to_string(&result).unwrap();
    println!("encrypt:'{}'", result_json);
  } else if let Some(matches) = matches.subcommand_matches("decrypt") {
    let pk = matches
      .value_of("private_key")
      .unwrap();
    let data = matches
      .value_of("data")
      .and_then(|x| serde_json::from_str::<Vec<u8>>(x).ok())
      .expect("failed to decode json string");
    println!("data:'{}'", serde_json::to_string(&data).unwrap());
    let result = decrypt(pk, &data);
    let result_utf8 = String::from_utf8(result).expect("failed to decode utf8 bytes");
    println!("decrypt:'{}'", result_utf8);
  }
}
```

### Build the WASM bytecode with cargo wasm32-wasi backend

```bash
cargo wasi build --release
```

After building, our target wasm file is located at `target/wasm32-wasi/release/rsa-example.wasm`.

### Install SSVM addon for your application

```bash
npm install ssvm
```

or if you want to build from source:

```bash
export CXX=g++-9
npm install --build-from-source https://github.com/second-state/ssvm-napi
```

### Use SSVM addon
After installing the SSVM addon, we could now interact with `rsa_example.wasm` generated by wasm32-wasi backend in Node.js.

- Create a new folder at any path you want. (e.g. `mkdir application`)
- Copy `rsa_example.wasm` into your application directory. (e.g. `cp rsa_example.wasm <path_to_your_application_folder>`)
- Create js file `main.js` and `lib.js` with the following content:

#### The entire content of `main.js`:

```javascript
const { generate_key_pair } = require('./lib.js');

console.log( "Generate Key Pair:" );
generate_key_pair(2048);
```

#### The entire content of `lib.js`:

```javascript
let vm;
/**
* @param {number} bits
* @returns {string}
*/
module.exports.generate_key_pair = function(bits) {
	return vm.Run("_start", "generate_key_pair", bits);
};

const ssvm = require('ssvm');
const path = require('path').join(__dirname, 'target/wasm32-wasi/release/rsa_example.wasm');
const wasi = {};
vm = new ssvm.VM(path, {'DisableWasmBindgen': true});
```

### Execute and check results

Please notice that ssvm.js is running in interpreter mode. It will take a few minutes to finish the key pair generation.

```bash
$ node main.js

Generate Key Pair:
{"rsa_private_key":{"n":[591866047,3646456190,683580725,1427200543,4247995072,500544554,2783460461,2044837728,3657133649,839614601,1366554736,342820942,2567289221,3792739598,202501138,687907030,324424905,2387064263,1062092653,200082885,3724055104,2871885927,470123699,893578609,2070017645,44764614,1283617512,3839445103,1606763971,2286062959,3810374226,990532102,2877925453,1862771874,2184162723,1083694098,2471167616,682413090,1669687240,1373036494,4136688316,1180152073,1466823969,3950500956,934488405,1693521828,2920641479,3396653153,1649125037,4250071160,3676951862,1751903411,3815821406,3866629890,1225647653,3779985582,1405390350,3684520707,1260106738,4080548059,272292883,890142985,786113276,2550702975],"e":[65537],"d":[3026463825,1951726268,3839375009,4168548555,4119132275,2946489049,4012674955,1112306966,419185618,3812081588,1180118620,2389576542,1027921107,2215376910,1801205216,995901305,601406209,1928490983,3004320859,2689699196,231295845,655787144,2718803763,1472287853,1259384482,669953827,1487230036,599441423,3291677709,2334476075,2404651633,1693980895,1460032429,2417404483,4248023530,1245109238,4265382732,1452035415,2924166731,3141591445,1600081660,2713831492,229424737,980790443,1201272064,593782960,4058636945,2775219766,3694057915,1886157117,3680937601,2712456638,784502068,2831045837,479572325,3975544270,2968789016,540211203,3419887960,3653599896,838401459,2043115248,2812897056,1757707062],"primes":[[1540010515,4138173370,557034346,2132792756,4032156372,4132260196,3636937683,631643313,4056260675,3849187394,2366346974,3950781792,3252579850,3443226979,4260319003,101244397,679176040,3627309906,901677978,1677073909,2204600615,3047177361,4086558791,529230390,3111196212,1113082630,2191658858,4055677562,2357190088,705595751,2307378750,3270393623],[2523161637,1610932469,1518819977,3610394985,165156375,1047124373,307187713,4024799639,2116079076,251392071,2441977467,3445554252,3181048146,3134604734,772034642,1560740157,1756321009,994456774,3454630647,1871605693,3867568860,2083016636,2171881534,3096070613,2524934301,2801276133,659354892,3330868278,1326322834,3608710730,29339917,3349806513]]},"rsa_public_key":{"n":[591866047,3646456190,683580725,1427200543,4247995072,500544554,2783460461,2044837728,3657133649,839614601,1366554736,342820942,2567289221,3792739598,202501138,687907030,324424905,2387064263,1062092653,200082885,3724055104,2871885927,470123699,893578609,2070017645,44764614,1283617512,3839445103,1606763971,2286062959,3810374226,990532102,2877925453,1862771874,2184162723,1083694098,2471167616,682413090,1669687240,1373036494,4136688316,1180152073,1466823969,3950500956,934488405,1693521828,2920641479,3396653153,1649125037,4250071160,3676951862,1751903411,3815821406,3866629890,1225647653,3779985582,1405390350,3684520707,1260106738,4080548059,272292883,890142985,786113276,2550702975],"e":[65537]}}
```

