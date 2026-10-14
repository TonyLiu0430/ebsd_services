#!/usr/bin/env node

import { stdin, stdout } from 'node:process'
import { renderGrainChartSvg, sharedHistogramYMax } from '../utils/grainChartRenderer.js'

let source = ''
stdin.setEncoding('utf8')
for await (const chunk of stdin) source += chunk

const request = JSON.parse(source || '{}')
const charts = Array.isArray(request.charts) ? request.charts : []
if (!charts.length) throw new Error('charts must contain at least one chart configuration')

const finiteValues = charts.flatMap(chart => [...(chart.sample ?? []), ...(chart.golden ?? [])])
  .map(Number)
  .filter(Number.isFinite)
const sharedXMax = request.sharedXMax
  ? Math.max(1, Math.ceil((finiteValues.length ? Math.max(...finiteValues) : 10) * 10) / 10)
  : null
const chartsWithSharedX = sharedXMax == null
  ? charts
  : charts.map(chart => ({ ...chart, fixedXMax: sharedXMax }))
const sharedYMax = request.sharedYMax ? sharedHistogramYMax(chartsWithSharedX) : null
const rendered = chartsWithSharedX.map((chart) => {
  const props = sharedYMax == null ? chart : { ...chart, fixedYMax: sharedYMax }
  return { svg: renderGrainChartSvg(props), yAxisMax: props.fixedYMax ?? null }
})

stdout.write(JSON.stringify({ charts: rendered, sharedXMax, sharedYMax }))
