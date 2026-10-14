#!/usr/bin/env node

import { mkdir, writeFile } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'
import { renderGrainChartSvg } from '../utils/grainChartRenderer.js'

const outputPath = resolve(process.argv[2] ?? '../.temp/ppt-visual-review/frontend-histogram-reference.svg')
const svg = renderGrainChartSvg({
  sample: [0.35, 0.52, 0.71, 1.15, 1.42, 2.05],
  golden: [],
  mode: 'hist',
  histYMode: 'count',
  fixedXMin: 0,
  fixedXMax: 2.5,
  autoBinCount: true,
  fixedYMax: 10,
  showXAxis: true,
  showYAxis: true,
  xAxisLabel: 'KAM (°)',
  sampleColor: '#0ea5e9',
  sampleDash: '',
  showAverage: true,
  averageSuffix: '°',
})

await mkdir(dirname(outputPath), { recursive: true })
await writeFile(outputPath, `${svg}\n`)
console.log(outputPath)
