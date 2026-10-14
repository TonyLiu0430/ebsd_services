import assert from 'node:assert/strict'
import test from 'node:test'

import { roundUpToNearestTen } from '../utils/chartScale.js'

test('rounds the page maximum to tens without adding headroom', () => {
  assert.equal(roundUpToNearestTen(0), 10)
  assert.equal(roundUpToNearestTen(9.9), 10)
  assert.equal(roundUpToNearestTen(50), 50)
  assert.equal(roundUpToNearestTen(50.1), 60)
  assert.equal(roundUpToNearestTen(60), 60)
  assert.equal(roundUpToNearestTen(60.1), 70)
})

test('supports the triangle chart fifty-percent floor', () => {
  assert.equal(roundUpToNearestTen(0, 50), 50)
  assert.equal(roundUpToNearestTen(49.9, 50), 50)
  assert.equal(roundUpToNearestTen(50, 50), 50)
  assert.equal(roundUpToNearestTen(50.1, 50), 60)
})

test('falls back safely for missing or invalid data', () => {
  assert.equal(roundUpToNearestTen(undefined), 10)
  assert.equal(roundUpToNearestTen(Number.NaN), 10)
  assert.equal(roundUpToNearestTen(Number.POSITIVE_INFINITY), 10)
})
