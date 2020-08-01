#!/usr/bin/env bash
ARCH=$1
CXX=g++-9 node-pre-gyp install --fallback-to-build --update-binary
rm -rf ssvm
mkdir ssvm
cp build/Release/ssvm.node ssvm
strip ssvm/ssvm.node
tar zcvf ssvm-linux-${ARCH}.tar.gz ssvm
