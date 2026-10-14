import assert from 'node:assert/strict'
import test from 'node:test'

import {
  buildPositionMatrix,
  normalizeRelativePath,
  parseTargetVersion,
  resolveTargetFolder,
} from '../utils/positions.ts'

test('builds an arbitrary N by M matrix without named-size special cases', () => {
  for (const [columns, rows] of [[2, 7], [3, 3], [5, 4], [8, 9], [11, 6], [27, 1]]) {
    const lastColumn = columns <= 26 ? String.fromCharCode(64 + columns) : 'AA'
    const matrix = buildPositionMatrix(['A-1', `${lastColumn}-${rows}`])
    assert.equal(matrix.columnCount, columns)
    assert.equal(matrix.rowCount, rows)
    assert.equal(matrix.cellCount, columns * rows)
  }
})

test('recognises a whole Data09 tree and its numbered version folder', () => {
  const relativePath = normalizeRelativePath('Data09\\資料\\Data09-01\\A-1.crc')
  const folder = resolveTargetFolder(relativePath.split('/'))
  assert.equal(relativePath, 'Data09/資料/Data09-01/A-1.crc')
  assert.equal(folder, 'Data09-01')
  assert.deepEqual(parseTargetVersion(folder), {
    sample: 'Data09',
    versionKey: 'v1',
    versionLabel: 'Data09-1',
    versionNum: 1,
  })
})

test('uses a non-generic parent folder when no explicit version exists', () => {
  const parts = 'root/資料/Copper/A-1.ctf'.split('/')
  assert.equal(resolveTargetFolder(parts), 'Copper')
  assert.deepEqual(parseTargetVersion('Copper'), {
    sample: 'Copper',
    versionKey: 'v1',
    versionLabel: 'Copper-1',
    versionNum: 1,
  })
})
