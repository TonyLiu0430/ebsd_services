<script setup lang="ts">
const COL_KEYS = ['C', 'M', 'E'] as const
const ROW_KEYS = ['U', 'M', 'B'] as const

const COL_LABELS: Record<string, string> = { C: '內 Center', M: '中 Middle', E: '外 Edge' }
const ROW_LABELS: Record<string, string> = { U: '上 Up', M: '中 Middle', B: '下 Bottom' }

interface FilePair {
  crc: File
  cpr: File
  sample: string
  pos: string
}

const props = defineProps<{
  pairs: FilePair[]
  sample: string
}>()

type CellState = 'idle' | 'loading' | 'loaded' | 'error'

const states = ref<Record<string, CellState>>({})
const images = ref<Record<string, string>>({})
const blobUrls: string[] = []
let loadGeneration = 0

function cellState(pos: string): CellState {
  return states.value[pos] ?? 'idle'
}

function cellImage(pos: string): string | undefined {
  return images.value[pos]
}

async function loadCell(pos: string, crc: File, cpr: File, generation: number) {
  if (generation !== loadGeneration) return
  states.value[pos] = 'loading'
  try {
    const formData = new FormData()
    formData.append('crc', crc)
    formData.append('cpr', cpr)
    const response = await fetch('/cppapi/ipf_map', { method: 'POST', body: formData })
    if (!response.ok) throw new Error(`HTTP ${response.status}`)
    if (generation !== loadGeneration) return
    const blob = await response.blob()
    if (generation !== loadGeneration) return
    const url = URL.createObjectURL(blob)
    blobUrls.push(url)
    images.value[pos] = url
    states.value[pos] = 'loaded'
  } catch (_e) {
    if (generation !== loadGeneration) return
    states.value[pos] = 'error'
  }
}

const samplePairs = computed(() =>
  props.pairs.filter((p) => p.sample === props.sample),
)

async function loadAllCells() {
  loadGeneration++
  const generation = loadGeneration

  for (const url of blobUrls) {
    URL.revokeObjectURL(url)
  }
  blobUrls.length = 0
  images.value = {}
  states.value = {}

  const batch: Promise<void>[] = []
  for (const p of samplePairs.value) {
    batch.push(loadCell(p.pos, p.crc, p.cpr, generation))
  }
  await Promise.all(batch)
}

watch(samplePairs, loadAllCells, { immediate: true })

onUnmounted(() => {
  for (const url of blobUrls) {
    URL.revokeObjectURL(url)
  }
})
</script>

<template>
  <div class="ipf-grid-wrap">
    <div class="ipf-grid-header">
      <div class="ipf-grid-corner"></div>
      <div v-for="colKey in COL_KEYS" :key="colKey" class="ipf-col-header">
        {{ COL_LABELS[colKey] }}<br /><small>({{ colKey }})</small>
      </div>
    </div>
    <div v-for="rowKey in ROW_KEYS" :key="rowKey" class="ipf-grid-row">
      <div class="ipf-row-header">{{ ROW_LABELS[rowKey] }}</div>
      <div v-for="colKey in COL_KEYS" :key="colKey" class="ipf-cell">
        <div class="ipf-pos-label">{{ colKey }}-{{ rowKey }}</div>
        <div class="ipf-img-wrap">
          <div v-if="cellState(`${colKey}-${rowKey}`) === 'loading'" class="ipf-status ipf-loading">
            <span>載入中...</span>
          </div>
          <div v-else-if="cellState(`${colKey}-${rowKey}`) === 'error'" class="ipf-status ipf-error">
            <span>載入失敗</span>
          </div>
          <img
            v-else-if="cellImage(`${colKey}-${rowKey}`)"
            :src="cellImage(`${colKey}-${rowKey}`)"
            class="ipf-image"
            :alt="`IPF map ${colKey}-${rowKey}`"
          />
          <div v-else class="ipf-status ipf-empty">
            <span>無數據</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.ipf-grid-wrap {
  overflow-x: auto;
}

.ipf-grid-header {
  display: grid;
  grid-template-columns: 80px repeat(3, 1fr);
  gap: 8px;
  margin-bottom: 4px;
}

.ipf-grid-corner {
  min-width: 0;
}

.ipf-col-header {
  background: #1e40af;
  color: #fff;
  border-radius: 6px;
  padding: 8px 10px;
  text-align: center;
  font-weight: 700;
  font-size: .85rem;
  line-height: 1.4;
}

.ipf-grid-row {
  display: grid;
  grid-template-columns: 80px repeat(3, 1fr);
  gap: 8px;
  margin-bottom: 8px;
}

.ipf-row-header {
  display: flex;
  align-items: center;
  justify-content: center;
  background: #1e40af;
  color: #fff;
  border-radius: 6px;
  font-weight: 700;
  font-size: .78rem;
  text-align: center;
  padding: 6px 4px;
  line-height: 1.3;
}

.ipf-cell {
  background: #f9fafb;
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  overflow: hidden;
}

.ipf-pos-label {
  font-size: .78rem;
  font-weight: 700;
  color: #374151;
  text-align: center;
  padding: 6px 4px;
  background: #eef2ff;
  border-bottom: 1px solid #e5e7eb;
}

.ipf-img-wrap {
  padding: 6px;
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 160px;
}

.ipf-image {
  width: 100%;
  height: auto;
  display: block;
  border-radius: 4px;
}

.ipf-status {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 100%;
  min-height: 140px;
  font-size: .8rem;
  font-weight: 600;
  border-radius: 4px;
}

.ipf-loading {
  color: #6b7280;
  background: #f3f4f6;
}

.ipf-error {
  color: #dc2626;
  background: #fef2f2;
}

.ipf-empty {
  color: #9ca3af;
  background: #f3f4f6;
}

@media (max-width: 700px) {
  .ipf-grid-header,
  .ipf-grid-row {
    grid-template-columns: 60px repeat(3, 1fr);
  }
}
</style>
