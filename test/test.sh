#!/usr/bin/env bash
set -xe
ssvmup build
cd pkg
npm install ../..
cd -
# XXX: workaround to prevent SSVM interpreter singleton from breaking
# mocha js
mocha js/test-integers.js
mocha js/test-aot.js
mocha js/test-aot-runtime.js
ssvmup clean
