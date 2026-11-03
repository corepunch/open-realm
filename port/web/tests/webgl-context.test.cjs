/* Canvas alpha regression: opaque layers keep texture alpha, which the page would composite as holes (#660). */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

test('the browser WebGL context requests an opaque drawing buffer', () => {
  const source = fs.readFileSync(path.join(__dirname, '../../../renderer/r_main.c'), 'utf8');
  const create = source.indexOf('emscripten_webgl_create_context(');
  const init = source.lastIndexOf('emscripten_webgl_init_context_attributes(', create);
  assert.ok(init >= 0 && create > init);
  assert.match(source.slice(init, create), /attributes\.alpha = EM_FALSE;/);
});
