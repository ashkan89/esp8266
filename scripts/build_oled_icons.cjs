// Developer-only rasterizer; firmware builds use the committed bitmap header.
// npm install --prefix .pio/icon-tools --no-audit --no-fund @resvg/resvg-js@2.6.2
// python scripts/build_oled_icons.py
const fs = require('fs');
const path = require('path');
const { Resvg } = require('../.pio/icon-tools/node_modules/@resvg/resvg-js');
const root = path.join(__dirname, '..');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'assets/oled/lucide/source.json'), 'utf8'));
for (const name of manifest.icons) {
  const source = fs.readFileSync(path.join(root, 'assets/oled/lucide', name + '.svg'), 'utf8').replace(/currentColor/g, '#ffffff');
  for (const size of [12, 22]) {
    const png = new Resvg(source, { fitTo: { mode: 'width', value: size } }).render().asPng();
    fs.writeFileSync(path.join(root, '.pio/icon-tools', name + '-' + size + '.png'), png);
  }
}
