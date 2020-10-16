### Build and Run

Add `../node_modules/.bin/` to your path.

```
ssvmup build
cd pkg/
npm install ../..
cd -
mocha node/app.js
ssvmup clean
```
