<script setup lang="ts">
definePageMeta({
  middleware: 'auth',
})

type InterpolationMode = 'pixelated' | 'crisp-edges' | 'smooth'

const route = useRoute()

const zoom = ref(100)
const naturalWidth = ref(0)
const naturalHeight = ref(0)
const interpolation = ref<InterpolationMode>('pixelated')

const title = computed(() => {
  const value = Array.isArray(route.query.title) ? route.query.title[0] : route.query.title
  return typeof value === 'string' && value.trim() ? value.trim() : 'IPF map'
})

const imageSrc = computed(() => {
  const value = Array.isArray(route.query.src) ? route.query.src[0] : route.query.src
  if (typeof value !== 'string') return ''
  return value.startsWith('/api/ebsd/pairs/') && value.includes('/ipf_map') ? value : ''
})

const imageRendering = computed(() => (interpolation.value === 'smooth' ? 'auto' : interpolation.value))
const displayWidth = computed(() => (naturalWidth.value > 0 ? `${Math.max(1, Math.round(naturalWidth.value * zoom.value / 100))}px` : 'auto'))
const imageSizeLabel = computed(() => {
  if (!naturalWidth.value || !naturalHeight.value) return '載入中'
  return `${naturalWidth.value} x ${naturalHeight.value}px`
})

function setZoom(value: number) {
  zoom.value = Math.max(25, Math.min(800, Math.round(value)))
}

function fitToWindow() {
  if (!naturalWidth.value || !naturalHeight.value || typeof window === 'undefined') return
  const availableWidth = Math.max(240, window.innerWidth - 48)
  const availableHeight = Math.max(240, window.innerHeight - 150)
  const widthRatio = availableWidth / naturalWidth.value
  const heightRatio = availableHeight / naturalHeight.value
  setZoom(Math.min(widthRatio, heightRatio, 1) * 100)
}

function onImageLoad(event: Event) {
  const img = event.target as HTMLImageElement
  naturalWidth.value = img.naturalWidth
  naturalHeight.value = img.naturalHeight
}
</script>

<template>
  <main class="viewer-page">
    <header class="viewer-toolbar">
      <div class="viewer-title-block">
        <h1>{{ title }}</h1>
        <p>{{ imageSizeLabel }} / {{ zoom }}%</p>
      </div>

      <div class="viewer-controls">
        <button type="button" @click="fitToWindow">Fit</button>
        <button type="button" @click="setZoom(100)">100%</button>
        <button type="button" @click="setZoom(200)">200%</button>
        <button type="button" @click="setZoom(400)">400%</button>

        <label class="zoom-control">
          <span>縮放</span>
          <input v-model.number="zoom" type="range" min="25" max="800" step="25" />
        </label>

        <label class="interpolation-control">
          <span>插值</span>
          <select v-model="interpolation">
            <option value="pixelated">最近鄰 pixelated</option>
            <option value="crisp-edges">清晰邊緣 crisp-edges</option>
            <option value="smooth">平滑 smooth</option>
          </select>
        </label>

        <a v-if="imageSrc" :href="imageSrc" target="_blank" rel="noopener" class="raw-link">原始 PNG</a>
      </div>
    </header>

    <section class="viewer-stage">
      <img
        v-if="imageSrc"
        :src="imageSrc"
        :alt="title"
        class="viewer-image"
        draggable="false"
        :style="{ width: displayWidth, imageRendering }"
        @load="onImageLoad"
      />
      <div v-else class="viewer-error">無效的 IPF map URL</div>
    </section>
  </main>
</template>

<style scoped>
.viewer-page {
  min-height: 100vh;
  background: #111827;
  color: #f9fafb;
}

.viewer-toolbar {
  position: sticky;
  top: 0;
  z-index: 10;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 12px 16px;
  border-bottom: 1px solid rgba(255, 255, 255, .12);
  background: rgba(17, 24, 39, .96);
}

.viewer-title-block h1 {
  margin: 0;
  font-size: 1rem;
  line-height: 1.3;
}

.viewer-title-block p {
  margin: 3px 0 0;
  color: #cbd5e1;
  font-size: .82rem;
}

.viewer-controls {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  justify-content: flex-end;
  gap: 8px;
}

.viewer-controls button,
.raw-link {
  border: 1px solid rgba(255, 255, 255, .22);
  border-radius: 6px;
  background: #1f2937;
  color: #f9fafb;
  padding: 6px 10px;
  font-size: .82rem;
  font-weight: 700;
  text-decoration: none;
  cursor: pointer;
}

.viewer-controls button:hover,
.raw-link:hover {
  background: #374151;
}

.zoom-control,
.interpolation-control {
  display: flex;
  align-items: center;
  gap: 6px;
  color: #d1d5db;
  font-size: .82rem;
  font-weight: 700;
}

.zoom-control input {
  width: 160px;
}

.interpolation-control select {
  border: 1px solid rgba(255, 255, 255, .22);
  border-radius: 6px;
  background: #1f2937;
  color: #f9fafb;
  padding: 5px 8px;
}

.viewer-stage {
  overflow: auto;
  height: calc(100vh - 74px);
  padding: 24px;
}

.viewer-image {
  display: block;
  max-width: none;
  height: auto;
  background: #000;
  box-shadow: 0 0 0 1px rgba(255, 255, 255, .12);
}

.viewer-error {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 240px;
  color: #fecaca;
  font-weight: 700;
}

@media (max-width: 760px) {
  .viewer-toolbar {
    align-items: flex-start;
    flex-direction: column;
  }

  .viewer-controls {
    justify-content: flex-start;
  }

  .zoom-control input {
    width: 120px;
  }

  .viewer-stage {
    height: calc(100vh - 148px);
    padding: 12px;
  }
}
</style>
