<template>
  <div class="page-shell">
    <h1 class="page-title">EBSD 分析</h1>
    
    <div
      class="upload-area"
      :class="{ active: pairs.length > 0 }"
      @click="triggerInput"
      @dragover.prevent
      @drop.prevent="handleDrop"
    >
      <input ref="folderInput" type="file" webkitdirectory directory multiple @change="handleSelect" />
      <div v-if="pairs.length === 0" class="upload-hint">
        <div class="upload-icon">📂</div>
        <p>點擊或拖曳資料夾至此</p>
      </div>
      <div v-else class="upload-done">
        <span class="check">✔</span>
        已讀取 <strong>{{ pairs.length }}</strong> 個掃描檔，共
        <strong>{{ samples.size }}</strong> 個樣本
      </div>
    </div>

    <div v-if="pairs.length" class="result">
      <h3>已選取: {{ selectedFolder }} 共 {{ samples.size }} 個靶材 ({{ pairs.length }} 個 ebsd 掃描資料)</h3>
      <div v-for="(sample_name) in samples" :key="sample_name" class="pair-row">
        <span>{{ sample_name }}</span>
      </div>
      <div></div>
      <button @click="submit" :disabled="loading">{{ loading ? `處理中... (${doneCount}/${pairs.length})` : '送出' }}</button>
      <br>
      <el-text v-if="error" class="mx-1" type="danger">{{ error }}</el-text>
      <div v-if="result">
        <section v-if="pairs.length" class="card">
          <h2 class="section-title">IPF 晶粒取向分佈圖</h2>
          <IpfMapGrid :pairs="pairs" :sample="[...samples][0] || ''" />
        </section>
        <div>選擇分析項目</div>
        <el-checkbox-group v-model="selectedFeatures">
          <el-checkbox v-for="feat in featuresOptions || []" :key="feat" :label="feat" :value="feat" />
        </el-checkbox-group>
        <div class="golden-sample-selector">
          <label for="goldenSampleSelect">選擇 Golden Sample:</label>
          <select id="goldenSampleSelect" v-model="goldenSample">
            <option value="">-- 未選擇 --</option>
            <option v-for="sample_name in samples" :key="sample_name" :value="sample_name">
              {{ sample_name }}
            </option>
          </select>
        </div>
        <el-button @click="analysis">apply</el-button>
        <div v-if="analysisRes">
          <h3>分析結果</h3>
          <div class="color-range-settings">
            <h4>顏色範圍設定（%）</h4>
            <div class="color-slider-wrap">
              <div class="color-bar">
                <div class="seg green" :style="{ width: colorThresholds[0] + '%' }"></div>
                <div class="seg yellow" :style="{ width: (colorThresholds[1] - colorThresholds[0]) + '%' }"></div>
                <div class="seg red" :style="{ width: (100 - colorThresholds[1]) + '%' }"></div>
              </div>
              <el-slider v-model="colorThresholds" range :min="0" :max="100" :step="1" :show-tooltip="true" />
            </div>
          </div>
          <div v-for="(sampleData, sampleName) in analysisRes" :key="sampleName" style="margin-bottom: 2rem;">
            <h4>{{ sampleName }}</h4>
            <ninepos :blocks="convertToNineposFormat(sampleData)" />
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
definePageMeta({
  middleware: 'auth'
})

interface FilePair { name: string; crc: File; cpr: File; sample: string; pos: string }
type CppFeatures = {
  grains: number[]
  "orientation_ratio(20%)": number[]
  "orientation_ratio(15%)": number[]
}

type AllDataResult = Record<string, Record<string, CppFeatures>>
type TODO = any
type AnalysisResult = Record<string, Record<string, number>>

const folderInput = ref<HTMLInputElement>()
const selectedFolder = ref('')
const samples = ref(new Set<string>())
const pairs = ref<FilePair[]>([])
const loading = ref(false)
const doneCount = ref(0)
const result = ref<AllDataResult | null>(null)
const error = ref('')
const goldenSample = ref('')
const selectedFeatures = ref<string[]>([])
const analysisRes = ref<AnalysisResult | null>(null)

const colorThresholds = ref([30, 70])

const { data: featuresOptions } = await useFetch<string[]>('/api/ebsd_features')

function getColorForValue(valuePercent: number): string {
  const [low, high] = colorThresholds.value
  if (Math.abs(valuePercent) <= low) return "#10B981"
  if (Math.abs(valuePercent) >= high) return "#EF4444"
  return "#F59E0B"
}

function convertToNineposFormat(sampleData: Record<string, number>) {
  const positionMap: Record<string, { col: string; row: string }> = {
    'C-U': { col: 'center', row: 'up' },
    'C-M': { col: 'center', row: 'mid' },
    'C-B': { col: 'center', row: 'bottom' },
    'M-U': { col: 'middle', row: 'up' },
    'M-M': { col: 'middle', row: 'mid' },
    'M-B': { col: 'middle', row: 'bottom' },
    'E-U': { col: 'edge', row: 'up' },
    'E-M': { col: 'edge', row: 'mid' },
    'E-B': { col: 'edge', row: 'bottom' },
  }
  
  const result: Record<string, Record<string, { value: number; color: string }>> = {
    center: {},
    middle: {},
    edge: {},
  }
  
  for (const [posKey, value] of Object.entries(sampleData)) {
    const mapping = positionMap[posKey]
    if (mapping) {
      const valuePercent = Number((value * 100).toFixed(1))
      result[mapping.col][mapping.row] = {
        value: valuePercent,
        color: getColorForValue(valuePercent),
      }
    }
  }
  
  return result
}

function triggerInput() { folderInput.value?.click() }

function handleSelect(e: Event) { processFiles((e.target as HTMLInputElement).files) }

function handleDrop(e: DragEvent) { processFiles(e.dataTransfer?.files) }

function processFiles(fileList: FileList | null | undefined) {
  if (!fileList?.length) return
  result.value = null

  const allFiles = Array.from(fileList)
  selectedFolder.value = allFiles[0]?.webkitRelativePath.split('/')[0] || ''
  
  const crcFiles = new Map<string, { file: File; sample: string; pos: string; num: string }>()
  const cprFiles = new Map<string, { file: File; num: string }>()
  
  samples.value = new Set()
  pairs.value = []

  for (const f of allFiles) {
    if (!f.name.endsWith('.crc') && !f.name.endsWith('.cpr')) continue
    const parts = f.webkitRelativePath.split('/')
    const subdir = parts[1] || ''
    const lastDash = subdir.lastIndexOf('-')
    const dataPatch = lastDash > 0 ? subdir.slice(0, lastDash) : subdir
    const num = lastDash > 0 ? subdir.slice(lastDash + 1) : '01'
    const pos = standard_pos(f.name.replace(/\.(crc|cpr)$/i, '').trim())
    const key = `${dataPatch}-${pos}`
    samples.value.add(dataPatch) // sample
    if (f.name.endsWith('.crc')) {
      const existing = crcFiles.get(key)
      if (!existing || num > existing.num) crcFiles.set(key, { file: f, sample: dataPatch, pos, num })
    } else {
      const existing = cprFiles.get(key)
      if (!existing || num > existing.num) cprFiles.set(key, { file: f, num })
    }
  }
  
  for (const [key, { file: crc, sample, pos }] of crcFiles) {
    const cprEntry = cprFiles.get(key)
    if (cprEntry) pairs.value.push({ name: key, crc, cpr: cprEntry.file, sample, pos })
  }
}

const standard_pos = (pos: string): string => {
  pos = pos.trim()

  // Format 1: 已有破折號的標準格式，例如 "C-B", "M-U", "E-M"
  const dashMatch = pos.match(/^([CME])-([UMB])$/i)
  if (dashMatch) {
    return `${dashMatch[1].toUpperCase()}-${dashMatch[2].toUpperCase()}`
  }

  // Format 2 (DATA13): 無破折號，E/I/M 開頭
  //   E=E(edge), I=C(center), M=M(middle)
  // Format 3 (DATA14+): 無破折號，A/B/C 開頭
  //   A=C(center), B=M(middle), C=E(edge)
  const colMap: Record<string, string> = {
    'E': 'E', 'I': 'C', 'M': 'M', // Format 2
    'A': 'C', 'B': 'M', 'C': 'E', // Format 3
  }
  const rowMap: Record<string, string> = {
    'U': 'U', 'M': 'M', 'B': 'B', 'D': 'B',
  }

  if (pos.length >= 2) {
    const col = colMap[pos[0].toUpperCase()]
    const row = rowMap[pos[1].toUpperCase()]
    if (col && row) return `${col}-${row}`
  }

  return pos
}

async function submit() {
  loading.value = true
  error.value = ''
  result.value = null
  doneCount.value = 0
  
  try {
    const results = []
    for (let i = 0; i < pairs.value.length; i += 2) {
      const batch = pairs.value.slice(i, i + 2)
      results.push(...await Promise.all(batch.map(async (p) => {
        const formData = new FormData()
        formData.append('crc', p.crc)
        formData.append('cpr', p.cpr)
        const features = await $fetch<CppFeatures>('/cppapi/features', { method: 'POST', body: formData })
        doneCount.value++
        return { sample: p.sample, pos: p.pos, features }
      })))
    }

    const allResult: AllDataResult = {}
    for (const res of results) {
      if (!allResult[res.sample]) allResult[res.sample] = {}
      allResult[res.sample][res.pos] = res.features
    }
    
    result.value = allResult
  } catch (e: unknown) {
    const err = e as { data?: { message?: string }; message?: string }
    error.value = err.data?.message || err.message || 'Unknown error'
  } finally {
    loading.value = false
  }
}

const analysis = async () => {
  if(selectedFeatures.value.length == 0 || goldenSample.value == '') {
    ElMessageBox.alert('請至少選擇一個 分析項目 及 golden sample', '警告', {
      confirmButtonText: 'OK',
    })
    return
  }
  if(result.value == null) {
    ElMessageBox.alert('沒有數據', '警告', {
      confirmButtonText: 'OK',
    })
    return
  }
  analysisRes.value = null
  const res = await $fetch<AnalysisResult>('/api/analysis', {
    method: 'POST',
    body: {
      golden: goldenSample.value,
      features: selectedFeatures.value,
      data: result.value
    }
  })
  analysisRes.value = res
}


</script>

<style scoped>
.page-shell {
  max-width: 1120px;
  margin: 0 auto;
  padding: 2rem 1.5rem 3rem;
  color: #111827;
}

.page-title {
  margin: 0 0 1.5rem;
  font-size: 1.7rem;
  font-weight: 500;
}
.upload-area {
  border: 2px dashed #d1d5db;
  border-radius: 10px;
  padding: 2rem;
  text-align: center;
  cursor: pointer;
  transition: border-color .2s, background .2s;
}
.upload-area:hover, .upload-area.active { border-color: #3B82F6; background: #eff6ff; }
.upload-area input { display: none; }
.upload-icon { font-size: 2rem; margin-bottom: .4rem; }
.upload-hint p { margin: 0; color: #6b7280; }
.upload-done { color: #059669; font-weight: 500; }
.check { font-size: 1.2rem; margin-right: .4rem; }
.result { margin-top: 2rem; padding: 1rem; background: #f5f5f5; border-radius: 8px; }
.result h3 { margin-bottom: 1rem; }
.pair-row { display: flex; gap: 0.5rem; margin-bottom: 0.5rem; align-items: center; }
.pair-row span { flex: 1; font-size: 0.9rem; }
.pair-row input { width: 100px; padding: 0.3rem; border: 1px solid #ccc; border-radius: 4px; }
button { margin-top: 1rem; padding: 0.5rem 1.5rem; background: #333; color: #fff; border: none; border-radius: 4px; cursor: pointer; }
button:disabled { background: #999; }
pre { margin-top: 1rem; background: #fff; padding: 1rem; border-radius: 4px; overflow: auto; max-height: 300px; font-size: 0.8rem; }
.error { color: #c00; margin-top: 1rem; }

.color-range-settings {
  margin-bottom: 2rem;
  padding: 1.5rem;
  background: #fff;
  border-radius: 8px;
  border: 2px solid #ddd;
}
.color-range-settings h4 {
  margin-top: 0;
  margin-bottom: 1rem;
  font-size: 1.1rem;
}

.color-slider-wrap {
  position: relative;
}

.color-bar {
  display: flex;
  height: 6px;
  border-radius: 3px;
  overflow: hidden;
  pointer-events: none;
  position: absolute;
  left: 0;
  right: 0;
  top: 15px;
  z-index: 1;
}

.seg { height: 100%; }
.seg.green  { background: #10B981; }
.seg.yellow { background: #F59E0B; }
.seg.red    { background: #EF4444; }

.color-slider-wrap :deep(.el-slider) {
  position: relative;
  z-index: 2;
}

.color-slider-wrap :deep(.el-slider__runway) {
  background: transparent;
}

.color-slider-wrap :deep(.el-slider__bar) {
  background: transparent;
}

.card {
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 12px;
  padding: 1.5rem;
  margin-bottom: 1.5rem;
  box-shadow: 0 1px 4px rgba(0,0,0,.04);
}
.section-title {
  margin: 0 0 1rem;
  font-size: 1.1rem;
  font-weight: 700;
  color: #1f2937;
}
</style>
