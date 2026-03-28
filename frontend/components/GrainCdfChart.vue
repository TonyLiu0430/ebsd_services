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
}>(), {
  sample: () => [],
  golden: () => [],
  sampleLabel: 'Sample',
  goldenLabel: 'Golden',
  mode: 'cdf',
  fixedXMin: null,
  fixedXMax: null,
  binCount: 16,
})

const W = 185
const H = 115
const PAD_L = 36
const PAD_T = 10
const PAD_B = 32
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

const cdfXMin = computed(() => props.fixedXMin != null ? props.fixedXMin : dataXMin.value)
const cdfXMax = computed(() => {
  if (props.fixedXMax != null) return props.fixedXMax
  if (!finiteValues.value.length) return 10
  const maxVal = Math.max(...finiteValues.value)
  return Math.max(1, Math.ceil(maxVal * 10) / 10)
})

const histXMin = computed(() => props.fixedXMin != null ? props.fixedXMin : dataXMin.value)

const histXMax = computed(() => {
  if (props.fixedXMax != null) return props.fixedXMax
  if (!finiteValues.value.length) return 10
  const maxVal = Math.max(...finiteValues.value)
  return Math.max(histXMin.value + 0.1, Math.ceil(maxVal * 10) / 10)
})

const chartXMin = computed(() => (props.mode === 'cdf' ? cdfXMin.value : histXMin.value))
const chartXMax = computed(() => (props.mode === 'cdf' ? cdfXMax.value : histXMax.value))
const samplePoints = computed(() => makeCdf(props.sample, cdfXMin.value, cdfXMax.value))
const goldenPoints = computed(() => makeCdf(props.golden, cdfXMin.value, cdfXMax.value))

const HIST_BINS = computed(() => Math.max(1, Math.floor(props.binCount || 16)))

function makeHistogram(arr: number[], bins: number, minV: number, maxV: number): number[] {
  const safeBins = Math.max(1, bins)
  const counts = Array.from({ length: safeBins }, () => 0)
  if (!arr.length) return counts
  const span = Math.max(maxV - minV, 1e-6)
  for (const raw of arr) {
    const v = Number.isFinite(raw) ? raw : 0
    const pct = (v - minV) / span
    const idx = Math.min(safeBins - 1, Math.max(0, Math.floor(pct * safeBins)))
    counts[idx]++
  }
  return counts
}

function makeAreaWeightedHistogram(arr: number[], bins: number, minV: number, maxV: number): number[] {
  const safeBins = Math.max(1, bins)
  const weights = Array.from({ length: safeBins }, () => 0)
  if (!arr.length) return weights
  const span = Math.max(maxV - minV, 1e-6)
  let totalWeight = 0
  for (const raw of arr) {
    if (!Number.isFinite(raw) || raw <= 0) continue
    const pct = (raw - minV) / span
    const idx = Math.min(safeBins - 1, Math.max(0, Math.floor(pct * safeBins)))
    const weight = Math.PI * (raw / 2) * (raw / 2)
    weights[idx] += weight
    totalWeight += weight
  }
  if (totalWeight <= 0) return weights.map(() => 0)
  return weights.map((w) => (w / totalWeight) * 100)
}

const sampleSeries = computed(() => {
  if (props.mode === 'areaHist') return makeAreaWeightedHistogram(props.sample, HIST_BINS.value, histXMin.value, histXMax.value)
  return makeHistogram(props.sample, HIST_BINS.value, histXMin.value, histXMax.value)
})
const goldenSeries = computed(() => {
  if (props.mode === 'areaHist') return makeAreaWeightedHistogram(props.golden, HIST_BINS.value, histXMin.value, histXMax.value)
  return makeHistogram(props.golden, HIST_BINS.value, histXMin.value, histXMax.value)
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
  const binW = W / HIST_BINS.value
  const drawW = Math.max(1, binW - 1)
  return sampleSeries.value.map((count, i) => ({
    x: i * binW + 0.5,
    y: H - (count / yAxisMax.value) * H,
    w: drawW,
    h: (count / yAxisMax.value) * H,
  }))
})

const goldenBars = computed(() => {
  const binW = W / HIST_BINS.value
  const drawW = Math.max(1, binW - 1)
  return goldenSeries.value.map((count, i) => ({
    x: i * binW + 0.5,
    y: H - (count / yAxisMax.value) * H,
    w: drawW,
    h: (count / yAxisMax.value) * H,
  }))
})

const xTicks = computed(() => {
  const count = 4
  const step = (chartXMax.value - chartXMin.value) / count
  return Array.from({ length: count + 1 }, (_, i) => {
    const v = chartXMin.value + step * i
    return {
      x: ((v - chartXMin.value) / Math.max(chartXMax.value - chartXMin.value, 1e-6)) * W,
      label: v.toFixed(1),
    }
  })
})

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
        <text :x="t.x" :y="H + 13" text-anchor="middle" font-size="7.5" fill="#6b7280">{{ t.label }}</text>
      </g>
      <text :x="W / 2" :y="H + PAD_B - 4" text-anchor="middle" font-size="8" fill="#6b7280">粒徑 (μm)</text>

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
