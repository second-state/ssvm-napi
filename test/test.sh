#!/usr/bin/env bash
set -xe
rustwasmc build
cd pkg
npm install ../..
sed -i "s/require('ssvm')/require('wasmedge-core')/" integers_lib.js
cd -
mocha js
rustwasmc clean
