<script setup>
import { computed } from "vue";


const props = defineProps({
  blocks: {
    type: Object,
    default: () => ({
      center: {
        up: { value: 30, color: "#F59E0B" },
        mid: { value: 20, color: "#F59E0B" },
        bottom: { value: 10, color: "#F59E0B" },
      },
      middle: {
        up: { value: 15, color: "#F59E0B" },
        mid: { value: 10, color: "#F59E0B" },
        bottom: { value: 5, color: "#F59E0B" },
      },
      edge: {
        up: { value: 5, color: "#F59E0B" },
        mid: { value: 3, color: "#F59E0B" },
        bottom: { value: 2, color: "#F59E0B" },
      },
    }),
  },
});

const columns = [
  { key: "center", label: "內 Center" },
  { key: "middle", label: "中 Middle" },
  { key: "edge", label: "外 Edge" },
];

const rows = [
  { key: "up", label: "上 Up" },
  { key: "mid", label: "中 Middle" },
  { key: "bottom", label: "下 Bottom" },
];

const DEFAULT_COLOR = "#F59E0B";

function cell(colKey, rowKey) {
  const obj = props.blocks?.[colKey]?.[rowKey] ?? {};
  return {
    value: Number(obj.value ?? 0) || 0,
    color: obj.color ?? DEFAULT_COLOR,
  };
}

const total = computed(() => {
  let sum = 0;
  for (const c of columns) {
    for (const r of rows) sum += cell(c.key, r.key).value;
  }
  return sum;
});

// --- 佈局（你原本那組，且已避免裁切） ---
const cube = { w: 46, h: 36, d: 8 };
const grid = { x0: 258, y0: 155, colStep: 74, rowStep: 52 };

const arrowDots = computed(() => {
  return columns.map((c, i) => ({
    key: c.key,
    x: grid.x0 + i * grid.colStep + cube.w / 2 + cube.d / 2,
    y: 118,
  }));
});
</script>

<template>
  <div class="wrap">
    <div class="stage">
      <svg class="svg" viewBox="0 0 640 360" aria-label="cylinder-grid">
        <defs>
          <linearGradient id="glassSide" x1="0" y1="0" x2="1" y2="0">
            <stop offset="0%" stop-color="#ffffff" stop-opacity="0.18" />
            <stop offset="45%" stop-color="#b7d3ff" stop-opacity="0.20" />
            <stop offset="100%" stop-color="#5b84c6" stop-opacity="0.28" />
          </linearGradient>

          <radialGradient id="topGrad" cx="45%" cy="35%" r="70%">
            <stop offset="0%" stop-color="#d9ecff" stop-opacity="0.95" />
            <stop offset="100%" stop-color="#9fc2ee" stop-opacity="0.95" />
          </radialGradient>

          <radialGradient id="bottomGrad" cx="45%" cy="65%" r="80%">
            <stop offset="0%" stop-color="#cfe3ff" stop-opacity="0.25" />
            <stop offset="100%" stop-color="#6c93ce" stop-opacity="0.18" />
          </radialGradient>

          <filter id="softShadow" x="-30%" y="-30%" width="160%" height="160%">
            <feDropShadow dx="0" dy="1.2" stdDeviation="1.2" flood-color="#000" flood-opacity="0.16" />
          </filter>
        </defs>

        <!-- 圓柱（純顯示，不吃點擊事件） -->
        <g pointer-events="none">
           <path
                d="
                    M70,92
                    L70,288
                    A210,46 0 0 0 490,288
                    L490,92
                    A210,46 ... 490,288
                    Z
                "
                fill="url(#glassSide)"
                stroke="#1e3a5f"
                stroke-width="4"
                stroke-linejoin="round"
                opacity="1"
            />

          <ellipse cx="280" cy="92" rx="210" ry="46" fill="url(#topGrad)" opacity="0.80" />
          <ellipse cx="280" cy="92" rx="210" ry="46" fill="none" stroke="#1e3a5f" stroke-width="4" />

          <ellipse cx="280" cy="288" rx="210" ry="46" fill="url(#bottomGrad)" opacity="0.40" />
          <ellipse cx="280" cy="288" rx="210" ry="46" fill="none" stroke="#1e3a5f" stroke-width="4" />
        </g>

        <!-- 九方塊（純顯示） -->
        <g clip-path="url(#cylClip)" filter="url(#softShadow)">
          <g v-for="(col, ci) in columns" :key="col.key">
            <g
              v-for="(row, ri) in rows"
              :key="row.key"
              :transform="`translate(${grid.x0 + ci * grid.colStep}, ${grid.y0 + ri * grid.rowStep})`"
              stroke-linejoin="round"
              stroke-linecap="round"
            >
              <!-- 上面 -->
              <polygon
                :points="`0,0 ${cube.d},${-cube.d} ${cube.w + cube.d},${-cube.d} ${cube.w},0`"
                :fill="cell(col.key, row.key).color"
                stroke="#7c5a00"
                stroke-width="2"
              />
              <!-- 右側 -->
              <polygon
                :points="`${cube.w},0 ${cube.w + cube.d},${-cube.d} ${cube.w + cube.d},${cube.h - cube.d} ${cube.w},${cube.h}`"
                :fill="cell(col.key, row.key).color"
                stroke="#7c5a00"
                stroke-width="2"
              />
              <!-- 正面 -->
              <rect
                x="0"
                y="0"
                :width="cube.w"
                :height="cube.h"
                :fill="cell(col.key, row.key).color"
                stroke="#7c5a00"
                stroke-width="2"
              />

              <!-- 百分比 -->
              <text
                :x="cube.w / 2"
                :y="cube.h / 2 + 5"
                text-anchor="middle"
                font-size="15"
                font-weight="900"
                fill="#111"
              >
                {{ cell(col.key, row.key).value }}%
              </text>
            </g>
          </g>
        </g>

        <!-- 右側文字 -->
        <g font-size="22" font-weight="900" fill="#111" pointer-events="none">
          <text x="525" y="160">上 Up</text>
          <text x="525" y="225">中 Middle</text>
          <text x="525" y="290">下 Bottom</text>
        </g>

        <!-- 三個紅色箭頭 + 標籤 -->
        <g pointer-events="none">
          <g fill="none" stroke="#e11d48" stroke-width="10" stroke-linecap="round" stroke-linejoin="round">
            <line x1="210" y1="70" :x2="arrowDots[0].x" :y2="arrowDots[0].y" />
            <circle :cx="arrowDots[0].x" :cy="arrowDots[0].y" r="10" />

            <line :x1="arrowDots[1].x" y1="45" :x2="arrowDots[1].x" :y2="arrowDots[1].y" />
            <circle :cx="arrowDots[1].x" :cy="arrowDots[1].y" r="10" />

            <line x1="430" y1="70" :x2="arrowDots[2].x" :y2="arrowDots[2].y" />
            <circle :cx="arrowDots[2].x" :cy="arrowDots[2].y" r="10" />
          </g>

          <g font-size="24" font-weight="900" fill="#111">
            <text x="160" y="55">內 Center</text>
            <text x="292" y="30">中 Middle</text>
            <text x="392" y="55">外 Edge</text>
          </g>
        </g>
      </svg>
    </div>
  </div>
</template>

<style scoped>
.wrap {
  max-width: 900px;
  margin: 24px auto;
  padding: 0 16px;
  font-family: ui-sans-serif, system-ui, -apple-system, "Segoe UI", Roboto, "Noto Sans TC", Arial;
}

.stage {
  border: 1px solid #e5e7eb;
  border-radius: 14px;
  padding: 14px;
  background: white;
}

.svg {
  width: 100%;
  height: auto;
  display: block;
}
</style>

