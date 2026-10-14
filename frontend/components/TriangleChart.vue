<script setup lang="ts">
import { roundUpToNearestTen } from '~/utils/chartScale'

/**
 * IPF-style triangle radar chart (SVG).
 * series: Array<{
 *   name: string
 *   color: string
 *   dashed: boolean
 *   values: [axis0, axis1, axis2]  // 0-100 (%)
 * }>
 */

interface Series {
  name: string
  color: string
  dashed: boolean
  values: [number, number, number]
}

interface LabelRequest {
  key: string
  text: string
  color: string
  fontSize: number
  fontWeight: number
  preferredX: number
  preferredY: number
  background?: boolean
  outlined?: boolean
  halo?: boolean
}

interface PlacedLabel extends LabelRequest {
  x: number
  y: number
  width: number
  height: number
}

const props = withDefaults(defineProps<{
  series: Series[]
  title?: string
  showValueLabels?: boolean
  axisLabels?: string[]
  maxValue?: number | null
}>(), {
  series: () => [],
  title: '',
  showValueLabels: true,
  axisLabels: () => ['[001]', '[011]', '[111]'],
  maxValue: null,
})

// SVG geometry
const VIEW_WIDTH = 300
const VIEW_HEIGHT = 270
const CX = 150
const CY = 145
const R = 96
const LABEL_MARGIN = 5

const ANGLES = [-Math.PI / 2, Math.PI / 6, (5 * Math.PI) / 6]
const maxSeriesValue = computed(() => Math.max(
  0,
  ...props.series.flatMap((item) => item.values).filter(Number.isFinite),
))
const maxPct = computed(() => {
  const requested = Number(props.maxValue)
  const maximum = Number.isFinite(requested) && requested > 0 ? requested : maxSeriesValue.value
  return roundUpToNearestTen(maximum, 50)
})
const gridLevels = computed(() => Array.from(
  { length: 5 },
  (_, index) => maxPct.value * (index + 1) / 5,
))

const axisLabels = computed(() => {
  const labels = props.axisLabels?.length ? props.axisLabels : ['[001]', '[011]', '[111]']
  return labels.slice(0, 3)
})

function axisPoint(axisIdx: number, pct: number): { x: number; y: number } {
  const r = (pct / maxPct.value) * R
  return {
    x: CX + r * Math.cos(ANGLES[axisIdx]),
    y: CY + r * Math.sin(ANGLES[axisIdx]),
  }
}

function gridPolygon(pct: number): string {
  return ANGLES.map((a) => {
    const r = (pct / maxPct.value) * R
    return `${CX + r * Math.cos(a)},${CY + r * Math.sin(a)}`
  }).join(' ')
}

function seriesPoint(v: number, axisIdx: number) {
  const r = (Math.min(maxPct.value, Math.max(0, v)) / maxPct.value) * R
  return {
    x: CX + r * Math.cos(ANGLES[axisIdx]),
    y: CY + r * Math.sin(ANGLES[axisIdx]),
  }
}

function seriesPolygon(values: [number, number, number]): string {
  return values.map((v, i) => {
    const p = seriesPoint(v, i)
    return `${p.x},${p.y}`
  }).join(' ')
}

function estimateTextBox(text: string, fontSize: number) {
  return {
    width: Math.max(fontSize, text.length * fontSize * 0.59),
    height: fontSize * 1.25,
  }
}

function collisionSize(request: LabelRequest) {
  const size = estimateTextBox(request.text, request.fontSize)
  const horizontalPadding = request.background ? 6 : request.halo ? 3 : 0
  const verticalPadding = request.background ? 4 : request.halo ? 3 : 0
  return {
    width: size.width + horizontalPadding,
    height: size.height + verticalPadding,
  }
}

function labelBox(x: number, y: number, width: number, height: number) {
  return {
    left: x - width / 2,
    right: x + width / 2,
    top: y - height * 0.78,
    bottom: y + height * 0.22,
  }
}

function overlapArea(a: ReturnType<typeof labelBox>, b: ReturnType<typeof labelBox>, gap = 3) {
  const width = Math.max(0, Math.min(a.right, b.right) - Math.max(a.left, b.left) + gap)
  const height = Math.max(0, Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) + gap)
  return width * height
}

const candidateOffsets = (() => {
  const offsets = [{ x: 0, y: 0 }]
  for (let radius = 4; radius <= 80; radius += 4) {
    const count = Math.max(8, Math.ceil(Math.PI * 2 * radius / 7))
    for (let index = 0; index < count; index++) {
      const angle = Math.PI * 2 * index / count
      offsets.push({ x: radius * Math.cos(angle), y: radius * Math.sin(angle) })
    }
  }
  return offsets
})()

function placeLabels(requests: LabelRequest[], obstacles: Array<{ x: number; y: number; width: number; height: number }>): PlacedLabel[] {
  const placed: PlacedLabel[] = []
  for (const request of requests) {
    const { width, height } = collisionSize(request)
    let best: PlacedLabel | null = null
    let bestScore = Number.POSITIVE_INFINITY
    for (const offset of candidateOffsets) {
      const rawX = request.preferredX + offset.x
      const rawY = request.preferredY + offset.y
      const x = Math.min(VIEW_WIDTH - LABEL_MARGIN - width / 2, Math.max(LABEL_MARGIN + width / 2, rawX))
      const y = Math.min(VIEW_HEIGHT - LABEL_MARGIN - height * 0.22, Math.max(LABEL_MARGIN + height * 0.78, rawY))
      const box = labelBox(x, y, width, height)
      const obstacleOverlap = obstacles.reduce(
        (total, item) => total + overlapArea(box, labelBox(item.x, item.y, item.width, item.height)),
        0,
      )
      const labelOverlap = placed.reduce(
        (total, item) => total + overlapArea(box, labelBox(item.x, item.y, item.width, item.height)),
        0,
      )
      const overlap = obstacleOverlap + labelOverlap
      const displacement = (x - request.preferredX) ** 2 + (y - request.preferredY) ** 2
      const score = overlap * 10000 + displacement
      if (score < bestScore) {
        bestScore = score
        best = { ...request, x, y, width, height }
      }
      if (overlap === 0) break
    }
    if (best) placed.push(best)
  }
  return placed
}

const chartLabels = computed(() => {
  const axisRequests: LabelRequest[] = []
  const valueRequests: LabelRequest[] = []
  const visibleSeries = props.series.filter((item) => item.values.some(Number.isFinite))

  axisLabels.value.forEach((label, axisIndex) => {
    const angle = ANGLES[axisIndex]
    const outer = axisPoint(axisIndex, maxPct.value)
    const outward = { x: Math.cos(angle), y: Math.sin(angle) }
    const tangent = { x: -Math.sin(angle), y: Math.cos(angle) }
    axisRequests.push({
      key: `axis-${axisIndex}`,
      text: label,
      color: '#374151',
      fontSize: 13,
      fontWeight: 700,
      preferredX: outer.x + outward.x * 22,
      preferredY: outer.y + outward.y * 22 + 4,
    })

    if (props.showValueLabels) {
      visibleSeries.forEach((series, seriesIndex) => {
        const value = series.values[axisIndex]
        if (!Number.isFinite(value)) return
        const centeredIndex = seriesIndex - (visibleSeries.length - 1) / 2
        const endpoint = seriesPoint(value, axisIndex)
        valueRequests.push({
          key: `value-${axisIndex}-${seriesIndex}`,
          text: `${value.toFixed(1)}%`,
          color: series.color,
          fontSize: 9,
          fontWeight: 700,
          preferredX: endpoint.x + outward.x * 15 + tangent.x * centeredIndex * 18,
          preferredY: endpoint.y + outward.y * 15 + tangent.y * centeredIndex * 18 + 3,
          halo: true,
        })
      })
    }
  })

  const top = axisPoint(0, maxPct.value)
  const right = axisPoint(1, maxPct.value)
  const midpoint = { x: (top.x + right.x) / 2, y: (top.y + right.y) / 2 }
  const normalLength = Math.hypot(midpoint.x - CX, midpoint.y - CY) || 1
  const outerScaleRequest: LabelRequest = {
    key: 'outer-scale',
    text: `${maxPct.value}%`,
    color: '#6b7280',
    fontSize: 9,
    fontWeight: 700,
    preferredX: midpoint.x + (midpoint.x - CX) / normalLength * 14,
    preferredY: midpoint.y + (midpoint.y - CY) / normalLength * 14 + 3,
    halo: true,
  }

  const endpointObstacles = visibleSeries.flatMap((series) => series.values.flatMap((value, axisIndex) => {
    if (!Number.isFinite(value)) return []
    const endpoint = seriesPoint(value, axisIndex)
    return [{ ...endpoint, width: 14, height: 14 }]
  }))

  // Stable labels reserve their space first.  Data values then search the
  // nearest collision-free position instead of relying on fixed zones.
  return placeLabels([...axisRequests, outerScaleRequest, ...valueRequests], endpointObstacles)
})
</script>

<template>
  <div class="triangle-wrap">
    <div v-if="title" class="tri-title">{{ title }}</div>
    <svg :viewBox="`0 0 ${VIEW_WIDTH} ${VIEW_HEIGHT}`" class="tri-svg">
      <!-- Background grid (concentric triangles) -->
      <g v-for="level in gridLevels" :key="level">
        <polygon
          :points="gridPolygon(level)"
          fill="none"
          stroke="#d1d5db"
          stroke-width="0.8"
        />
      </g>

      <!-- Axis lines from center to vertex -->
      <line
        v-for="(a, i) in ANGLES"
        :key="i"
        :x1="CX"
        :y1="CY"
        :x2="CX + R * Math.cos(a)"
        :y2="CY + R * Math.sin(a)"
        stroke="#9ca3af"
        stroke-width="1"
      />

      <!-- Data series -->
      <g v-for="(s, si) in series" :key="si">
        <polygon
          :points="seriesPolygon(s.values)"
          :fill="s.color"
          fill-opacity="0.07"
          :stroke="s.color"
          stroke-width="1"
          :stroke-dasharray="s.dashed ? '5,3' : 'none'"
        />
        <!-- vertex dots -->
        <g v-for="(v, vi) in s.values" :key="vi">
          <circle
            :cx="seriesPoint(v, vi).x"
            :cy="seriesPoint(v, vi).y"
            r="2.5"
            :fill="s.color"
          />
        </g>
      </g>

      <!-- Labels render last, so chart strokes can never cut through text. -->
      <g v-for="label in chartLabels" :key="label.key">
        <rect
          v-if="label.background"
          :x="label.x - label.width / 2"
          :y="label.y - label.height * 0.78"
          :width="label.width"
          :height="label.height"
          rx="3"
          fill="#ffffff"
          :fill-opacity="label.outlined ? 0.96 : 0.9"
          :stroke="label.outlined ? '#d1d5db' : 'none'"
          stroke-width="0.6"
        />
        <text
          v-if="label.halo"
          :x="label.x"
          :y="label.y"
          text-anchor="middle"
          :font-size="label.fontSize"
          :font-weight="label.fontWeight"
          fill="none"
          stroke="#ffffff"
          stroke-width="2.4"
          stroke-linejoin="round"
        >{{ label.text }}</text>
        <text
          :x="label.x"
          :y="label.y"
          text-anchor="middle"
          :font-size="label.fontSize"
          :font-weight="label.fontWeight"
          :fill="label.color"
        >{{ label.text }}</text>
      </g>
    </svg>

    <!-- Legend -->
    <div class="tri-legend">
      <div v-for="(s, i) in series" :key="i" class="legend-item">
        <svg width="24" height="12">
          <line
            x1="0" y1="6" x2="24" y2="6"
            :stroke="s.color"
            stroke-width="2"
            :stroke-dasharray="s.dashed ? '4,2' : 'none'"
          />
        </svg>
        <span>{{ s.name }}</span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.triangle-wrap {
  display: flex;
  flex-direction: column;
  align-items: center;
}
.tri-title {
  font-size: 0.9rem;
  font-weight: 600;
  color: #374151;
  margin-bottom: 4px;
}
.tri-svg {
  width: 100%;
  max-width: 300px;
  height: auto;
}
.tri-legend {
  display: flex;
  flex-wrap: wrap;
  gap: 6px 14px;
  justify-content: center;
  margin-top: 6px;
}
.legend-item {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 0.75rem;
  color: #374151;
}
</style>
