#!/bin/bash

INSTALL_SCRIPT=/tmp/install_wasmedge.sh
wget -O "$INSTALL_SCRIPT" https://github.com/second-state/WasmEdge-go/releases/download/v0.8.1/install_wasmedge.sh
sh "$INSTALL_SCRIPT" /usr/local
rm -f "$INSTALL_SCRIPT"
