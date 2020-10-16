#!/usr/bin/env bash
set -xe
ssvmup build
cd pkg
npm install ../..
cd -
mocha node/app.js
ssvmup clean
