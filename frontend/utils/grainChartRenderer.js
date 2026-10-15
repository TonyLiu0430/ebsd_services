import { roundUpToNearestTen } from './chartScale.js'

const DEFAULT_PLOT_WIDTH = 185
const DEFAULT_PLOT_HEIGHT = 115
const PAD_L = 36
const PAD_T = 10
const PAD_B = 46
const PAD_R = 8

const DEFAULTS = Object.freeze({
  sample: [],
  golden: [],
  mode: 'cdf',
  histYMode: 'count',
  fixedXMin: null,
  fixedXMax: null,
  binCount: 20,
  binWidth: null,
  fixedYMax: null,
  autoBinCount: false,
  maxXTicks: 7,
  canvasAspectRatio: null,
  fontScale: 1,
  showXAxis: true,
  showYAxis: true,
  xAxisLabel: '粒徑 (µm)',
  sampleColor: '#DC2626',
  goldenColor: '#2563EB',
  sampleDash: '2.2,1.4',
  showAverage: false,
  averageLabel: 'Avg',
  averageSuffix: '',
})

function finite(values) {
  return (Array.isArray(values) ? values : []).map(Number).filter(Number.isFinite)
}

function escapeXml(value) {
  return String(value)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&apos;')
}

function compositeOnWhite(color, opacity) {
  const match = /^#([0-9a-f]{6})$/i.exec(String(color))
  if (!match) return color
  const channels = [0, 2, 4].map(offset => Number.parseInt(match[1].slice(offset, offset + 2), 16))
  const blended = channels.map(channel => Math.round(channel * opacity + 255 * (1 - opacity)))
  return `#${blended.map(channel => channel.toString(16).padStart(2, '0')).join('')}`
}

function attrs(values) {
  return Object.entries(values)
    .filter(([, value]) => value !== undefined && value !== null && value !== false && value !== '')
    .map(([name, value]) => `${name}="${escapeXml(value)}"`)
    .join(' ')
}

function histogram(values, bins, minValue, maxValue, requestedWidth, areaWeighted, ratio) {
  const result = Array.from({ length: bins }, () => 0)
  const width = requestedWidth ?? Math.max(maxValue - minValue, 1e-6) / bins
  let total = 0
  for (const raw of values) {
    const value = Number(raw)
    if (!Number.isFinite(value) || (areaWeighted && value <= 0)) continue
    const index = Math.min(bins - 1, Math.max(0, Math.floor((value - minValue) / Math.max(width, 1e-6))))
    const weight = areaWeighted ? Math.PI * (value / 2) ** 2 : 1
    result[index] += weight
    total += weight
  }
  return ratio && total > 0 ? result.map(value => value / total * 100) : result
}

function histogramYMax(maximum, mode, histYMode) {
  const rounded = roundUpToNearestTen(maximum)
  return mode === 'areaHist' || histYMode === 'ratio' ? Math.min(100, rounded) : rounded
}

function makeModel(input = {}) {
  const props = { ...DEFAULTS, ...input }
  const plotHeight = DEFAULT_PLOT_HEIGHT
  const fontScale = Math.max(0.8, Math.min(2.2, Number(props.fontScale) || 1))
  const padLeft = PAD_L + 5 * (fontScale - 1)
  const padRight = PAD_R + 6 * (fontScale - 1)
  const requestedAspectRatio = Number(props.canvasAspectRatio)
  const plotWidth = Number.isFinite(requestedAspectRatio) && requestedAspectRatio > 0
    ? Math.max(140, Math.min(520, requestedAspectRatio * (plotHeight + PAD_T + PAD_B) - padLeft - padRight))
    : DEFAULT_PLOT_WIDTH
  const sample = finite(props.sample)
  const golden = finite(props.golden)
  const all = [...sample, ...golden]
  const positive = all.filter(value => value > 0)
  const dataXMin = positive.length ? Math.floor(Math.min(...positive) * 10) / 10 : 0
  const cdfXMax = props.fixedXMax != null
    ? Number(props.fixedXMax)
    : Math.max(1, Math.ceil((all.length ? Math.max(...all) : 10) * 10) / 10)
  const histBinXMin = props.fixedXMin != null ? Number(props.fixedXMin) : dataXMin
  const histXMax = props.fixedXMax != null
    ? Number(props.fixedXMax)
    : Math.max(histBinXMin + 0.1, Math.ceil((all.length ? Math.max(...all) : 10) * 10) / 10)
  const requestedWidth = Number.isFinite(Number(props.binWidth)) && Number(props.binWidth) > 0
    ? Number(props.binWidth)
    : null
  const span = Math.max(histXMax - histBinXMin, 1e-6)
  const automaticBinCount = Math.max(5, Math.min(20, Math.round(Math.sqrt(Math.max(1, all.length)) * 2)))
  const binCount = requestedWidth
    ? Math.max(1, Math.min(80, Math.ceil(span / requestedWidth)))
    : props.autoBinCount
      ? automaticBinCount
      : Math.max(1, Math.floor(Number(props.binCount) || 16))
  const binWidth = requestedWidth ?? span / binCount
  const ratio = props.mode === 'areaHist' || (props.mode === 'hist' && props.histYMode === 'ratio')
  const sampleSeries = histogram(sample, binCount, histBinXMin, histXMax, requestedWidth, props.mode === 'areaHist', ratio)
  const goldenSeries = histogram(golden, binCount, histBinXMin, histXMax, requestedWidth, props.mode === 'areaHist', ratio)
  const seriesMax = Math.max(1, ...sampleSeries, ...goldenSeries)
  const yAxisMax = props.mode === 'cdf'
    ? 100
    : Number.isFinite(Number(props.fixedYMax)) && Number(props.fixedYMax) > 0
      ? Number(props.fixedYMax)
      : histogramYMax(seriesMax, props.mode, props.histYMode)

  return {
    props,
    sample,
    golden,
    sampleSeries,
    goldenSeries,
    seriesMax,
    yAxisMax,
    binCount,
    binWidth,
    histBinXMin,
    histXMax,
    chartXMin: props.mode === 'cdf' ? 0 : histBinXMin,
    chartXMax: props.mode === 'cdf' ? cdfXMax : histXMax,
    plotWidth,
    plotHeight,
    fontScale,
    padLeft,
    padRight,
  }
}

function scaleX(model, value) {
  const span = Math.max(model.chartXMax - model.chartXMin, 1e-6)
  return ((value - model.chartXMin) / span) * model.plotWidth
}

function histBoundary(model, index) {
  if (index <= 0) return model.histBinXMin
  if (index >= model.binCount) return model.histXMax
  return Math.min(model.histXMax, model.histBinXMin + model.binWidth * index)
}

function formatXTick(value) {
  if (Math.abs(value) < 1e-6) return '0'
  if (Math.abs(value) >= 100 || Number.isInteger(value)) return String(Math.round(value))
  return value.toFixed(1)
}

function formatYTick(value, model) {
  if (model.props.mode === 'areaHist' || model.props.histYMode === 'ratio') {
    return Number.isInteger(value) ? String(value) : value.toFixed(1)
  }
  return String(Math.round(value))
}

function niceTicks(minimum, maximum, maximumTicks = 6) {
  const span = Math.max(maximum - minimum, 1e-6)
  const rawStep = span / Math.max(1, maximumTicks - 1)
  const magnitude = 10 ** Math.floor(Math.log10(rawStep))
  const normalized = rawStep / magnitude
  const factor = normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 5 ? 5 : 10
  const step = factor * magnitude
  const ticks = [minimum]
  let value = Math.ceil((minimum + 1e-9) / step) * step
  while (value < maximum - 1e-9) {
    ticks.push(value)
    value += step
  }
  if (Math.abs(ticks[ticks.length - 1] - maximum) > 1e-9) ticks.push(maximum)
  return ticks
}

function yTicks(model) {
  if (model.props.mode === 'cdf') return [0, 25, 50, 75, 100]
  return niceTicks(0, model.yAxisMax, 6)
}

function xTicks(model) {
  if (model.props.mode !== 'cdf') {
    const maximumTicks = Math.max(2, Math.floor(Number(model.props.maxXTicks) || 7))
    if (model.binCount + 1 > maximumTicks) {
      return niceTicks(model.chartXMin, model.chartXMax, maximumTicks).map(value => ({
        x: scaleX(model, value),
        label: formatXTick(value),
        rotate: false,
      }))
    }
    const ticks = [{ x: scaleX(model, 0), label: '0', rotate: true }]
    for (let index = 0; index <= model.binCount; index++) {
      const value = histBoundary(model, index)
      if (Math.abs(value) < 1e-6) continue
      ticks.push({ x: scaleX(model, value), label: formatXTick(value), rotate: true })
    }
    return ticks
  }
  const step = (model.chartXMax - model.chartXMin) / 4
  return Array.from({ length: 5 }, (_, index) => {
    const value = model.chartXMin + step * index
    return { x: scaleX(model, value), label: formatXTick(value), rotate: false }
  })
}

function makeCdf(model, values) {
  if (!values.length) return ''
  const sorted = [...values].sort((a, b) => a - b)
  return sorted.map((value, index) => {
    const x = scaleX(model, value)
    const y = model.plotHeight - (index + 1) / sorted.length * model.plotHeight
    return `${x},${y}`
  }).join(' ')
}

function barRects(model, series, color, opacity, strokeWidth, dash = '', strokeColor = color) {
  const fills = []
  const strokes = []
  series.forEach((count, index) => {
    if (count <= 0) return
    const x = scaleX(model, histBoundary(model, index)) + 0.5
    const nextX = scaleX(model, histBoundary(model, index + 1))
    const y = model.plotHeight - count / model.yAxisMax * model.plotHeight
    const width = Math.max(1, nextX - scaleX(model, histBoundary(model, index)) - 1)
    const height = count / model.yAxisMax * model.plotHeight
    const geometry = { x, y, width, height }
    fills.push(`<rect ${attrs({ ...geometry, fill: color, 'fill-opacity': opacity, stroke: 'none' })}/>`)
    strokes.push(`<rect ${attrs({ ...geometry, fill: 'none', stroke: strokeColor, 'stroke-opacity': 0.95, 'stroke-width': strokeWidth, 'stroke-dasharray': dash })}/>`)
  })
  return { fills, strokes }
}

function histogramStepPath(model, series) {
  const first = series.findIndex(value => value > 0)
  if (first < 0) return ''
  let last = series.length - 1
  while (last > first && series[last] <= 0) last -= 1
  const yAt = index => model.plotHeight - series[index] / model.yAxisMax * model.plotHeight
  let path = `M ${scaleX(model, histBoundary(model, first))} ${model.plotHeight}`
  path += ` L ${scaleX(model, histBoundary(model, first))} ${yAt(first)}`
  for (let index = first; index <= last; index++) {
    const nextX = scaleX(model, histBoundary(model, index + 1))
    path += ` L ${nextX} ${yAt(index)}`
    if (index < last) path += ` L ${nextX} ${yAt(index + 1)}`
  }
  return `${path} L ${scaleX(model, histBoundary(model, last + 1))} ${model.plotHeight}`
}

export function getGrainChartMetrics(input = {}) {
  const model = makeModel(input)
  return { seriesMax: model.seriesMax, yAxisMax: model.yAxisMax }
}

export function sharedHistogramYMax(charts) {
  const models = charts.map(makeModel)
  const maximum = Math.max(1, ...models.map(model => model.seriesMax))
  const first = models[0]?.props ?? DEFAULTS
  return histogramYMax(maximum, first.mode, first.histYMode)
}

export function renderGrainChartSvg(input = {}) {
  const model = makeModel(input)
  const { props } = model
  const W = model.plotWidth
  const H = model.plotHeight
  const fontScale = model.fontScale
  const strokeScale = Math.min(1.6, Math.sqrt(fontScale))
  // Every matrix cell keeps the same canvas and plot rectangle. Axis
  // visibility must never change the exported image aspect ratio.
  const plotPadLeft = model.padLeft
  const plotPadBottom = PAD_B
  const viewWidth = W + plotPadLeft + model.padRight
  const viewHeight = H + PAD_T + plotPadBottom
  const lines = []

  for (const value of yTicks(model)) {
    const y = H - value / model.yAxisMax * H
    lines.push(`<line ${attrs({ x1: 0, y1: y, x2: W, y2: y, stroke: '#f0f0f0', 'stroke-width': strokeScale })}/>`)
  }
  if (props.showYAxis) lines.push(`<line x1="0" y1="0" x2="0" y2="${H}" stroke="#9ca3af" stroke-width="${strokeScale}"/>`)
  lines.push(`<line x1="0" y1="${H}" x2="${W}" y2="${H}" stroke="#9ca3af" stroke-width="${strokeScale}"/>`)

  if (props.showYAxis) {
    for (const value of yTicks(model)) {
      const y = H - value / model.yAxisMax * H
      lines.push(`<line ${attrs({ x1: -3, y1: y, x2: 0, y2: y, stroke: '#9ca3af', 'stroke-width': strokeScale })}/>`)
      lines.push(`<text ${attrs({ x: -5, y: y + 3.5 * fontScale, 'text-anchor': 'end', 'font-size': 9 * fontScale, fill: '#4b5563' })}>${escapeXml(formatYTick(value, model))}</text>`)
    }
    const yLabel = props.mode === 'cdf' ? 'Cumulative %' : props.mode === 'areaHist' ? 'Area %' : props.histYMode === 'ratio' ? 'Ratio %' : 'Counts'
    const yLabelFontSize = 9.5 * fontScale
    // Keep half the rotated glyph height plus a small safety margin inside
    // the SVG viewport. A fixed 6 px inset clipped the leading edge of
    // "Ratio %" once dense PPT layouts increased fontScale.
    const yLabelInset = yLabelFontSize / 2 + 2
    const labelX = -plotPadLeft + yLabelInset
    lines.push(`<text ${attrs({ x: labelX, y: H / 2, 'text-anchor': 'middle', 'font-size': yLabelFontSize, 'font-weight': 600, fill: '#374151', transform: `rotate(-90,${labelX},${H / 2})` })}>${escapeXml(yLabel)}</text>`)
  }

  if (props.showXAxis) {
    for (const tick of xTicks(model)) {
      lines.push(`<line ${attrs({ x1: tick.x, y1: H, x2: tick.x, y2: H + 3, stroke: '#9ca3af', 'stroke-width': strokeScale })}/>`)
      lines.push(`<text ${attrs({ x: tick.x, y: H + 14, 'text-anchor': tick.rotate ? 'end' : 'middle', 'font-size': (tick.rotate ? 7.5 : 9) * fontScale, fill: '#4b5563', transform: tick.rotate ? `rotate(-55,${tick.x},${H + 14})` : undefined })}>${escapeXml(tick.label)}</text>`)
    }
    lines.push(`<text ${attrs({ x: W / 2, y: H + plotPadBottom - 3, 'text-anchor': 'middle', 'font-size': 9.5 * fontScale, 'font-weight': 600, fill: '#374151' })}>${escapeXml(props.xAxisLabel)}</text>`)
  }

  if (props.mode === 'cdf') {
    const goldenPoints = makeCdf(model, model.golden)
    const samplePoints = makeCdf(model, model.sample)
    if (goldenPoints) lines.push(`<polyline ${attrs({ points: goldenPoints, fill: 'none', stroke: props.goldenColor, 'stroke-width': 1.8, 'stroke-linejoin': 'round' })}/>`)
    if (samplePoints) lines.push(`<polyline ${attrs({ points: samplePoints, fill: 'none', stroke: props.sampleColor, 'stroke-width': 1.8, 'stroke-dasharray': '5,2.5', 'stroke-linejoin': 'round' })}/>`)
  } else {
    const sampleOnly = model.sample.length > 0 && model.golden.length === 0
    const goldenOnly = model.golden.length > 0 && model.sample.length === 0
    const golden = barRects(
      model,
      model.goldenSeries,
      compositeOnWhite(props.goldenColor, goldenOnly ? 0.30 : 0.24),
      1,
      0.9 * strokeScale,
      '',
      props.goldenColor,
    )
    const sample = barRects(
      model,
      model.sampleSeries,
      compositeOnWhite(props.sampleColor, sampleOnly ? 0.18 : 0.10),
      1,
      1.15 * strokeScale,
      props.sampleDash,
      props.sampleColor,
    )
    // Opaque pale fills cover the grid. Golden is laid over sample so the
    // overlap stays blue instead of becoming a muddy purple stripe pattern.
    lines.push(...sample.fills, ...golden.fills, ...golden.strokes)
    if (model.golden.length) {
      const sampleOutline = histogramStepPath(model, model.sampleSeries)
      if (sampleOutline) {
        lines.push(`<path ${attrs({ d: sampleOutline, fill: 'none', stroke: props.sampleColor, 'stroke-width': 1.15 * strokeScale, 'stroke-dasharray': props.sampleDash })}/>`)
      }
    } else {
      lines.push(...sample.strokes)
    }
  }

  if (props.showAverage && model.sample.length) {
    const average = model.sample.reduce((sum, value) => sum + value, 0) / model.sample.length
    const averageX = Math.min(W, Math.max(0, scaleX(model, average)))
    const averageText = `${props.averageLabel} ${average.toFixed(2)}${props.averageSuffix}`
    const badgeWidth = Math.max(48, averageText.length * 4.7 + 8)
    const badgeX = W - badgeWidth - 3
    lines.push(`<path ${attrs({ d: `M ${averageX - 3.2} ${H + 1} L ${averageX + 3.2} ${H + 1} L ${averageX} ${H - 4.2} Z`, fill: props.sampleColor })}/>`)
    lines.push(`<rect ${attrs({ x: badgeX, y: 4, width: badgeWidth, height: 15, rx: 3, fill: '#ffffff', stroke: props.sampleColor, 'stroke-width': 0.8 })}/>`)
    lines.push(`<text ${attrs({ x: W - 7, y: 14.2, 'text-anchor': 'end', 'font-size': 8 * fontScale, 'font-weight': 700, fill: '#374151' })}>${escapeXml(averageText)}</text>`)
  }

  return `<svg xmlns="http://www.w3.org/2000/svg" width="${viewWidth}" height="${viewHeight}" viewBox="0 0 ${viewWidth} ${viewHeight}"><rect width="${viewWidth}" height="${viewHeight}" fill="#ffffff"/><g transform="translate(${plotPadLeft},${PAD_T})" font-family="Arial, sans-serif">${lines.join('')}</g></svg>`
}
