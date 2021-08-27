#!/usr/bin/env bash
ARCH=$1
npm run release
rm -rf wasmedge
mkdir wasmedge
cp build/Release/wasmedge.node wasmedge
strip wasmedge/wasmedge.node
tar zcvf wasmedge-linux-${ARCH}.tar.gz wasmedge
