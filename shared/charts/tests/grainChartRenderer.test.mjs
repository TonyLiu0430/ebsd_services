import assert from 'node:assert/strict'
import test from 'node:test'

import { renderGrainChartSvg } from '../src/grainChartRenderer.js'

test('keeps the scaled Ratio label inside the left SVG edge', () => {
  const svg = renderGrainChartSvg({
    sample: [5, 15, 15, 25, 25, 25],
    mode: 'hist',
    histYMode: 'ratio',
    fixedXMin: 0,
    fixedXMax: 130,
    binWidth: 10,
    canvasAspectRatio: 2,
    fontScale: 1.7,
  })
  const group = svg.match(/<g transform="translate\(([^,]+),/)
  const label = svg.match(/<text [^>]+>Ratio %<\/text>/)

  assert.ok(group)
  assert.ok(label)
  const x = Number(label[0].match(/ x="([^"]+)"/)?.[1])
  const fontSize = Number(label[0].match(/ font-size="([^"]+)"/)?.[1])
  const absoluteCenter = Number(group[1]) + x
  assert.ok(absoluteCenter >= fontSize / 2 + 1)
})
