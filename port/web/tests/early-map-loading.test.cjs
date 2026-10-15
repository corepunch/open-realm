/* Browser-only placeholder display, no game assets. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {makeEarlyMapLoading} = require('../site/early-map-loading.js');
test('placeholder initially hides and shows on signal', () => {
  const node = {hidden: true};
  const show = makeEarlyMapLoading({getElementById: () => node});
  show(true);
  assert.equal(node.hidden, false);
});
test('missing placeholder is harmless', () => {
  assert.doesNotThrow(() => makeEarlyMapLoading({getElementById: () => null})(true));
});
