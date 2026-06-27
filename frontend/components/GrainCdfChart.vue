<script setup lang="ts">
const props = withDefaults(defineProps<{
  sample: number[]
  golden: number[]
  sampleLabel?: string
  goldenLabel?: string
  mode?: 'cdf' | 'hist' | 'areaHist'
  fixedXMin?: number | null
  fixedXMax?: number | null
  binCount?: number
  binWidth?: number | null
}>(), {
  sample: () => [],
  golden: () => [],
  sampleLabel: 'Sample',
  goldenLabel: 'Golden',
  mode: 'cdf',
  fixedXMin: null,
  fixedXMax: null,
  binCount: 20,
  binWidth: null,
})

const W = 185
const H = 115
const PAD_L = 36
const PAD_T = 10
const PAD_B = 46
const PAD_R = 8

function makeCdf(arr: number[], xMin: number, xMax: number): string {
  if (!arr.length) return ''
  const sorted = [...arr].sort((a, b) => a - b)
  const n = sorted.length
  const pts: string[] = []
  for (let i = 0; i < n; i++) {
    const xPct = xMax > xMin ? (sorted[i] - xMin) / (xMax - xMin) : 0
    const yPct = (i + 1) / n
    const svgX = xPct * W
    const svgY = H - yPct * H
    pts.push(`${svgX},${svgY}`)
  }
  return pts.join(' ')
}

const finiteValues = computed(() => [...props.sample, ...props.golden].filter((v) => Number.isFinite(v)))
const positiveFiniteValues = computed(() => finiteValues.value.filter((v) => v > 0))

const dataXMin = computed(() => {
  if (!positiveFiniteValues.value.length) return 0
  const minVal = Math.min(...positiveFiniteValues.value)
  return Math.floor(minVal * 10) / 10
})

const cdfXMin = computed(() => 0)
const cdfXMax = computed(() => {
  if (props.fixedXMax != null) return props.fixedXMax
  if (!finiteValues.value.length) return 10
  const maxVal = Math.max(...finiteValues.value)
  return Math.max(1, Math.ceil(maxVal * 10) / 10)
})

const histBinXMin = computed(() => props.fixedXMin != null ? props.fixedXMin : dataXMin.value)
const histBaseXMax = computed(() => {
  if (props.fixedXMax != null) return props.fixedXMax
  if (!finiteValues.value.length) return 10
  const maxVal = Math.max(...finiteValues.value)
  return Math.max(histBinXMin.value + 0.1, Math.ceil(maxVal * 10) / 10)
})

const requestedBinWidth = computed(() => {
  if (props.binWidth == null || !Number.isFinite(props.binWidth) || props.binWidth <= 0) return null
  return props.binWidth
})

const HIST_BINS = computed(() => {
  if (!requestedBinWidth.value) return Math.max(1, Math.floor(props.binCount || 16))
  const span = Math.max(histBaseXMax.value - histBinXMin.value, 1e-6)
  return Math.max(1, Math.min(80, Math.ceil(span / requestedBinWidth.value)))
})

const histXMax = computed(() => {
  return histBaseXMax.value
})

const histDisplayXMin = computed(() => 0)
const chartXMin = computed(() => (props.mode === 'cdf' ? cdfXMin.value : histDisplayXMin.value))
const chartXMax = computed(() => (props.mode === 'cdf' ? cdfXMax.value : histXMax.value))
const samplePoints = computed(() => makeCdf(props.sample, cdfXMin.value, cdfXMax.value))
const goldenPoints = computed(() => makeCdf(props.golden, cdfXMin.value, cdfXMax.value))

const histBinWidth = computed(() =>
  requestedBinWidth.value ?? Math.max(histXMax.value - histBinXMin.value, 1e-6) / HIST_BINS.value,
)

function histBoundary(index: number): number {
  if (index <= 0) return histBinXMin.value
  if (index >= HIST_BINS.value) return histXMax.value
  return Math.min(histXMax.value, histBinXMin.value + histBinWidth.value * index)
}

function scaleX(v: number): number {
  const span = Math.max(chartXMax.value - chartXMin.value, 1e-6)
  return ((v - chartXMin.value) / span) * W
}

function makeHistogram(arr: number[], bins: number, minV: number, maxV: number): number[] {
  const safeBins = Math.max(1, bins)
  const counts = Array.from({ length: safeBins }, () => 0)
  if (!arr.length) return counts
  const width = requestedBinWidth.value ?? Math.max(maxV - minV, 1e-6) / safeBins
  for (const raw of arr) {
    const v = Number.isFinite(raw) ? raw : 0
    const idx = Math.min(safeBins - 1, Math.max(0, Math.floor((v - minV) / Math.max(width, 1e-6))))
    counts[idx]++
  }
  return counts
}

function makeAreaWeightedHistogram(arr: number[], bins: number, minV: number, maxV: number): number[] {
  const safeBins = Math.max(1, bins)
  const weights = Array.from({ length: safeBins }, () => 0)
  if (!arr.length) return weights
  const width = requestedBinWidth.value ?? Math.max(maxV - minV, 1e-6) / safeBins
  let totalWeight = 0
  for (const raw of arr) {
    if (!Number.isFinite(raw) || raw <= 0) continue
    const idx = Math.min(safeBins - 1, Math.max(0, Math.floor((raw - minV) / Math.max(width, 1e-6))))
    const weight = Math.PI * (raw / 2) * (raw / 2)
    weights[idx] += weight
    totalWeight += weight
  }
  if (totalWeight <= 0) return weights.map(() => 0)
  return weights.map((w) => (w / totalWeight) * 100)
}

const sampleSeries = computed(() => {
  if (props.mode === 'areaHist') return makeAreaWeightedHistogram(props.sample, HIST_BINS.value, histBinXMin.value, histXMax.value)
  return makeHistogram(props.sample, HIST_BINS.value, histBinXMin.value, histXMax.value)
})
const goldenSeries = computed(() => {
  if (props.mode === 'areaHist') return makeAreaWeightedHistogram(props.golden, HIST_BINS.value, histBinXMin.value, histXMax.value)
  return makeHistogram(props.golden, HIST_BINS.value, histBinXMin.value, histXMax.value)
})

const ySeriesMax = computed(() => Math.max(1, ...sampleSeries.value, ...goldenSeries.value))
const yAxisMax = computed(() => {
  if (props.mode === 'cdf') return 100
  if (props.mode === 'areaHist') {
    const step = Math.max(1, Math.ceil(ySeriesMax.value / 4))
    return step * 4
  }
  const step = Math.max(1, Math.ceil(ySeriesMax.value / 4))
  return step * 4
})

const yTicks = computed(() => {
  if (props.mode === 'cdf') return [0, 25, 50, 75, 100]
  const step = yAxisMax.value / 4
  return [0, step, step * 2, step * 3, step * 4]
})

const sampleBars = computed(() => {
  return sampleSeries.value.map((count, i) => ({
    x: scaleX(histBoundary(i)) + 0.5,
    y: H - (count / yAxisMax.value) * H,
    w: Math.max(1, scaleX(histBoundary(i + 1)) - scaleX(histBoundary(i)) - 1),
    h: (count / yAxisMax.value) * H,
  }))
})

const goldenBars = computed(() => {
  return goldenSeries.value.map((count, i) => ({
    x: scaleX(histBoundary(i)) + 0.5,
    y: H - (count / yAxisMax.value) * H,
    w: Math.max(1, scaleX(histBoundary(i + 1)) - scaleX(histBoundary(i)) - 1),
    h: (count / yAxisMax.value) * H,
  }))
})

const xTicks = computed(() => {
  if (props.mode !== 'cdf') {
    const ticks = [
      {
        x: scaleX(0),
        label: '0',
        rotate: true,
      },
    ]
    for (let i = 0; i <= HIST_BINS.value; i++) {
      const v = histBoundary(i)
      if (Math.abs(v) < 1e-6) continue
      ticks.push({
        x: scaleX(v),
        label: formatXTick(v),
        rotate: true,
      })
    }
    return ticks
  }

  const count = 4
  const step = (chartXMax.value - chartXMin.value) / count
  return Array.from({ length: count + 1 }, (_, i) => {
    const v = chartXMin.value + step * i
    return {
      x: scaleX(v),
      label: formatXTick(v),
      rotate: false,
    }
  })
})

function formatXTick(v: number) {
  if (Math.abs(v) < 1e-6) return '0'
  if (Math.abs(v) >= 100 || Number.isInteger(v)) return String(Math.round(v))
  return v.toFixed(1)
}

function formatYTick(v: number) {
  if (props.mode === 'areaHist') return Number.isInteger(v) ? String(v) : v.toFixed(1)
  return String(Math.round(v))
}

const yAxisLabel = computed(() => {
  if (props.mode === 'cdf') return '累積 %'
  if (props.mode === 'areaHist') return '面積占比 %'
  return 'Counts'
})
</script>

<template>
  <svg :viewBox="`0 0 ${W + PAD_L + PAD_R} ${H + PAD_T + PAD_B}`" class="cdf-svg">
    <g :transform="`translate(${PAD_L},${PAD_T})`">
      <g v-for="v in yTicks" :key="'grid-'+v">
        <line :x1="0" :y1="H - v / yAxisMax * H" :x2="W" :y2="H - v / yAxisMax * H" stroke="#f0f0f0" stroke-width="1" />
      </g>

      <line x1="0" y1="0" x2="0" :y2="H" stroke="#9ca3af" stroke-width="1" />
      <line :x1="0" :y1="H" :x2="W" :y2="H" stroke="#9ca3af" stroke-width="1" />

      <g v-for="v in yTicks" :key="'y'+v">
        <line :x1="-3" :y1="H - v / yAxisMax * H" x2="0" :y2="H - v / yAxisMax * H" stroke="#9ca3af" stroke-width="1" />
        <text :x="-5" :y="H - v / yAxisMax * H + 3.5" text-anchor="end" font-size="8" fill="#6b7280">{{ formatYTick(v) }}</text>
      </g>
      <text :x="-PAD_L + 6" :y="H / 2" text-anchor="middle" font-size="8" fill="#6b7280" :transform="`rotate(-90,${-PAD_L + 6},${H / 2})`">{{ yAxisLabel }}</text>

      <g v-for="t in xTicks" :key="'x'+t.label">
        <line :x1="t.x" :y1="H" :x2="t.x" :y2="H + 3" stroke="#9ca3af" stroke-width="1" />
        <text
          :x="t.x"
          :y="H + 13"
          :text-anchor="t.rotate ? 'end' : 'middle'"
          :font-size="t.rotate ? 6.5 : 7.5"
          fill="#6b7280"
          :transform="t.rotate ? `rotate(-55,${t.x},${H + 13})` : undefined"
        >{{ t.label }}</text>
      </g>
      <text :x="W / 2" :y="H + PAD_B - 3" text-anchor="middle" font-size="8" fill="#6b7280">粒徑 (μm)</text>

      <template v-if="props.mode === 'cdf'">
        <polyline v-if="goldenPoints" :points="goldenPoints" fill="none" stroke="#F59E0B" stroke-width="1.8" stroke-dasharray="5,2.5" stroke-linejoin="round" />
        <polyline v-if="samplePoints" :points="samplePoints" fill="none" stroke="#3B82F6" stroke-width="1.8" stroke-linejoin="round" />
      </template>
      <template v-else>
        <g class="hist-fill-layer">
          <rect v-for="(b, i) in goldenBars" :key="'golden-fill-'+i" :x="b.x" :y="b.y" :width="b.w" :height="b.h" fill="#F59E0B" fill-opacity="0.28" stroke="none" />
          <rect v-for="(b, i) in sampleBars" :key="'sample-fill-'+i" :x="b.x" :y="b.y" :width="b.w" :height="b.h" fill="#0f766e" fill-opacity="0.28" stroke="none" />
        </g>
        <g class="hist-stroke-layer">
          <rect v-for="(b, i) in goldenBars" :key="'golden-stroke-'+i" :x="b.x" :y="b.y" :width="b.w" :height="b.h" fill="none" stroke="#F59E0B" stroke-opacity="0.95" stroke-width="0.9" stroke-dasharray="2.2,1.4" />
          <rect v-for="(b, i) in sampleBars" :key="'sample-stroke-'+i" :x="b.x" :y="b.y" :width="b.w" :height="b.h" fill="none" stroke="#0f766e" stroke-opacity="0.95" stroke-width="0.9" />
        </g>
      </template>
    </g>
  </svg>
</template>

<style scoped>
.cdf-svg {
  width: 100%;
  height: auto;
  display: block;
}
</style>
