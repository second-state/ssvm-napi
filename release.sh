CXX=g++-9 node-pre-gyp install --fallback-to-build --update-binary
rm -rf ssvm
mkdir ssvm
cp build/Release/ssvm.node ssvm
strip ssvm/ssvm.node
tar zcvf ssvm-linux-x64.tar.gz ssvm
