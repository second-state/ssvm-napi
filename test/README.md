### Build and Run

Add `../node_modules/.bin/` to your path.

```
ssvmup build
cd pkg/
npm install ../..
cd -
mocha js
ssvmup clean
```
