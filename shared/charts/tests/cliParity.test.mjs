import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { closeSync, mkdtempSync, openSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import test from 'node:test'
import { fileURLToPath } from 'node:url'

import { renderGrainCharts } from '../src/batchRenderer.js'
import { renderGrainChartSvg } from '../src/index.js'

const cli = fileURLToPath(new URL('../bin/render-grain-charts.mjs', import.meta.url))

test('CLI output matches the shared renderer for all supported distribution modes', () => {
  const request = {
    sharedXMax: true,
    sharedYMax: true,
    charts: [
      { sample: [2, 4, 8], golden: [3, 6, 9], mode: 'cdf' },
      { sample: [2, 4, 8], golden: [3, 6, 9], mode: 'hist', histYMode: 'count', binWidth: 2 },
      { sample: [2, 4, 8], golden: [3, 6, 9], mode: 'hist', histYMode: 'ratio', binWidth: 2 },
      { sample: [2, 4, 8], golden: [3, 6, 9], mode: 'areaHist', histYMode: 'ratio', binWidth: 2 },
    ],
  }

  const directory = mkdtempSync(join(tmpdir(), 'ebsd-chart-cli-'))
  const inputPath = join(directory, 'request.json')
  const outputPath = join(directory, 'response.json')
  writeFileSync(inputPath, JSON.stringify(request))
  let input
  let output
  try {
    input = openSync(inputPath, 'r')
    output = openSync(outputPath, 'w')
    const cliProcess = spawnSync(process.execPath, [cli], {
      stdio: [input, output, 'pipe'],
      encoding: 'utf8',
    })
    closeSync(input)
    closeSync(output)
    input = undefined
    output = undefined

    assert.equal(cliProcess.status, 0, cliProcess.stderr)
    const cliResult = JSON.parse(readFileSync(outputPath, 'utf8'))
    assert.deepEqual(cliResult, renderGrainCharts(request))
    cliResult.charts.forEach((rendered, index) => {
      const props = {
        ...request.charts[index],
        ...(cliResult.sharedXMax == null ? {} : { fixedXMax: cliResult.sharedXMax }),
        ...(cliResult.sharedYMax == null ? {} : { fixedYMax: cliResult.sharedYMax }),
      }
      assert.equal(rendered.svg, renderGrainChartSvg(props))
    })
  } finally {
    if (input !== undefined) closeSync(input)
    if (output !== undefined) closeSync(output)
    rmSync(directory, { recursive: true, force: true })
  }
})
