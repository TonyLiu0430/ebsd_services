<template>
  <div class="container">
    <h1>EBSD 資料夾選取</h1>
    
    <div class="upload-area" @click="triggerInput" @dragover.prevent @drop.prevent="handleDrop">
      <input ref="folderInput" type="file" webkitdirectory directory multiple @change="handleSelect" />
      <p>點擊或拖曳資料夾至此</p>
    </div>

    <div v-if="pairs.length" class="result">
      <h3>已選取: {{ selectedFolder }} 共 {{ samples.size }} 個靶材 ({{ pairs.length }} 個 ebsd 掃描資料)</h3>
      <div class="golden-sample-selector">
        <label for="goldenSampleSelect">選擇 Golden Sample:</label>
        <select id="goldenSampleSelect" v-model="goldenSample">
          <option value="">-- 未選擇 --</option>
          <option v-for="sample_name in samples" :key="sample_name" :value="sample_name">
            {{ sample_name }}
          </option>
        </select>
      </div>
      <div v-for="(sample_name) in samples" :key="sample_name" class="pair-row">
        <span>{{ sample_name }}</span>
      </div>
      <div></div>
      <button @click="submit" :disabled="loading">{{ loading ? '處理中...' : '送出' }}</button>
      <!-- <pre v-if="result">{{ result }}</pre> -->
      <Ninepos v-if="result"></Ninepos>
      <p v-if="error" class="error">{{ error }}</p>
    </div>
  </div>
</template>

<script setup lang="ts">
interface FilePair { name: string; crc: File; cpr: File; sample: string; pos: string }
type AllDataResult = Record<string, Record<string, number[]>>

const folderInput = ref<HTMLInputElement>()
const selectedFolder = ref('')
const samples = ref(new Set<string>())
const pairs = ref<FilePair[]>([])
const loading = ref(false)
const result = ref<AllDataResult | null>(null)
const error = ref('')
const goldenSample = ref('')

function triggerInput() { folderInput.value?.click() }

function handleSelect(e: Event) { processFiles((e.target as HTMLInputElement).files) }

function handleDrop(e: DragEvent) { processFiles(e.dataTransfer?.files) }

function processFiles(fileList: FileList | null | undefined) {
  if (!fileList?.length) return
  const allFiles = Array.from(fileList)
  selectedFolder.value = allFiles[0]?.webkitRelativePath.split('/')[0] || ''
  
  const crcFiles = new Map<string, { file: File; sample: string; pos: string; num: string }>()
  const cprFiles = new Map<string, File>()
  
  
  for (const f of allFiles) {
    if (!f.name.endsWith('.crc') && !f.name.endsWith('.cpr')) continue
    const parts = f.webkitRelativePath.split('/')
    const subdir = parts[1] || ''
    const lastDash = subdir.lastIndexOf('-')
    const dataPatch = subdir.slice(0, lastDash)
    const num = subdir.slice(lastDash + 1)
    const pos = f.name.replace(/\.(crc|cpr)$/i, '')
    const key = `${dataPatch}-${pos}`
    samples.value.add(dataPatch) // sample
    if (f.name.endsWith('.crc')) {
      const existing = crcFiles.get(key)
      if (!existing || num === '02') crcFiles.set(key, { file: f, sample: dataPatch, pos, num })
    } else {
      const existing = cprFiles.get(key)
      if (!existing || num === '02') cprFiles.set(key, f)
    }
  }
  pairs.value = []
  for (const [key, { file: crc, sample, pos }] of crcFiles) {
    const cpr = cprFiles.get(key)
    if (cpr) pairs.value.push({ name: key, crc, cpr, sample, pos })
  }
}

async function submit() {
  loading.value = true
  error.value = ''
  result.value = null
  
  const formData = new FormData()
  const metas: { sample: string; pos: string }[] = []
  
  for (const p of pairs.value) {
    formData.append('files', p.crc)
    formData.append('files', p.cpr)
    metas.push({ sample: p.sample, pos: p.pos })
  }
  formData.append('meta_data', JSON.stringify(metas))
  
  try {
    result.value = await $fetch<AllDataResult>('/api/all_data', { method: 'POST', body: formData })
  } catch (e: unknown) {
    const err = e as { data?: { message?: string }; message?: string }
    error.value = err.data?.message || err.message || 'Unknown error'
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.container { max-width: 800px; margin: 0 auto; padding: 2rem; }
h1 { margin-bottom: 2rem; }
.upload-area { border: 2px dashed #ccc; border-radius: 8px; padding: 3rem; text-align: center; cursor: pointer; }
.upload-area:hover { border-color: #999; }
.upload-area input { display: none; }
.result { margin-top: 2rem; padding: 1rem; background: #f5f5f5; border-radius: 8px; }
.result h3 { margin-bottom: 1rem; }
.pair-row { display: flex; gap: 0.5rem; margin-bottom: 0.5rem; align-items: center; }
.pair-row span { flex: 1; font-size: 0.9rem; }
.pair-row input { width: 100px; padding: 0.3rem; border: 1px solid #ccc; border-radius: 4px; }
button { margin-top: 1rem; padding: 0.5rem 1.5rem; background: #333; color: #fff; border: none; border-radius: 4px; cursor: pointer; }
button:disabled { background: #999; }
pre { margin-top: 1rem; background: #fff; padding: 1rem; border-radius: 4px; overflow: auto; max-height: 300px; font-size: 0.8rem; }
.error { color: #c00; margin-top: 1rem; }
</style>
