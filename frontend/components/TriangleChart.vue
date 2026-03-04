<script setup lang="ts">
/**
 * IPF-style triangle radar chart (SVG).
 * 3 axes:
 *   [001] → top      (angle = -π/2)
 *   [110] → bottom-right (angle = +π/6)
 *   [111] → bottom-left  (angle = 5π/6)
 *
 * series: Array<{
 *   name: string
 *   color: string
 *   dashed: boolean
 *   values: [v001, v110, v111]  // 0-100 (%)
 * }>
 */

interface Series {
  name: string
  color: string
  dashed: boolean
  values: [number, number, number]
}

const props = withDefaults(defineProps<{
  series: Series[]
  title?: string
}>(), {
  series: () => [],
  title: ''
})

// SVG geometry
const CX = 140
const CY = 148
const R = 100 // radius for 100%

const ANGLES = [-Math.PI / 2, Math.PI / 6, (5 * Math.PI) / 6] // 001, 110, 111
const AXIS_LABELS = ['001', '110', '111']
const MAX_PCT = 50  // 頂點 = 50%，超過自然超出

const GRID_LEVELS = [10, 20, 30, 40, 50]

function axisPoint(axisIdx: number, pct: number): { x: number; y: number } {
  const r = (pct / MAX_PCT) * R
  return {
    x: CX + r * Math.cos(ANGLES[axisIdx]),
    y: CY + r * Math.sin(ANGLES[axisIdx]),
  }
}

function gridPolygon(pct: number): string {
  return ANGLES.map((a) => {
    const r = (pct / MAX_PCT) * R
    return `${CX + r * Math.cos(a)},${CY + r * Math.sin(a)}`
  }).join(' ')
}

function seriesPoint(v: number, axisIdx: number) {
  const r = (v / MAX_PCT) * R  // 不 clamp，> 50% 自然超出頂點
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

function axisLabelPos(idx: number) {
  const labelR = R + 18
  return {
    x: CX + labelR * Math.cos(ANGLES[idx]),
    y: CY + labelR * Math.sin(ANGLES[idx]),
  }
}
</script>

<template>
  <div class="triangle-wrap">
    <div v-if="title" class="tri-title">{{ title }}</div>
    <svg viewBox="0 0 280 270" class="tri-svg" overflow="visible">
      <!-- Background grid (concentric triangles) -->
      <g v-for="level in GRID_LEVELS" :key="level">
        <polygon
          :points="gridPolygon(level)"
          fill="none"
          stroke="#d1d5db"
          stroke-width="0.8"
        />
        <!-- grid label on 001 axis -->
        <text
          :x="axisPoint(0, level).x + 4"
          :y="axisPoint(0, level).y"
          font-size="7.5"
          fill="#9ca3af"
          text-anchor="start"
        >{{ level }}%</text>
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

      <!-- Axis labels -->
      <text
        v-for="(label, i) in AXIS_LABELS"
        :key="label"
        :x="axisLabelPos(i).x"
        :y="axisLabelPos(i).y + 4"
        text-anchor="middle"
        font-size="13"
        font-weight="700"
        fill="#374151"
      >{{ label }}</text>

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
        <!-- vertex dots + value labels -->
        <g v-for="(v, vi) in s.values" :key="vi">
          <circle
            :cx="seriesPoint(v, vi).x"
            :cy="seriesPoint(v, vi).y"
            r="2.5"
            :fill="s.color"
          />
          <text
            :x="seriesPoint(v, vi).x"
            :y="seriesPoint(v, vi).y - 5"
            text-anchor="middle"
            font-size="7"
            :fill="s.color"
            font-weight="600"
          >{{ v.toFixed(1) }}%</text>
        </g>
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
  max-width: 280px;
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
