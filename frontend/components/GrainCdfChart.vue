<script setup lang="ts">
/**
 * Grain size CDF (Cumulative Distribution Function) chart.
 * Renders two sorted grain arrays as CDF curves in SVG.
 */

const props = withDefaults(defineProps<{
  sample: number[]
  golden: number[]
  sampleLabel?: string
  goldenLabel?: string
}>(), {
  sample: () => [],
  golden: () => [],
  sampleLabel: 'Sample',
  goldenLabel: 'Golden',
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

const xMax = computed(() => {
  const all = [...props.sample, ...props.golden]
  return all.length ? Math.ceil(Math.max(...all) * 10) / 10 : 10
})

const xMin = computed(() => 0)

const samplePoints = computed(() => makeCdf(props.sample, xMin.value, xMax.value))
const goldenPoints = computed(() => makeCdf(props.golden, xMin.value, xMax.value))

const xTicks = computed(() => {
  const count = 4
  const step = xMax.value / count
  return Array.from({ length: count + 1 }, (_, i) => {
    const v = step * i
    return {
      x: (v / xMax.value) * W,
      label: v.toFixed(1),
    }
  })
})

const yTicks = [0, 25, 50, 75, 100]
</script>

<template>
  <svg :viewBox="`0 0 ${W + PAD_L + PAD_R} ${H + PAD_T + PAD_B}`" class="cdf-svg">
    <g :transform="`translate(${PAD_L},${PAD_T})`">
      <!-- Grid lines -->
      <g v-for="pct in yTicks" :key="pct">
        <line :x1="0" :y1="H - pct / 100 * H" :x2="W" :y2="H - pct / 100 * H"
          stroke="#f0f0f0" stroke-width="1" />
      </g>

      <!-- Axes -->
      <line x1="0" y1="0" x2="0" :y2="H" stroke="#9ca3af" stroke-width="1" />
      <line :x1="0" :y1="H" :x2="W" :y2="H" stroke="#9ca3af" stroke-width="1" />

      <!-- Y axis ticks + labels -->
      <g v-for="pct in yTicks" :key="'y'+pct">
        <line :x1="-3" :y1="H - pct / 100 * H" x2="0" :y2="H - pct / 100 * H"
          stroke="#9ca3af" stroke-width="1" />
        <text :x="-5" :y="H - pct / 100 * H + 3.5"
          text-anchor="end" font-size="8" fill="#6b7280">{{ pct }}</text>
      </g>
      <!-- Y axis label -->
      <text :x="-PAD_L + 6" :y="H / 2" text-anchor="middle" font-size="8" fill="#6b7280"
        :transform="`rotate(-90,${-PAD_L + 6},${H / 2})`">累積 %</text>

      <!-- X axis ticks + labels -->
      <g v-for="t in xTicks" :key="'x'+t.label">
        <line :x1="t.x" :y1="H" :x2="t.x" :y2="H + 3" stroke="#9ca3af" stroke-width="1" />
        <text :x="t.x" :y="H + 13" text-anchor="middle" font-size="7.5" fill="#6b7280">{{ t.label }}</text>
      </g>
      <!-- X axis label -->
      <text :x="W / 2" :y="H + PAD_B - 4" text-anchor="middle" font-size="8" fill="#6b7280">粒徑 (μm)</text>

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
