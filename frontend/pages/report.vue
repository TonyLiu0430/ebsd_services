<template>
  <div class="report-page">
    <header class="page-header">
      <h1>EBSD 自動化報表</h1>
      <a href="/" class="back-link">← 返回分析頁</a>
    </header>

    <!-- ── Step 1: Upload ─────────────────────────── -->
    <section class="card">
      <h2 class="section-title">① 上傳 EBSD 資料夾</h2>
      <div
        class="upload-area"
        :class="{ active: pairs.length > 0 }"
        @click="triggerInput"
        @dragover.prevent
        @drop.prevent="handleDrop"
      >
        <input
          ref="folderInput"
          type="file"
          webkitdirectory
          directory
          multiple
          @change="handleSelect"
        />
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
    </section>

    <!-- ── Step 2: Sample selection ──────────────── -->
    <section v-if="pairs.length" class="card">
      <h2 class="section-title">② 選擇樣本</h2>
      <div class="selector-row">
        <div class="selector-group">
          <label>分析樣本</label>
          <select v-model="selectedSample" class="sel">
            <option value="">— 請選擇 —</option>
            <option v-for="s in samples" :key="s" :value="s">{{ s }}</option>
          </select>
        </div>
        <div class="selector-group">
          <label>黃金樣本（基準）</label>
          <select v-model="goldenSample" class="sel">
            <option value="">— 請選擇 —</option>
            <option v-for="s in samples" :key="s" :value="s">{{ s }}</option>
          </select>
        </div>
      </div>
      <div class="btn-row">
        <button class="gen-btn" @click="generateReport" :disabled="!canGenerate || loading">
          <span v-if="loading">處理中… ({{ doneCount }}/{{ totalTasks || pairs.length }})</span>
          <span v-else>產生報表</span>
        </button>
        <el-text v-if="error" type="danger" class="err-msg">{{ error }}</el-text>
      </div>
    </section>

    <!-- ══════════════ REPORT ══════════════ -->
    <template v-if="reportData">

      <!-- ── Section 1: Grain distribution curves ──────────── -->
      <section class="card">
        <h2 class="section-title">Grain 粒徑分布比對</h2>
        <div class="grain-mode-switch">
          <span class="grain-mode-label">圖形模式</span>
          <button
            class="grain-mode-btn"
            :class="{ active: grainChartMode === 'cdf' }"
            @click="grainChartMode = 'cdf'"
          > 累積曲線 </button>
          <button
            class="grain-mode-btn"
            :class="{ active: grainChartMode === 'hist' }"
            @click="grainChartMode = 'hist'"
          > 分布直方圖 </button>
          <button
            class="grain-mode-btn"
            :class="{ active: grainChartMode === 'area-hist' }"
            @click="grainChartMode = 'area-hist'"
          > 面積加權直方圖 </button>
        </div>
        <div v-if="grainChartMode === 'area-hist'" class="grain-mode-note">
          以 grain 等效直徑 d 換算面積權重，單顆 grain 權重採用 π(d/2)^2，圖上顯示各粒徑區間的面積占比。
        </div>
        <div class="chart-legend-row">
          <svg v-if="grainChartMode === 'cdf'" width="30" height="14"><line x1="0" y1="7" x2="30" y2="7" stroke="#3B82F6" stroke-width="2.5"/></svg>
          <svg v-else width="16" height="14">
            <rect x="2" y="2" width="12" height="10" fill="#0f766e" opacity="0.28"/>
            <rect x="2" y="2" width="12" height="10" fill="none" stroke="#0f766e" stroke-width="1"/>
          </svg>
          <span class="legend-lbl">{{ selectedSample }}</span>
          <svg v-if="grainChartMode === 'cdf'" width="30" height="14"><line x1="0" y1="7" x2="30" y2="7" stroke="#F59E0B" stroke-width="2.5" stroke-dasharray="5,2.5"/></svg>
          <svg v-else width="16" height="14">
            <rect x="2" y="2" width="12" height="10" fill="#F59E0B" opacity="0.28"/>
            <rect x="2" y="2" width="12" height="10" fill="none" stroke="#F59E0B" stroke-width="1" stroke-dasharray="2,1"/>
          </svg>
          <span class="legend-lbl">{{ goldenSample }} (黃金)</span>
        </div>

        <!-- 3×3 grid ordered: row=U/M/B, col=C/M/E -->
        <div class="nine-cdf-grid">
          <template v-for="(rowLabel, rowKey) in ROW_LABELS" :key="rowKey">
            <template v-for="colKey in COL_KEYS" :key="colKey">
              <div class="cdf-cell">
                <div class="cdf-pos-label">{{ colKey }}-{{ rowKey }}</div>
                <GrainCdfChart
                  :sample="getSampleGrains(`${colKey}-${rowKey}`)"
                  :golden="getGoldenGrains(`${colKey}-${rowKey}`)"
                  :sampleLabel="selectedSample"
                  :goldenLabel="goldenSample"
                  :mode="grainChartMode"
                />
              </div>
            </template>
          </template>
        </div>
      </section>

      <!-- ── Section 2: Nine-grid full data ────────── -->
      <section class="card">
        <h2 class="section-title">九宮格完整數據 — {{ currentVersionDisplayName }}</h2>
        <div class="version-switch-wrap">
          <div class="version-switch-head">
            <div>
              <div class="version-switch-label">資料版本</div>
              <div class="version-switch-current">{{ currentVersionDisplayName }}</div>
            </div>
            <div class="version-switch-note">紅底表示該位置會在下一個版本重測</div>
          </div>
          <div v-if="selectedSampleVersions.length > 1" class="version-slider-wrap">
            <el-slider
              v-model="selectedVersionIndex"
              :min="0"
              :max="selectedSampleVersions.length - 1"
              :step="1"
              :show-tooltip="true"
              :format-tooltip="formatVersionTooltip"
            />
            <div class="version-ticks">
              <span
                v-for="(version, idx) in selectedSampleVersions"
                :key="version.label"
                class="version-tick"
                :class="{ active: idx === selectedVersionIndex }"
              >{{ version.label }}</span>
            </div>
          </div>
        </div>
        <div class="nine-grid-wrapper">
          <!-- Header row -->
          <div class="ng-header-row">
            <div class="ng-corner"></div>
            <div v-for="(colLabel, colKey) in COL_LABELS" :key="colKey" class="ng-col-header">
              {{ colLabel }}<br/><small>({{ colKey }})</small>
            </div>
          </div>
          <!-- Data rows -->
          <div v-for="(rowLabel, rowKey) in ROW_LABELS" :key="rowKey" class="ng-data-row">
            <div class="ng-row-header">{{ rowLabel }}</div>
            <div
              v-for="(_, colKey) in COL_LABELS"
              :key="colKey"
              class="ng-cell"
              :class="{ 'ng-cell-retest': nextRetestPositions.has(`${colKey}-${rowKey}`) }"
            >
              <div class="ng-cell-inner">
                <!-- Grain stats -->
                <div class="stat-block">
                  <div class="stat-title">Grain Size (μm)</div>
                  <div class="stat-row">
                    <span class="stat-key">Mean</span>
                    <span class="stat-val">{{ fmtVersionGrain(colKey+'-'+rowKey, 'mean') }}</span>
                  </div>
                  <div class="stat-row">
                    <span class="stat-key">Max</span>
                    <span class="stat-val">{{ fmtVersionGrain(colKey+'-'+rowKey, 'max') }}</span>
                  </div>
                  <div class="stat-row">
                    <span class="stat-key">P75</span>
                    <span class="stat-val">{{ fmtVersionGrain(colKey+'-'+rowKey, 'p75') }}</span>
                  </div>
                  <div class="stat-row">
                    <span class="stat-key">Count</span>
                    <span class="stat-val">{{ fmtVersionGrain(colKey+'-'+rowKey, 'count') }}</span>
                  </div>
                </div>
                <!-- Orientation ratios -->
                <div class="stat-block orient-block">
                  <div class="stat-title">Orientation Ratio</div>
                  <table class="orient-table">
                    <thead>
                      <tr><th></th><th>[001]</th><th>[110]</th><th>[111]</th></tr>
                    </thead>
                    <tbody>
                      <tr>
                        <td class="dev-label">20°</td>
                        <td>{{ fmtVersionOrient(colKey+'-'+rowKey, '20%', 0) }}%</td>
                        <td>{{ fmtVersionOrient(colKey+'-'+rowKey, '20%', 1) }}%</td>
                        <td>{{ fmtVersionOrient(colKey+'-'+rowKey, '20%', 2) }}%</td>
                      </tr>
                      <tr>
                        <td class="dev-label">15°</td>
                        <td>{{ fmtVersionOrient(colKey+'-'+rowKey, '15%', 0) }}%</td>
                        <td>{{ fmtVersionOrient(colKey+'-'+rowKey, '15%', 1) }}%</td>
                        <td>{{ fmtVersionOrient(colKey+'-'+rowKey, '15%', 2) }}%</td>
                      </tr>
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          </div>
        </div>
      </section>

      <!-- ── Section 3a: Orientation triangles 20° ── -->
      <section class="card">
        <h2 class="section-title">晶粒取向比例 — Misorientation 20°</h2>
        <div class="orient-controls">
          <div class="orient-legend-row">
            <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#6b7280" stroke-width="1.5"/></svg>
            <span class="legend-lbl">{{ selectedSample }}（實線）</span>
            <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#6b7280" stroke-width="1.5" stroke-dasharray="4,2"/></svg>
            <span class="legend-lbl">{{ goldenSample }}（虛線）</span>
            <template v-for="info in ROW_SERIES_INFO" :key="info.rowKey">
              <span class="color-dot" :style="{ background: info.color }"></span>
              <span class="legend-lbl">{{ info.label }}</span>
            </template>
          </div>
          <div class="orient-toggle-bar">
            <div class="row-toggle-group">
              <span class="toggle-label">顯示位置:</span>
              <button
                v-for="info in ROW_SERIES_INFO"
                :key="info.rowKey"
                class="row-toggle-btn"
                :class="{ active: visibleRows.has(info.rowKey) }"
                :style="visibleRows.has(info.rowKey) ? { background: info.color, borderColor: info.color } : {}"
                @click="toggleRow(info.rowKey)"
              >{{ info.label }}</button>
            </div>
            <div class="label-toggle-group">
              <span class="toggle-label">數字顯示:</span>
              <button
                class="simple-toggle-btn"
                :class="{ active: showTriangleLabels }"
                @click="showTriangleLabels = true"
              >顯示</button>
              <button
                class="simple-toggle-btn"
                :class="{ active: !showTriangleLabels }"
                @click="showTriangleLabels = false"
              >隱藏</button>
            </div>
          </div>
        </div>
        <div class="triangle-row">
          <div v-for="(colLabel, colKey) in COL_LABELS" :key="colKey" class="triangle-panel">
            <div class="tri-col-title">{{ colLabel }} ({{ colKey }})</div>
            <TriangleChart :series="buildOrientSeries(colKey, '20%')"
              :showValueLabels="showTriangleLabels" />
          </div>
        </div>
      </section>

      <!-- ── Section 3b: Orientation triangles 15° ── -->
      <section class="card">
        <h2 class="section-title">晶粒取向比例 — Misorientation 15°</h2>
        <div class="orient-controls">
          <div class="orient-legend-row">
            <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#6b7280" stroke-width="1.5"/></svg>
            <span class="legend-lbl">{{ selectedSample }}（實線）</span>
            <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#6b7280" stroke-width="1.5" stroke-dasharray="4,2"/></svg>
            <span class="legend-lbl">{{ goldenSample }}（虛線）</span>
            <template v-for="info in ROW_SERIES_INFO" :key="info.rowKey">
              <span class="color-dot" :style="{ background: info.color }"></span>
              <span class="legend-lbl">{{ info.label }}</span>
            </template>
          </div>
          <div class="orient-toggle-bar">
            <div class="row-toggle-group">
              <span class="toggle-label">顯示位置:</span>
              <button
                v-for="info in ROW_SERIES_INFO"
                :key="info.rowKey"
                class="row-toggle-btn"
                :class="{ active: visibleRows.has(info.rowKey) }"
                :style="visibleRows.has(info.rowKey) ? { background: info.color, borderColor: info.color } : {}"
                @click="toggleRow(info.rowKey)"
              >{{ info.label }}</button>
            </div>
            <div class="label-toggle-group">
              <span class="toggle-label">數字顯示:</span>
              <button
                class="simple-toggle-btn"
                :class="{ active: showTriangleLabels }"
                @click="showTriangleLabels = true"
              >顯示</button>
              <button
                class="simple-toggle-btn"
                :class="{ active: !showTriangleLabels }"
                @click="showTriangleLabels = false"
              >隱藏</button>
            </div>
          </div>
        </div>
        <div class="triangle-row">
          <div v-for="(colLabel, colKey) in COL_LABELS" :key="colKey" class="triangle-panel">
            <div class="tri-col-title">{{ colLabel }} ({{ colKey }})</div>
            <TriangleChart :series="buildOrientSeries(colKey, '15%')"
              :showValueLabels="showTriangleLabels" />
          </div>
        </div>
      </section>

      <!-- ── Section 4: Python Analysis ──────── -->
      <section class="card">
        <h2 class="section-title">④ 特徵分析 </h2>
        <div class="feat-label">選擇分析項目</div>
        <el-checkbox-group v-model="selectedFeatures" class="feat-checkbox-group">
          <el-checkbox v-for="feat in featuresOptions || []" :key="feat" :label="feat" :value="feat" />
        </el-checkbox-group>
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
        <el-button type="primary" @click="analysis">套用分析</el-button>
        <div v-if="analysisRes" class="analysis-result-wrap">
          <h3>分析結果</h3>
          <div v-for="(sampleData, sampleName) in analysisRes" :key="sampleName" style="margin-bottom: 2rem;">
            <h4>{{ sampleName }}</h4>
            <ninepos :blocks="convertToNineposFormat(sampleData)" />
          </div>
        </div>
      </section>

    </template>
  </div>
</template>

<script setup lang="ts">
// ─── Types ────────────────────────────────────────────────────────────────────
interface FilePair { name: string; crc: File; cpr: File; sample: string; pos: string }
interface VersionedFilePair extends FilePair { versionNum: number; versionLabel: string }
interface SampleVersionMeta { label: string; versionNum: number }
type CppFeatures = {
  grains: number[]
  'orientation_ratio(20%)': number[]   // [001, 110, 111]  values 0-1
  'orientation_ratio(15%)': number[]
}
type AllDataResult = Record<string, Record<string, CppFeatures>>

// ─── Constants ────────────────────────────────────────────────────────────────
const COL_KEYS = ['C', 'M', 'E'] as const
const COL_LABELS: Record<string, string> = { C: '內 Center', M: '中 Middle', E: '外 Edge' }
const ROW_LABELS: Record<string, string> = { U: '上 Up', M: '中 Middle', B: '下 Bottom' }

// Colors per row position (U/M/B)
const ROW_SERIES_INFO = [
  { rowKey: 'U', color: '#EF4444', label: '上 (U)' },
  { rowKey: 'M', color: '#3B82F6', label: '中 (M)' },
  { rowKey: 'B', color: '#10B981', label: '下 (B)' },
]
// 控制三角圖顯示哪些 row 位置（同時套用於 20° 和 15° 圖）
const visibleRows = ref(new Set<string>(['U', 'M', 'B']))
const showTriangleLabels = ref(true)
function toggleRow(rowKey: string) {
  if (visibleRows.value.has(rowKey)) {
    if (visibleRows.value.size > 1) visibleRows.value.delete(rowKey) // 至少保留一個
  } else {
    visibleRows.value.add(rowKey)
  }
  visibleRows.value = new Set(visibleRows.value) // trigger reactivity
}

// ─── Reactive state ───────────────────────────────────────────────────────────
const folderInput = ref<HTMLInputElement>()
const pairs = ref<FilePair[]>([])
const versionedPairs = ref<VersionedFilePair[]>([])
const sampleVersionsMap = ref<Record<string, SampleVersionMeta[]>>({})
const samples = ref(new Set<string>())
const selectedSample = ref('')
const goldenSample = ref('')
const loading = ref(false)
const doneCount = ref(0)
const totalTasks = ref(0)
const error = ref('')
const reportData = ref<AllDataResult | null>(null)
const versionedReportData = ref<Record<string, Record<string, Record<string, CppFeatures>>>>({})
const selectedVersionIndex = ref(0)

// ─── Analysis state ──────────────────────────────────────────────────────────
type AnalysisResult = Record<string, Record<string, number>>
const { data: featuresOptions } = await useFetch<string[]>('/api/ebsd_features')
const selectedFeatures = ref<string[]>([])
const colorThresholds = ref([30, 70])
const analysisRes = ref<AnalysisResult | null>(null)
const grainChartMode = ref<'cdf' | 'hist' | 'area-hist'>('cdf')

const canGenerate = computed(
  () =>
    selectedSample.value !== '' &&
    goldenSample.value !== '' &&
    selectedSample.value !== goldenSample.value,
)

const selectedSampleVersions = computed(() => sampleVersionsMap.value[selectedSample.value] ?? [])
const currentVersionMeta = computed(() => {
  const versions = selectedSampleVersions.value
  if (!versions.length) return null
  const safeIdx = Math.min(selectedVersionIndex.value, versions.length - 1)
  return versions[safeIdx] ?? null
})
const currentVersionDisplayName = computed(() => currentVersionMeta.value?.label ?? selectedSample.value)

watch(
  selectedSample,
  () => {
    const versions = sampleVersionsMap.value[selectedSample.value] ?? []
    selectedVersionIndex.value = versions.length ? versions.length - 1 : 0
  },
  { immediate: true },
)

const currentVersionData = computed<Record<string, CppFeatures>>(() => {
  const sample = selectedSample.value
  const versions = selectedSampleVersions.value
  if (!sample || !versions.length) return {}
  const merged: Record<string, CppFeatures> = {}
  const rawByVersion = versionedReportData.value[sample] ?? {}
  const safeIdx = Math.min(selectedVersionIndex.value, versions.length - 1)
  for (let i = 0; i <= safeIdx; i++) {
    const versionLabel = versions[i]?.label
    if (!versionLabel) continue
    Object.assign(merged, rawByVersion[versionLabel] ?? {})
  }
  return merged
})

const nextRetestPositions = computed(() => {
  const sample = selectedSample.value
  const versions = selectedSampleVersions.value
  const nextMeta = versions[selectedVersionIndex.value + 1]
  if (!sample || !nextMeta) return new Set<string>()
  return new Set(Object.keys(versionedReportData.value[sample]?.[nextMeta.label] ?? {}))
})

// ─── Upload helpers ───────────────────────────────────────────────────────────
function triggerInput() { folderInput.value?.click() }
function handleSelect(e: Event) { processFiles((e.target as HTMLInputElement).files) }
function handleDrop(e: DragEvent) { processFiles(e.dataTransfer?.files) }

function standard_pos(pos: string): string {
  pos = pos.trim()
  const dashMatch = pos.match(/^([CME])-([UMB])$/i)
  if (dashMatch) return `${dashMatch[1].toUpperCase()}-${dashMatch[2].toUpperCase()}`
  const colMap: Record<string, string> = { E: 'E', I: 'C', M: 'M', A: 'C', B: 'M', C: 'E' }
  const rowMap: Record<string, string> = { U: 'U', M: 'M', B: 'B', D: 'B' }
  if (pos.length >= 2) {
    const col = colMap[pos[0].toUpperCase()]
    const row = rowMap[pos[1].toUpperCase()]
    if (col && row) return `${col}-${row}`
  }
  return pos
}

function processFiles(fileList: FileList | null | undefined) {
  if (!fileList?.length) return
  reportData.value = null
  versionedReportData.value = {}
  error.value = ''

  const allFiles = Array.from(fileList)
  const latestCrcFiles = new Map<string, { file: File; sample: string; pos: string; versionNum: number }>()
  const latestCprFiles = new Map<string, { file: File; versionNum: number }>()
  const versionedCrcFiles = new Map<string, { file: File; sample: string; pos: string; versionNum: number; versionLabel: string }>()
  const versionedCprFiles = new Map<string, { file: File; versionNum: number; versionLabel: string }>()
  const versionMetaMap = new Map<string, Map<number, string>>()

  samples.value = new Set()
  pairs.value = []
  versionedPairs.value = []
  sampleVersionsMap.value = {}
  selectedVersionIndex.value = 0

  for (const f of allFiles) {
    if (!f.name.endsWith('.crc') && !f.name.endsWith('.cpr')) continue
    const parts = f.webkitRelativePath.split('/')
    const subdir = (parts[1] || '').trim()
    if (!subdir) continue

    const match = subdir.match(/^(.*?)-(\d+)$/)
    const sample = (match?.[1] || subdir).trim()
    const versionNum = match ? Number.parseInt(match[2], 10) : 1
    const versionLabel = subdir
    const pos = standard_pos(f.name.replace(/\.(crc|cpr)$/i, '').trim())
    const latestKey = `${sample}-${pos}`
    const versionedKey = `${sample}__${versionLabel}__${pos}`

    samples.value.add(sample)
    if (!versionMetaMap.has(sample)) versionMetaMap.set(sample, new Map())
    versionMetaMap.get(sample)!.set(versionNum, versionLabel)

    if (f.name.endsWith('.crc')) {
      versionedCrcFiles.set(versionedKey, { file: f, sample, pos, versionNum, versionLabel })
      const existing = latestCrcFiles.get(latestKey)
      if (!existing || versionNum >= existing.versionNum) {
        latestCrcFiles.set(latestKey, { file: f, sample, pos, versionNum })
      }
    } else {
      versionedCprFiles.set(versionedKey, { file: f, versionNum, versionLabel })
      const existing = latestCprFiles.get(latestKey)
      if (!existing || versionNum >= existing.versionNum) {
        latestCprFiles.set(latestKey, { file: f, versionNum })
      }
    }
  }

  const versionsObj: Record<string, SampleVersionMeta[]> = {}
  for (const [sample, versionMap] of versionMetaMap.entries()) {
    versionsObj[sample] = Array.from(versionMap.entries())
      .sort((a, b) => a[0] - b[0])
      .map(([versionNum, label]) => ({ versionNum, label }))
  }
  sampleVersionsMap.value = versionsObj

  for (const [key, { file: crc, sample, pos, versionNum, versionLabel }] of versionedCrcFiles) {
    const cprEntry = versionedCprFiles.get(key)
    if (cprEntry) {
      versionedPairs.value.push({
        name: key,
        crc,
        cpr: cprEntry.file,
        sample,
        pos,
        versionNum,
        versionLabel,
      })
    }
  }

  for (const [key, { file: crc, sample, pos }] of latestCrcFiles) {
    const cprEntry = latestCprFiles.get(key)
    if (cprEntry) pairs.value.push({ name: key, crc, cpr: cprEntry.file, sample, pos })
  }
}

// ─── Generate report ──────────────────────────────────────────────────────────
async function generateReport() {
  if (!canGenerate.value) return
  loading.value = true
  error.value = ''
  reportData.value = null
  versionedReportData.value = {}
  doneCount.value = 0
  totalTasks.value = 0

  try {
    const neededLatestPairs = pairs.value.filter(
      (p) => p.sample === selectedSample.value || p.sample === goldenSample.value,
    )
    const neededVersionedPairs = versionedPairs.value.filter((p) => p.sample === selectedSample.value)

    const requestCache = new Map<string, Promise<CppFeatures>>()
    const requestKeys = new Set<string>()

    const getRequestKey = (p: { crc: File; cpr: File }) => `${p.crc.webkitRelativePath}__${p.cpr.webkitRelativePath}`
    for (const p of neededLatestPairs) requestKeys.add(getRequestKey(p))
    for (const p of neededVersionedPairs) requestKeys.add(getRequestKey(p))
    totalTasks.value = requestKeys.size

    const fetchFeatures = async (p: { crc: File; cpr: File }) => {
      const key = getRequestKey(p)
      const cached = requestCache.get(key)
      if (cached) return cached
      const promise = (async () => {
        const formData = new FormData()
        formData.append('crc', p.crc)
        formData.append('cpr', p.cpr)
        const features = await $fetch<CppFeatures>('/cppapi/features', {
          method: 'POST',
          body: formData,
        })
        doneCount.value++
        return features
      })()
      requestCache.set(key, promise)
      return promise
    }

    const latestResults = await Promise.all(
      neededLatestPairs.map(async (p) => ({
        sample: p.sample,
        pos: p.pos,
        features: await fetchFeatures(p),
      })),
    )

    const versionedResults = await Promise.all(
      neededVersionedPairs.map(async (p) => ({
        sample: p.sample,
        pos: p.pos,
        versionLabel: p.versionLabel,
        features: await fetchFeatures(p),
      })),
    )

    const latestData: AllDataResult = {}
    for (const r of latestResults) {
      if (!latestData[r.sample]) latestData[r.sample] = {}
      latestData[r.sample][r.pos] = r.features
    }
    reportData.value = latestData

    const versionData: Record<string, Record<string, Record<string, CppFeatures>>> = {}
    for (const r of versionedResults) {
      if (!versionData[r.sample]) versionData[r.sample] = {}
      if (!versionData[r.sample][r.versionLabel]) versionData[r.sample][r.versionLabel] = {}
      versionData[r.sample][r.versionLabel][r.pos] = r.features
    }
    versionedReportData.value = versionData
  } catch (e: unknown) {
    const err = e as { data?: { message?: string }; message?: string }
    error.value = err.data?.message || err.message || '未知錯誤'
  } finally {
    loading.value = false
  }
  console.log(reportData.value)
}

// ─── Data accessors ───────────────────────────────────────────────────────────
function getSampleGrains(pos: string): number[] {
  return reportData.value?.[selectedSample.value]?.[pos]?.grains ?? []
}
function getGoldenGrains(pos: string): number[] {
  return reportData.value?.[goldenSample.value]?.[pos]?.grains ?? []
}
function getCurrentVersionGrains(pos: string): number[] {
  return currentVersionData.value?.[pos]?.grains ?? []
}
function formatVersionTooltip(idx: number) {
  const meta = selectedSampleVersions.value[idx]
  return meta?.label ?? String(idx + 1)
}

function fmtVersionGrain(pos: string, stat: 'mean' | 'max' | 'p75' | 'count'): string {
  const g = getCurrentVersionGrains(pos)
  if (!g.length) return '—'
  if (stat === 'count') return String(g.length)
  const sorted = [...g].sort((a, b) => a - b)
  if (stat === 'mean') return (g.reduce((s, v) => s + v, 0) / g.length).toFixed(2)
  if (stat === 'max') return sorted[sorted.length - 1].toFixed(2)
  if (stat === 'p75') return sorted[Math.floor(sorted.length * 0.75)].toFixed(2)
  return '—'
}

function fmtVersionOrient(pos: string, dev: '20%' | '15%', idx: number): string {
  const key = `orientation_ratio(${dev})` as keyof CppFeatures
  const arr = currentVersionData.value?.[pos]?.[key] as number[] | undefined
  if (!arr) return '—'
  return (arr[idx] * 100).toFixed(1)
}

function fmtGrain(pos: string, stat: 'mean' | 'max' | 'p75' | 'count'): string {
  const g = getSampleGrains(pos)
  if (!g.length) return '—'
  if (stat === 'count') return String(g.length)
  const sorted = [...g].sort((a, b) => a - b)
  if (stat === 'mean') return (g.reduce((s, v) => s + v, 0) / g.length).toFixed(2)
  if (stat === 'max') return sorted[sorted.length - 1].toFixed(2)
  if (stat === 'p75') return sorted[Math.floor(sorted.length * 0.75)].toFixed(2)
  return '—'
}

function fmtOrient(pos: string, dev: '20%' | '15%', idx: number): string {
  const key = `orientation_ratio(${dev})` as keyof CppFeatures
  const arr = reportData.value?.[selectedSample.value]?.[pos]?.[key] as number[] | undefined
  if (!arr) return '—'
  return (arr[idx] * 100).toFixed(1)
}



// ─── Analysis helpers ────────────────────────────────────────────────────────
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
    center: {}, middle: {}, edge: {},
  }
  for (const [posKey, value] of Object.entries(sampleData)) {
    const mapping = positionMap[posKey]
    if (mapping) {
      const valuePercent = Number((value * 100).toFixed(1))
      result[mapping.col][mapping.row] = { value: valuePercent, color: getColorForValue(valuePercent) }
    }
  }
  return result
}

async function analysis() {
  if (selectedFeatures.value.length === 0 || goldenSample.value === '') {
    ElMessageBox.alert('請至少選擇一個分析項目及黃金樣本', '警告', { confirmButtonText: 'OK' })
    return
  }
  if (!reportData.value) {
    ElMessageBox.alert('請先產生報表', '警告', { confirmButtonText: 'OK' })
    return
  }
  analysisRes.value = null
  const res = await $fetch<AnalysisResult>('/api/analysis', {
    method: 'POST',
    body: {
      golden: goldenSample.value,
      features: selectedFeatures.value,
      data: reportData.value,
    },
  })
  analysisRes.value = res
}

// ─── Triangle chart series builder ───────────────────────────────────────────
function buildOrientSeries(colKey: string, dev: '20%' | '15%') {
  const ratioKey = `orientation_ratio(${dev})` as keyof CppFeatures
  const series: Array<{ name: string; color: string; dashed: boolean; values: [number, number, number] }> = []

  for (const { rowKey, color } of ROW_SERIES_INFO) {
    if (!visibleRows.value.has(rowKey)) continue  // 根據篩選略過
    const pos = `${colKey}-${rowKey}`

    // Sample series (solid)
    const sampleArr = reportData.value?.[selectedSample.value]?.[pos]?.[ratioKey] as number[] | undefined
    series.push({
      name: `${pos} (${selectedSample.value})`,
      color,
      dashed: false,
      values: sampleArr
        ? [(sampleArr[0] ?? 0) * 100, (sampleArr[1] ?? 0) * 100, (sampleArr[2] ?? 0) * 100]
        : [0, 0, 0],
    })

    // Golden series (dashed)
    const goldenArr = reportData.value?.[goldenSample.value]?.[pos]?.[ratioKey] as number[] | undefined
    series.push({
      name: `${pos} (${goldenSample.value})`,
      color,
      dashed: true,
      values: goldenArr
        ? [(goldenArr[0] ?? 0) * 100, (goldenArr[1] ?? 0) * 100, (goldenArr[2] ?? 0) * 100]
        : [0, 0, 0],
    })
  }
  return series
}
</script>

<style scoped>
/* ── Page layout ─────────────────────────────────────────────────────────────── */
.report-page {
  max-width: 1100px;
  margin: 0 auto;
  padding: 1.5rem 1rem 4rem;
  font-family: ui-sans-serif, system-ui, "Segoe UI", "Noto Sans TC", Arial, sans-serif;
  color: #111827;
}
.page-header {
  display: flex;
  align-items: baseline;
  gap: 1.5rem;
  margin-bottom: 1.5rem;
}
.page-header h1 { margin: 0; font-size: 1.7rem; }
.back-link { font-size: 0.85rem; color: #6b7280; text-decoration: none; }
.back-link:hover { color: #374151; text-decoration: underline; }

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

/* ── Upload ──────────────────────────────────────────────────────────────────── */
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

/* ── Sample selectors ────────────────────────────────────────────────────────── */
.selector-row {
  display: flex;
  align-items: flex-end;
  gap: 1rem;
  flex-wrap: wrap;
}
.selector-group { display: flex; flex-direction: column; gap: .3rem; }
.selector-group label { font-size: .8rem; font-weight: 600; color: #374151; }
.sel {
  padding: .45rem .7rem;
  border: 1px solid #d1d5db;
  border-radius: 6px;
  font-size: .9rem;
  min-width: 180px;
}
.arrow-icon { font-size: 1.4rem; color: #9ca3af; padding-bottom: .3rem; }
.btn-row { display: flex; align-items: center; gap: 1rem; margin-top: 1rem; }
.gen-btn {
  padding: .55rem 1.6rem;
  background: #2563EB;
  color: #fff;
  border: none;
  border-radius: 8px;
  font-size: .95rem;
  font-weight: 600;
  cursor: pointer;
  transition: background .15s;
}
.gen-btn:hover:not(:disabled) { background: #1d4ed8; }
.gen-btn:disabled { background: #93c5fd; cursor: not-allowed; }
.err-msg { font-size: .85rem; }

/* ── CDF grain curves ────────────────────────────────────────────────────────── */
.chart-legend-row, .orient-legend-row {
  display: flex;
  align-items: center;
  gap: .5rem;
  margin-bottom: 1rem;
  flex-wrap: wrap;
}
.legend-lbl { font-size: .82rem; color: #374151; margin-right: .5rem; }
.grain-mode-switch {
  display: flex;
  align-items: center;
  gap: .45rem;
  margin-bottom: .75rem;
  flex-wrap: wrap;
}
.grain-mode-label {
  font-size: .82rem;
  font-weight: 600;
  color: #4b5563;
}
.grain-mode-btn {
  border: 1px solid #d1d5db;
  background: #f9fafb;
  color: #4b5563;
  border-radius: 999px;
  padding: .24rem .75rem;
  font-size: .78rem;
  font-weight: 600;
  cursor: pointer;
}
.grain-mode-btn.active {
  border-color: #2563eb;
  background: #eff6ff;
  color: #1d4ed8;
}
.grain-mode-note {
  margin-bottom: .9rem;
  font-size: .78rem;
  line-height: 1.5;
  color: #6b7280;
}

.nine-cdf-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 12px;
}
.cdf-cell {
  background: #f9fafb;
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  padding: 8px 8px 4px;
}
.cdf-pos-label {
  font-size: .78rem;
  font-weight: 700;
  color: #374151;
  margin-bottom: 4px;
  text-align: center;
}

/* ── Nine-grid data ──────────────────────────────────────────────────────────── */

.version-switch-wrap {
  margin-bottom: 1rem;
  padding: .9rem 1rem;
  background: #f8fafc;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
}
.version-switch-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: .75rem 1rem;
  flex-wrap: wrap;
  margin-bottom: .75rem;
}
.version-switch-label {
  font-size: .78rem;
  font-weight: 700;
  color: #6b7280;
}
.version-switch-current {
  margin-top: .18rem;
  font-size: .95rem;
  font-weight: 700;
  color: #1f2937;
}
.version-switch-note {
  font-size: .78rem;
  font-weight: 600;
  color: #dc2626;
}
.version-slider-wrap {
  padding: 0 .35rem;
}
.version-ticks {
  display: flex;
  justify-content: space-between;
  gap: .4rem;
  margin-top: .35rem;
  flex-wrap: wrap;
}
.version-tick {
  font-size: .74rem;
  color: #6b7280;
}
.version-tick.active {
  color: #1d4ed8;
  font-weight: 700;
}

.nine-grid-wrapper { overflow-x: auto; }
.ng-header-row {
  display: grid;
  grid-template-columns: 80px repeat(3, 1fr);
  gap: 8px;
  margin-bottom: 4px;
}
.ng-corner { min-width: 0; }
.ng-col-header {
  background: #1e40af;
  color: #fff;
  border-radius: 6px;
  padding: 8px 10px;
  text-align: center;
  font-weight: 700;
  font-size: .85rem;
  line-height: 1.4;
}
.ng-data-row {
  display: grid;
  grid-template-columns: 80px repeat(3, 1fr);
  gap: 8px;
  margin-bottom: 8px;
}
.ng-row-header {
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
.ng-cell {
  background: #f9fafb;
  border: 1px solid #e5e7eb;
  border-left: 5px solid #1e40af;
  border-radius: 8px;
  overflow: hidden;
}
.ng-cell-retest {
  background: #fef2f2;
  border-color: #fecaca;
  border-left-color: #dc2626;
}
.ng-cell-inner {
  padding: 10px 12px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.stat-block { min-width: 0; }
.stat-title {
  font-size: .72rem;
  font-weight: 700;
  color: #6b7280;
  letter-spacing: .04em;
  margin-bottom: 4px;
}
.stat-row {
  display: flex;
  justify-content: space-between;
  font-size: .82rem;
  color: #374151;
  line-height: 1.55;
}
.stat-key { color: #9ca3af; }
.stat-val { font-weight: 600; font-variant-numeric: tabular-nums; }

.orient-block { margin-top: 4px; }
.orient-table {
  width: 100%;
  border-collapse: collapse;
  font-size: .76rem;
}
.orient-table th, .orient-table td {
  text-align: center;
  padding: 2px 4px;
  border-bottom: 1px solid #f3f4f6;
}
.orient-table th { color: #6b7280; font-weight: 600; }
.dev-label { color: #6b7280; font-weight: 700; text-align: left; }

/* ── Triangle charts ─────────────────────────────────────────────────────────── */
.orient-controls {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: .6rem 1.4rem;
  margin-bottom: 1rem;
}
.row-toggle-group {
  display: flex;
  align-items: center;
  gap: .4rem;
  flex-wrap: wrap;
}
.orient-toggle-bar {
  display: flex;
  align-items: center;
  gap: .75rem 1.25rem;
  flex-wrap: wrap;
}
.label-toggle-group {
  display: flex;
  align-items: center;
  gap: .4rem;
  flex-wrap: wrap;
}
.toggle-label {
  font-size: .8rem;
  font-weight: 600;
  color: #374151;
}
.row-toggle-btn {
  padding: .28rem .75rem;
  border: 1.5px solid #d1d5db;
  border-radius: 20px;
  background: #f9fafb;
  color: #6b7280;
  font-size: .78rem;
  font-weight: 600;
  cursor: pointer;
  transition: background .15s, color .15s, border-color .15s;
}
.row-toggle-btn.active {
  color: #fff;
}
.simple-toggle-btn {
  padding: .28rem .75rem;
  border: 1.5px solid #d1d5db;
  border-radius: 20px;
  background: #f9fafb;
  color: #6b7280;
  font-size: .78rem;
  font-weight: 600;
  cursor: pointer;
  transition: background .15s, color .15s, border-color .15s;
}
.simple-toggle-btn.active {
  background: #2563eb;
  border-color: #2563eb;
  color: #fff;
}
.triangle-row {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 12px;
}
.triangle-panel {
  background: #f9fafb;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  padding: 12px 8px 8px;
}
.tri-col-title {
  text-align: center;
  font-weight: 700;
  font-size: .88rem;
  color: #1e40af;
  margin-bottom: 4px;
}
.color-dot {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 50%;
}

/* ── Analysis section ───────────────────────────────────────────────────────── */
.feat-label { font-size: .88rem; font-weight: 600; color: #374151; margin-bottom: .4rem; }
.feat-checkbox-group { display: flex; flex-wrap: wrap; gap: .3rem .8rem; margin-bottom: 1rem; }

.color-range-settings {
  margin: 1rem 0;
  padding: 1.2rem 1.5rem;
  background: #f9fafb;
  border-radius: 8px;
  border: 1.5px solid #e5e7eb;
  max-width: 480px;
}
.color-range-settings h4 {
  margin: 0 0 .8rem;
  font-size: .95rem;
  color: #374151;
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
.color-slider-wrap :deep(.el-slider) { position: relative; z-index: 2; }
.color-slider-wrap :deep(.el-slider__runway) { background: transparent; }
.color-slider-wrap :deep(.el-slider__bar) { background: transparent; }

.analysis-result-wrap { margin-top: 1.5rem; }
.analysis-result-wrap h3 { font-size: 1rem; margin-bottom: 1rem; color: #1f2937; }
.analysis-result-wrap h4 { font-size: .9rem; font-weight: 700; color: #374151; margin-bottom: .5rem; }

/* ── Responsive ──────────────────────────────────────────────────────────────── */
@media (max-width: 700px) {
  .nine-cdf-grid { grid-template-columns: repeat(2, 1fr); }
  .triangle-row { grid-template-columns: 1fr; }
  .ng-header-row, .ng-data-row { grid-template-columns: 60px repeat(3, 1fr); }
}
</style>
