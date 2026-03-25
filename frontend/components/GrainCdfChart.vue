<script setup lang="ts">
/**
 * Grain size chart.
 * Supports CDF line mode and histogram mode.
 */

const props = withDefaults(defineProps<{
  sample: number[]
  golden: number[]
  sampleLabel?: string
  goldenLabel?: string
  mode?: 'cdf' | 'hist' | 'area-hist'
}>(), {
  sample: () => [],
  golden: () => [],
  sampleLabel: 'Sample',
  goldenLabel: 'Golden',
  mode: 'cdf',
})

// SVG layout constants
const W = 185   // inner width
const H = 115   // inner height
const PAD_L = 36
const PAD_T = 10
const PAD_B = 32
const PAD_R = 8

function makeCdf(arr: number[], xMin: number, xMax: number): string {
  if (!arr.length) return ''
  const sorted = [...arr].sort((a, b) => a - b)
  const n = sorted.length
  const pts: string[] = []
  // start at 0
  for (let i = 0; i < n; i++) {
    const xPct = xMax > xMin ? (sorted[i] - xMin) / (xMax - xMin) : 0
    const yPct = (i + 1) / n
    const svgX = xPct * W
    const svgY = H - yPct * H
    pts.push(`${svgX},${svgY}`)
  }
  return pts.join(' ')
}

const finiteValues = computed(() =>
  [...props.sample, ...props.golden].filter((v) => Number.isFinite(v)),
)

const positiveFiniteValues = computed(() =>
  finiteValues.value.filter((v) => v > 0),
)

const cdfXMin = computed(() => 0)
const cdfXMax = computed(() => {
  if (!finiteValues.value.length) return 10
  const maxVal = Math.max(...finiteValues.value)
  return Math.max(1, Math.ceil(maxVal * 10) / 10)
})

const histXMin = computed(() => {
  if (!positiveFiniteValues.value.length) return 0
  const minVal = Math.min(...positiveFiniteValues.value)
  return Math.floor(minVal * 10) / 10
})

const histXMax = computed(() => {
  if (!finiteValues.value.length) return 10
  const maxVal = Math.max(...finiteValues.value)
  return Math.max(histXMin.value + 0.1, Math.ceil(maxVal * 10) / 10)
})

const chartXMin = computed(() => (props.mode === 'cdf' ? cdfXMin.value : histXMin.value))
const chartXMax = computed(() => (props.mode === 'cdf' ? cdfXMax.value : histXMax.value))

const samplePoints = computed(() => makeCdf(props.sample, cdfXMin.value, cdfXMax.value))
const goldenPoints = computed(() => makeCdf(props.golden, cdfXMin.value, cdfXMax.value))

const HIST_BINS = 16
function equivalentAreaWeight(diameter: number): number {
  const radius = diameter / 2
  return Math.PI * radius * radius
}

function makeHistogram(
  arr: number[],
  bins: number,
  minV: number,
  maxV: number,
  weightFn: (value: number) => number = () => 1,
): number[] {
  const safeBins = Math.max(1, bins)
  const counts = Array.from({ length: safeBins }, () => 0)
  if (!arr.length) return counts
  const span = Math.max(maxV - minV, 1e-6)
  for (const raw of arr) {
    if (!Number.isFinite(raw) || raw <= 0) continue
    const v = raw
    const pct = (v - minV) / span
    const idx = Math.min(safeBins - 1, Math.max(0, Math.floor(pct * safeBins)))
    counts[idx] += weightFn(v)
  }
  return counts
}

function normalizeToPercent(values: number[]): number[] {
  const total = values.reduce((sum, value) => sum + value, 0)
  if (total <= 0) return values.map(() => 0)
  return values.map((value) => (value / total) * 100)
}

function niceStep(target: number): number {
  if (target <= 0) return 1
  const magnitude = 10 ** Math.floor(Math.log10(target))
  const normalized = target / magnitude
  if (normalized <= 1) return magnitude
  if (normalized <= 2) return 2 * magnitude
  if (normalized <= 2.5) return 2.5 * magnitude
  if (normalized <= 5) return 5 * magnitude
  return 10 * magnitude
}

const sampleHist = computed(() => makeHistogram(props.sample, HIST_BINS, histXMin.value, histXMax.value))
const goldenHist = computed(() => makeHistogram(props.golden, HIST_BINS, histXMin.value, histXMax.value))
const sampleAreaHist = computed(() =>
  normalizeToPercent(
    makeHistogram(props.sample, HIST_BINS, histXMin.value, histXMax.value, equivalentAreaWeight),
  ),
)
const goldenAreaHist = computed(() =>
  normalizeToPercent(
    makeHistogram(props.golden, HIST_BINS, histXMin.value, histXMax.value, equivalentAreaWeight),
  ),
)
const activeSampleHist = computed(() => (props.mode === 'area-hist' ? sampleAreaHist.value : sampleHist.value))
const activeGoldenHist = computed(() => (props.mode === 'area-hist' ? goldenAreaHist.value : goldenHist.value))
const yCountMax = computed(() => Math.max(0, ...activeSampleHist.value, ...activeGoldenHist.value))
const yAxisMax = computed(() => {
  if (props.mode === 'cdf') return 100
  if (props.mode === 'area-hist') {
    if (yCountMax.value <= 0) return 100
    return niceStep(yCountMax.value / 4) * 4
  }
  const step = Math.max(1, Math.ceil(yCountMax.value / 4))
  return step * 4
})

const yTicks = computed(() => {
  if (props.mode === 'cdf') return [0, 25, 50, 75, 100]
  const step = yAxisMax.value / 4
  return [0, step, step * 2, step * 3, step * 4]
})

function formatYAxisTick(value: number): string {
  if (props.mode !== 'area-hist') return String(Math.round(value))
  if (value >= 10) return value.toFixed(0)
  if (value >= 1) return value.toFixed(1)
  return value.toFixed(2)
}

const yAxisLabel = computed(() => {
  if (props.mode === 'cdf') return '累積 %'
  if (props.mode === 'area-hist') return '面積占比 %'
  return 'Counts'
})

const sampleBars = computed(() => {
  const binW = W / HIST_BINS
  const drawW = Math.max(1, binW - 1)
  return activeSampleHist.value.map((count, i) => ({
    x: i * binW + 0.5,
    y: H - (count / yAxisMax.value) * H,
    w: drawW,
    h: (count / yAxisMax.value) * H,
  }))
})

const goldenBars = computed(() => {
  const binW = W / HIST_BINS
  const drawW = Math.max(1, binW - 1)
  return activeGoldenHist.value.map((count, i) => ({
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
</script>

<template>
  <svg :viewBox="`0 0 ${W + PAD_L + PAD_R} ${H + PAD_T + PAD_B}`" class="cdf-svg">
    <g :transform="`translate(${PAD_L},${PAD_T})`">
      <!-- Grid lines -->
      <g v-for="v in yTicks" :key="'grid-'+v">
        <line :x1="0" :y1="H - v / yAxisMax * H" :x2="W" :y2="H - v / yAxisMax * H"
          stroke="#f0f0f0" stroke-width="1" />
      </g>

      <!-- Axes -->
      <line x1="0" y1="0" x2="0" :y2="H" stroke="#9ca3af" stroke-width="1" />
      <line :x1="0" :y1="H" :x2="W" :y2="H" stroke="#9ca3af" stroke-width="1" />

      <!-- Y axis ticks + labels -->
      <g v-for="v in yTicks" :key="'y'+v">
        <line :x1="-3" :y1="H - v / yAxisMax * H" x2="0" :y2="H - v / yAxisMax * H"
          stroke="#9ca3af" stroke-width="1" />
        <text :x="-5" :y="H - v / yAxisMax * H + 3.5"
          text-anchor="end" font-size="8" fill="#6b7280">{{ formatYAxisTick(v) }}</text>
      </g>
      <!-- Y axis label -->
      <text :x="-PAD_L + 6" :y="H / 2" text-anchor="middle" font-size="8" fill="#6b7280"
        :transform="`rotate(-90,${-PAD_L + 6},${H / 2})`">{{ yAxisLabel }}</text>

      <!-- X axis ticks + labels -->
      <g v-for="t in xTicks" :key="'x'+t.label">
        <line :x1="t.x" :y1="H" :x2="t.x" :y2="H + 3" stroke="#9ca3af" stroke-width="1" />
        <text :x="t.x" :y="H + 13" text-anchor="middle" font-size="7.5" fill="#6b7280">{{ t.label }}</text>
      </g>
      <!-- X axis label -->
      <text :x="W / 2" :y="H + PAD_B - 4" text-anchor="middle" font-size="8" fill="#6b7280">粒徑 (μm)</text>

      <template v-if="props.mode === 'cdf'">
      <!-- Golden CDF (dashed, orange) -->
      <polyline v-if="goldenPoints"
        :points="goldenPoints"
        fill="none"
        stroke="#F59E0B"
        stroke-width="1.8"
        stroke-dasharray="5,2.5"
        stroke-linejoin="round"
      />
      <!-- Sample CDF (solid, blue) -->
      <polyline v-if="samplePoints"
        :points="samplePoints"
        fill="none"
        stroke="#3B82F6"
        stroke-width="1.8"
        stroke-linejoin="round"
      />
      </template>
      <template v-else>
        <g class="hist-fill-layer">
          <rect
            v-for="(b, i) in goldenBars"
            :key="'golden-fill-'+i"
            :x="b.x"
            :y="b.y"
            :width="b.w"
            :height="b.h"
            fill="#F59E0B"
            fill-opacity="0.28"
            stroke="none"
          />
          <rect
            v-for="(b, i) in sampleBars"
            :key="'sample-fill-'+i"
            :x="b.x"
            :y="b.y"
            :width="b.w"
            :height="b.h"
            fill="#0f766e"
            fill-opacity="0.28"
            stroke="none"
          />
        </g>
        <g class="hist-stroke-layer">
          <rect
            v-for="(b, i) in goldenBars"
            :key="'golden-stroke-'+i"
            :x="b.x"
            :y="b.y"
            :width="b.w"
            :height="b.h"
            fill="none"
            stroke="#F59E0B"
            stroke-opacity="0.95"
            stroke-width="0.9"
            stroke-dasharray="2.2,1.4"
          />
          <rect
            v-for="(b, i) in sampleBars"
            :key="'sample-stroke-'+i"
            :x="b.x"
            :y="b.y"
            :width="b.w"
            :height="b.h"
            fill="none"
            stroke="#0f766e"
            stroke-opacity="0.95"
            stroke-width="0.9"
          />
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
