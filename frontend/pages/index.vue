<template>
  <div class="library-page">
    <header class="page-head">
      <div>
        <h1>EBSD 檔案庫</h1>
        <p>{{ auth.user.value?.username || '目前帳號' }} · {{ targetOptions.length }} 筆靶材 · {{ storedPairs.length }} 組掃描檔</p>
      </div>
      <button class="ghost-btn" type="button" :disabled="loading" @click="refreshAll">
        {{ loading ? '更新中' : '重新整理' }}
      </button>
    </header>

    <section class="workspace-grid">
      <div class="panel target-panel">
        <div class="panel-title-row">
          <h2>靶材</h2>
          <span>{{ targetOptions.length }} 筆</span>
        </div>

        <div class="create-target-row">
          <input
            v-model.trim="newTargetName"
            type="text"
            placeholder="新增靶材名稱"
            @keydown.enter.prevent="createTarget"
          />
          <button class="primary-btn" type="button" :disabled="!canCreateTarget || creatingTarget" @click="createTarget">
            {{ creatingTarget ? '新增中' : '新增' }}
          </button>
        </div>
        <el-text v-if="targetError" type="danger">{{ targetError }}</el-text>

        <div v-if="targetOptions.length === 0 && !loading" class="empty-state">尚未新增靶材</div>
        <div v-else class="target-list">
          <button
            v-for="target in targetOptions"
            :key="target.name"
            type="button"
            class="target-item"
            :class="{ active: selectedTarget === target.name }"
            @click="selectedTarget = target.name"
          >
            <span>{{ target.name }}</span>
            <small>{{ target.scanCount }} 組掃描檔</small>
          </button>
        </div>
      </div>

      <div class="panel upload-panel">
        <div class="panel-title-row">
          <h2>新增位置掃描檔</h2>
          <span class="pair-badge" :class="{ ready: hasCompletePair }">
            {{ hasCompletePair ? 'CRC + CPR 已選齊' : '等待成對檔案' }}
          </span>
        </div>

        <label class="field">
          <span>選擇靶材</span>
          <select v-model="selectedTarget">
            <option value="">請先選擇或新增靶材</option>
            <option v-for="target in targetOptions" :key="target.name" :value="target.name">
              {{ target.name }}
            </option>
          </select>
        </label>

        <div class="version-select-grid">
          <label class="field">
            <span>版本</span>
            <select v-model="singleForm.versionChoice">
              <option
                v-for="version in selectedTargetVersionOptions"
                :key="version.key"
                :value="version.key"
              >
                {{ version.label }}
              </option>
              <option value="__new__">新增版本</option>
            </select>
          </label>
          <label v-if="singleForm.versionChoice === '__new__'" class="field">
            <span>版本編號</span>
            <input v-model.number="singleForm.newVersionNum" type="number" min="1" step="1" />
          </label>
        </div>

        <div class="single-position-panel">
          <div class="position-picker-head">
            <span>位置</span>
            <small>可點選或手動輸入，保存時會統一成 C-U 格式</small>
          </div>
          <div class="single-position-map">
            <div class="single-axis-labels">
              <span>內</span>
              <span>中</span>
              <span>外</span>
            </div>
            <div class="single-map-grid">
              <div v-for="row in POSITION_ROWS" :key="row.label" class="single-map-row">
                <span class="row-label">{{ row.label }}</span>
                <button
                  v-for="pos in row.positions"
                  :key="pos"
                  type="button"
                  class="position-pick-btn"
                  :class="{ active: normalizedSinglePosition === pos }"
                  @click="setSinglePosition(pos)"
                >
                  {{ pos }}
                </button>
              </div>
            </div>
          </div>
          <label class="field">
            <span>手動位置</span>
            <input
              v-model.trim="singleForm.position"
              type="text"
              placeholder="C-U"
              @blur="normalizeSinglePositionInput"
            />
          </label>
        </div>

        <div class="file-pair-grid">
          <label class="file-drop" :class="{ selected: !!crcFile }">
            <input ref="crcInput" type="file" accept=".crc" @change="handleSingleFile('crc', $event)" />
            <span class="file-kind">CRC</span>
            <strong>{{ crcFile?.name || '選擇 .crc' }}</strong>
            <small>{{ crcFile ? formatBytes(crcFile.size) : '必填' }}</small>
          </label>

          <label class="file-drop" :class="{ selected: !!cprFile }">
            <input ref="cprInput" type="file" accept=".cpr" @change="handleSingleFile('cpr', $event)" />
            <span class="file-kind">CPR</span>
            <strong>{{ cprFile?.name || '選擇 .cpr' }}</strong>
            <small>{{ cprFile ? formatBytes(cprFile.size) : '必填' }}</small>
          </label>
        </div>

        <div class="action-row">
          <button class="primary-btn" type="button" :disabled="!canUploadSingle || uploading" @click="uploadSinglePair">
            {{ uploading ? '保存中' : '保存到靶材' }}
          </button>
          <button class="ghost-btn" type="button" :disabled="uploading" @click="resetUploadForm">清除</button>
          <el-text v-if="uploadError" type="danger">{{ uploadError }}</el-text>
          <el-text v-else-if="uploadMessage" type="success">{{ uploadMessage }}</el-text>
        </div>
      </div>
    </section>

    <section class="panel folder-panel">
      <div class="panel-title-row">
        <h2>資料夾批次匯入</h2>
        <span>{{ folderPairs.length }} 組已配對</span>
      </div>
      <div
        class="folder-drop"
        :class="{ active: folderPairs.length > 0 }"
        @click="folderInput?.click()"
        @dragover.prevent
        @drop.prevent="handleFolderDrop"
      >
        <input
          ref="folderInput"
          type="file"
          webkitdirectory
          directory
          multiple
          @change="handleFolderSelect"
        />
        <strong>{{ folderPairs.length ? '已解析資料夾' : '點擊或拖曳 EBSD 資料夾' }}</strong>
        <span v-if="folderPairs.length">
          {{ folderTargets.size }} 筆靶材，{{ folderVersionCount }} 個版本，{{ folderPairs.length }} 組 .crc/.cpr
        </span>
        <span v-else>會自動配對同位置的 .crc 與 .cpr；資料夾結尾為 -1、-2、-3 會視為同一靶材的版本，沒有數字則視為 -1。</span>
      </div>

      <div v-if="folderPairs.length" class="folder-summary">
        <div v-for="target in Array.from(folderTargets).sort(sortText)" :key="target">
          <strong>{{ target }}</strong>
          <span>{{ folderTargetSummary(target) }}</span>
        </div>
      </div>

      <div class="action-row">
        <button class="primary-btn" type="button" :disabled="folderPairs.length === 0 || importingFolder" @click="importFolderPairs">
          {{ importingFolder ? `匯入中 ${folderImportedCount}/${folderPairs.length}` : '匯入資料夾' }}
        </button>
        <button class="ghost-btn" type="button" :disabled="importingFolder" @click="resetFolderImport">清除資料夾</button>
        <el-text v-if="folderError" type="danger">{{ folderError }}</el-text>
        <el-text v-else-if="folderMessage" type="success">{{ folderMessage }}</el-text>
      </div>
    </section>

    <section class="panel list-panel">
      <div class="panel-title-row">
        <h2>目前帳號檔案</h2>
        <span>{{ filteredPairs.length }} / {{ storedPairs.length }} 組</span>
      </div>

      <div class="filter-row">
        <label class="field compact">
          <span>靶材篩選</span>
          <select v-model="listTargetFilter">
            <option value="">全部靶材</option>
            <option v-for="target in targetOptions" :key="target.name" :value="target.name">
              {{ target.name }}
            </option>
          </select>
        </label>
      </div>

      <div v-if="listError" class="empty-state error-state">{{ listError }}</div>
      <div v-else-if="loading" class="empty-state">讀取中</div>
      <div v-else-if="storedPairs.length === 0" class="empty-state">尚未保存 EBSD 檔案</div>
      <div v-else-if="filteredPairs.length === 0" class="empty-state">這個靶材尚未新增掃描檔</div>
      <div v-else class="target-groups">
        <section v-for="group in groupedPairs" :key="group.sample" class="target-group">
          <div class="target-card-head">
            <div>
              <h3>{{ group.sample }}</h3>
              <p>{{ group.displayTotalPositions }} 個位置 · {{ group.versions.length }} 個版本 · 最後更新 {{ formatDate(group.lastUpdated) }}</p>
            </div>
            <div class="target-card-actions">
              <div class="coverage-meter">
                <strong>{{ group.displayStandardCount }}</strong>
                <span>/ 9 位置</span>
                <small :class="{ complete: group.displayMissingCount === 0 }">
                  {{ group.displayMissingCount === 0 ? '標準 9 點已齊' : `缺 ${group.displayMissingCount} 個標準位置` }}
                </small>
              </div>
              <button
                class="danger-btn"
                type="button"
                :disabled="deletingTarget === group.sample"
                @click="deleteTarget(group.sample)"
              >
                {{ deletingTarget === group.sample ? '刪除中' : '刪除靶材' }}
              </button>
            </div>
          </div>

          <div class="mini-map-wrap">
            <div class="mini-wafer-map">
              <div class="axis-labels">
                <span>內</span>
                <span>中</span>
                <span>外</span>
              </div>
              <div class="mini-map-grid">
                <div v-for="row in POSITION_ROWS" :key="row.label" class="mini-map-row">
                  <span class="row-label">{{ row.label }}</span>
                  <span
                    v-for="pos in row.positions"
                    :key="pos"
                    class="map-dot"
                    :class="{ present: group.displayPositions.has(pos), missing: !group.displayPositions.has(pos) }"
                    :title="`${pos} ${group.displayPositions.has(pos) ? '有資料' : '缺少'}`"
                  >
                    {{ pos }}
                  </span>
                </div>
              </div>
            </div>

            <div class="map-legend">
              <span><i class="legend-dot present"></i>{{ group.displayLabel }} 有資料</span>
              <span><i class="legend-dot missing"></i>缺少</span>
            </div>

            <div v-if="group.displayExtraPositions.length" class="extra-position-row">
              <span
                v-for="pos in group.displayExtraPositions"
                :key="pos"
                class="pos-chip extra present"
              >
                {{ pos }}
              </span>
            </div>

            <div class="version-strip">
              <button
                type="button"
                class="version-pill complete-state"
                :class="{ active: group.activeViewKey === 'complete' }"
                @click="selectVersionView(group.sample, 'complete')"
              >
                <strong>完整位置</strong>
                <span>{{ group.totalPositions }} 個位置</span>
              </button>
              <button
                v-for="version in group.versions"
                :key="version.key"
                type="button"
                class="version-pill"
                :class="{ active: group.activeViewKey === version.key, latest: version.key === group.latestVersionKey }"
                @click="selectVersionView(group.sample, version.key)"
              >
                <strong>{{ version.label }}</strong>
                <span>{{ version.positions.length }} 個位置</span>
              </button>
            </div>
          </div>

          <details class="details-panel">
            <summary>檔案明細</summary>
            <div class="scan-list">
              <article v-for="pair in group.items" :key="pair.id" class="scan-item">
                <div>
                  <span class="pos-chip present">{{ pair.position }}</span>
                  <strong>{{ pair.name }}</strong>
                  <small>{{ pair.version_label || pair.sample }}</small>
                  <button
                    class="inline-danger-btn"
                    type="button"
                    :disabled="deletingPairIds.has(pair.id)"
                    @click="deletePair(pair)"
                  >
                    {{ deletingPairIds.has(pair.id) ? '刪除中' : '移除' }}
                  </button>
                </div>
                <dl>
                  <div>
                    <dt>CRC</dt>
                    <dd>{{ pair.crc_filename }} · {{ formatBytes(pair.crc_size_bytes) }}</dd>
                  </div>
                  <div>
                    <dt>CPR</dt>
                    <dd>{{ pair.cpr_filename }} · {{ formatBytes(pair.cpr_size_bytes) }}</dd>
                  </div>
                </dl>
              </article>
            </div>
          </details>
        </section>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
definePageMeta({
  middleware: 'auth',
})

type StoredTarget = {
  id: string
  name: string
  created_at: string
  updated_at: string
}

type StoredEbsdPair = {
  id: string
  pair_id: string
  pair_hash: string
  crc_filename: string
  crc_size_bytes: number
  crc_hash: string
  cpr_filename: string
  cpr_size_bytes: number
  cpr_hash: string
  sample: string
  position: string
  name: string
  folder?: string | null
  version_key?: string | null
  version_label?: string | null
  version_num?: number | null
  created_at: string
  updated_at: string
}

type SaveEbsdPairResponse = {
  id: string
  pair_id: string
  pair_hash: string
  crc_hash: string
  cpr_hash: string
}

type LocalFolderPair = {
  name: string
  crc: File
  cpr: File
  sample: string
  position: string
  folder: string
  versionKey: string
  versionLabel: string
  versionNum: number
}

const STANDARD_POSITIONS = ['C-U', 'C-M', 'C-B', 'M-U', 'M-M', 'M-B', 'E-U', 'E-M', 'E-B'] as const
type StandardPosition = typeof STANDARD_POSITIONS[number]
const POSITION_ROWS = [
  { label: '上', positions: ['C-U', 'M-U', 'E-U'] },
  { label: '中', positions: ['C-M', 'M-M', 'E-M'] },
  { label: '下', positions: ['C-B', 'M-B', 'E-B'] },
] as const

const auth = useAuth()
const storedTargets = ref<StoredTarget[]>([])
const storedPairs = ref<StoredEbsdPair[]>([])
const selectedTarget = ref('')
const listTargetFilter = ref('')
const newTargetName = ref('')
const loading = ref(false)
const creatingTarget = ref(false)
const uploading = ref(false)
const importingFolder = ref(false)
const listError = ref('')
const targetError = ref('')
const uploadError = ref('')
const uploadMessage = ref('')
const folderError = ref('')
const folderMessage = ref('')
const crcFile = ref<File | null>(null)
const cprFile = ref<File | null>(null)
const crcInput = ref<HTMLInputElement>()
const cprInput = ref<HTMLInputElement>()
const folderInput = ref<HTMLInputElement>()
const folderPairs = ref<LocalFolderPair[]>([])
const folderTargets = ref(new Set<string>())
const folderImportedCount = ref(0)
const selectedVersionView = ref<Record<string, string>>({})
const deletingPairIds = ref(new Set<string>())
const deletingTarget = ref('')
const singleForm = reactive({
  position: '',
  versionChoice: 'v1',
  newVersionNum: 1,
})

const targetOptions = computed(() => {
  const map = new Map<string, { name: string; scanCount: number }>()
  for (const target of storedTargets.value) {
    map.set(target.name, { name: target.name, scanCount: 0 })
  }
  for (const pair of storedPairs.value) {
    const existing = map.get(pair.sample)
    if (existing) existing.scanCount += 1
    else map.set(pair.sample, { name: pair.sample, scanCount: 1 })
  }
  return Array.from(map.values()).sort((a, b) =>
    a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: 'base' }),
  )
})

const hasCompletePair = computed(() => !!crcFile.value && !!cprFile.value)
const canCreateTarget = computed(() =>
  newTargetName.value.trim() !== '' &&
  !targetOptions.value.some((target) => target.name === newTargetName.value.trim()),
)
const canUploadSingle = computed(() =>
  selectedTarget.value !== '' &&
  hasCompletePair.value &&
  singleForm.position.trim() !== '',
)
const filteredPairs = computed(() => {
  const target = listTargetFilter.value
  return target ? storedPairs.value.filter((pair) => pair.sample === target) : storedPairs.value
})
const selectedTargetVersionOptions = computed(() => {
  if (!selectedTarget.value) return [{ key: 'v1', label: '-1', num: 1 }]

  const versionMap = new Map<string, { key: string; label: string; num: number }>()
  for (const pair of storedPairs.value.filter((row) => row.sample === selectedTarget.value)) {
    const num = pair.version_num ?? 1
    const key = pair.version_key || `v${num}`
    const label = `-${num}`
    versionMap.set(key, { key, label, num })
  }

  if (!versionMap.has('v1')) {
    versionMap.set('v1', { key: 'v1', label: '-1', num: 1 })
  }

  return Array.from(versionMap.values()).sort((a, b) =>
    a.num - b.num || sortText(a.label, b.label),
  )
})
const nextSingleVersionNum = computed(() =>
  Math.max(1, ...selectedTargetVersionOptions.value.map((version) => version.num)) + 1,
)
const selectedSingleVersion = computed(() => {
  if (!selectedTarget.value) return { key: 'v1', label: '-1', num: 1 }

  if (singleForm.versionChoice === '__new__') {
    const num = Math.max(1, Number(singleForm.newVersionNum) || nextSingleVersionNum.value)
    return {
      key: `v${num}`,
      label: `-${num}`,
      num,
    }
  }

  return selectedTargetVersionOptions.value.find((version) => version.key === singleForm.versionChoice) ??
    selectedTargetVersionOptions.value[0] ??
    { key: 'v1', label: '-1', num: 1 }
})
const normalizedSinglePosition = computed(() => normalizeStandardPosition(singleForm.position))
const folderVersionCount = computed(() => new Set(folderPairs.value.map((pair) => `${pair.sample}::${pair.versionKey}`)).size)
const groupedPairs = computed(() => {
  const map = new Map<string, StoredEbsdPair[]>()
  for (const pair of filteredPairs.value) {
    const items = map.get(pair.sample) ?? []
    items.push(pair)
    map.set(pair.sample, items)
  }
  return Array.from(map.entries())
    .map(([sample, items]) => {
      const sortedItems = [...items].sort((a, b) =>
        versionNum(a) - versionNum(b) ||
        a.position.localeCompare(b.position, undefined, { numeric: true, sensitivity: 'base' }),
      )
      const versionMap = new Map<string, StoredEbsdPair[]>()
      for (const item of sortedItems) {
        const itemNum = versionNum(item)
        const key = item.version_key || `v${itemNum}`
        const versionItems = versionMap.get(key) ?? []
        versionItems.push(item)
        versionMap.set(key, versionItems)
      }
      const versions = Array.from(versionMap.entries())
        .map(([key, versionItems]) => ({
          key,
          label: `-${versionItems[0]?.version_num ?? 1}`,
          num: versionItems[0]?.version_num ?? 1,
          positions: Array.from(new Set(versionItems.map((item) => item.position))).sort(sortText),
        }))
        .sort((a, b) => a.num - b.num || sortText(a.label, b.label))
      const latestVersion = versions[versions.length - 1]
      const latestNum = latestVersion?.num ?? 1
      const cumulativeByPos = new Map<string, StoredEbsdPair>()
      for (const item of sortedItems) {
        if (versionNum(item) > latestNum) continue
        const existing = cumulativeByPos.get(item.position)
        if (!existing || versionNum(item) >= versionNum(existing)) {
          cumulativeByPos.set(item.position, item)
        }
      }
      const positions = new Set(cumulativeByPos.keys())
      const requestedViewKey = selectedVersionView.value[sample] ?? 'complete'
      const selectedVersion = versions.find((version) => version.key === requestedViewKey)
      const activeViewKey = selectedVersion ? selectedVersion.key : 'complete'
      const displayPositions = selectedVersion
        ? new Set(selectedVersion.positions)
        : positions
      const displayStandardCount = STANDARD_POSITIONS.filter((pos) => displayPositions.has(pos)).length
      const displayExtraPositions = Array.from(displayPositions)
        .filter((pos) => !STANDARD_POSITIONS.includes(pos as typeof STANDARD_POSITIONS[number]))
        .sort(sortText)
      const standardCount = STANDARD_POSITIONS.filter((pos) => positions.has(pos)).length
      const extraPositions = Array.from(positions)
        .filter((pos) => !STANDARD_POSITIONS.includes(pos as typeof STANDARD_POSITIONS[number]))
        .sort(sortText)

      return {
        sample,
        items: sortedItems,
        positions,
        standardCount,
        missingCount: STANDARD_POSITIONS.length - standardCount,
        extraPositions,
        totalPositions: positions.size,
        displayPositions,
        displayStandardCount,
        displayMissingCount: STANDARD_POSITIONS.length - displayStandardCount,
        displayExtraPositions,
        displayTotalPositions: displayPositions.size,
        displayLabel: selectedVersion?.label ?? '完整位置',
        activeViewKey,
        versions,
        latestVersionKey: latestVersion?.key ?? 'v1',
        latestVersionLabel: latestVersion?.label ?? '-1',
        lastUpdated: sortedItems
          .map((item) => item.updated_at || item.created_at)
          .sort((a, b) => new Date(b).getTime() - new Date(a).getTime())[0] ?? '',
      }
    })
    .sort((a, b) => a.sample.localeCompare(b.sample, undefined, { numeric: true, sensitivity: 'base' }))
})

watch(targetOptions, (targets) => {
  if (!selectedTarget.value && targets.length) selectedTarget.value = targets[0].name
  if (selectedTarget.value && !targets.some((target) => target.name === selectedTarget.value)) {
    selectedTarget.value = targets[0]?.name ?? ''
  }
  if (listTargetFilter.value && !targets.some((target) => target.name === listTargetFilter.value)) {
    listTargetFilter.value = ''
  }
})

watch(selectedTargetVersionOptions, (versions) => {
  if (!versions.some((version) => version.key === singleForm.versionChoice) && singleForm.versionChoice !== '__new__') {
    singleForm.versionChoice = versions[0]?.key ?? 'v1'
  }
  singleForm.newVersionNum = nextSingleVersionNum.value
}, { immediate: true })

onMounted(async () => {
  await auth.refreshUser()
  await refreshAll()
})

async function refreshAll() {
  loading.value = true
  listError.value = ''
  try {
    const [targets, pairs] = await Promise.all([
      $fetch<StoredTarget[]>('/api/ebsd/targets', { credentials: 'include' }),
      $fetch<StoredEbsdPair[]>('/api/ebsd/pairs/metadata', { credentials: 'include' }),
    ])
    storedTargets.value = targets
    storedPairs.value = pairs
  } catch (e: unknown) {
    const err = e as { data?: { detail?: string; message?: string }; message?: string }
    listError.value = err.data?.detail || err.data?.message || err.message || '讀取檔案庫失敗'
  } finally {
    loading.value = false
  }
}

async function createTarget() {
  const name = newTargetName.value.trim()
  if (!name || creatingTarget.value) return

  targetError.value = ''
  creatingTarget.value = true
  try {
    const body = new FormData()
    body.append('name', name)
    const created = await $fetch<StoredTarget & { created: boolean }>('/api/ebsd/targets', {
      method: 'POST',
      body,
      credentials: 'include',
    })
    if (!storedTargets.value.some((target) => target.name === created.name)) {
      storedTargets.value = [...storedTargets.value, created]
    }
    selectedTarget.value = created.name
    listTargetFilter.value = created.name
    newTargetName.value = ''
  } catch (e: unknown) {
    const err = e as { data?: { detail?: string; message?: string }; message?: string }
    targetError.value = err.data?.detail || err.data?.message || err.message || '新增靶材失敗'
  } finally {
    creatingTarget.value = false
  }
}

function handleFolderSelect(event: Event) {
  processFolderFiles((event.target as HTMLInputElement).files)
}

function handleFolderDrop(event: DragEvent) {
  processFolderFiles(event.dataTransfer?.files)
}

function processFolderFiles(fileList: FileList | null | undefined) {
  folderError.value = ''
  folderMessage.value = ''
  folderImportedCount.value = 0
  folderPairs.value = []
  folderTargets.value = new Set()
  if (!fileList?.length) return

  const crcFiles = new Map<string, { file: File; sample: string; position: string; folder: string; versionKey: string; versionLabel: string; versionNum: number }>()
  const cprFiles = new Map<string, File>()

  for (const file of Array.from(fileList)) {
    const lowerName = file.name.toLowerCase()
    if (!lowerName.endsWith('.crc') && !lowerName.endsWith('.cpr')) continue

    const relativePath = file.webkitRelativePath || file.name
    const parts = relativePath.split('/').filter(Boolean)
    const folder = resolveTargetFolder(parts)
    if (!folder) continue

    const versionInfo = parseTargetVersion(folder)
    const sample = versionInfo.sample
    const position = standardPos(file.name.replace(/\.(crc|cpr)$/i, '').trim())
    const key = `${sample}::${versionInfo.versionKey}::${position}`
    folderTargets.value.add(sample)

    if (lowerName.endsWith('.crc')) {
      crcFiles.set(key, { file, sample, position, folder, ...versionInfo })
    } else {
      cprFiles.set(key, file)
    }
  }

  const matched: LocalFolderPair[] = []
  for (const [key, info] of crcFiles) {
    const cpr = cprFiles.get(key)
    if (!cpr) continue
    matched.push({
      name: `${info.sample}-${info.position}`,
      crc: info.file,
      cpr,
      sample: info.sample,
      position: info.position,
      folder: info.folder,
      versionKey: info.versionKey,
      versionLabel: info.versionLabel,
      versionNum: info.versionNum,
    })
  }

  matched.sort((a, b) =>
    sortText(a.sample, b.sample) ||
    a.versionNum - b.versionNum ||
    sortText(a.position, b.position),
  )
  folderPairs.value = matched
  if (!matched.length) {
    folderError.value = '找不到可配對的 .crc/.cpr'
  }
}

function resolveTargetFolder(parts: string[]): string {
  if (parts.length <= 1) return ''
  if (parts.length === 2) return parts[0]
  return parts[1] || parts[0]
}

function parseTargetVersion(folder: string) {
  const clean = folder.trim()
  const match = clean.match(/^(.*?)-(\d+)$/)
  if (!match) {
    return {
      sample: clean,
      versionKey: 'v1',
      versionLabel: `${clean}-1`,
      versionNum: 1,
    }
  }

  const sample = match[1].trim() || clean
  const versionNum = Number.parseInt(match[2], 10)
  const safeVersionNum = Number.isFinite(versionNum) && versionNum > 0 ? versionNum : 1
  return {
    sample,
    versionKey: `v${safeVersionNum}`,
    versionLabel: `${sample}-${safeVersionNum}`,
    versionNum: safeVersionNum,
  }
}

function folderTargetSummary(target: string): string {
  const pairs = folderPairs.value.filter((pair) => pair.sample === target)
  const versions = new Set(pairs.map((pair) => pair.versionKey))
  return `${versions.size} 版本 / ${pairs.length} 組`
}

function selectVersionView(sample: string, key: string) {
  selectedVersionView.value = {
    ...selectedVersionView.value,
    [sample]: key,
  }
}

async function importFolderPairs() {
  if (!folderPairs.value.length || importingFolder.value) return
  folderError.value = ''
  folderMessage.value = ''
  importingFolder.value = true
  folderImportedCount.value = 0

  try {
    for (const pair of folderPairs.value) {
      await savePairToLibrary({
        crc: pair.crc,
        cpr: pair.cpr,
        sample: pair.sample,
        position: pair.position,
        name: pair.name,
        folder: pair.folder,
        versionKey: pair.versionKey,
        versionLabel: pair.versionLabel,
        versionNum: pair.versionNum,
      })
      folderImportedCount.value++
    }
    folderMessage.value = `已匯入 ${folderImportedCount.value} 組掃描檔`
    resetFolderImport(false)
    await refreshAll()
  } catch (e: unknown) {
    const err = e as { data?: { detail?: string; message?: string }; message?: string }
    folderError.value = err.data?.detail || err.data?.message || err.message || '資料夾匯入失敗'
  } finally {
    importingFolder.value = false
  }
}

function resetFolderImport(clearMessage = true) {
  folderPairs.value = []
  folderTargets.value = new Set()
  folderImportedCount.value = 0
  if (folderInput.value) folderInput.value.value = ''
  if (clearMessage) {
    folderError.value = ''
    folderMessage.value = ''
  }
}

function handleSingleFile(kind: 'crc' | 'cpr', event: Event) {
  uploadError.value = ''
  uploadMessage.value = ''

  const input = event.target as HTMLInputElement
  const file = input.files?.[0] ?? null
  const expectedExt = `.${kind}`

  if (!file) {
    if (kind === 'crc') crcFile.value = null
    else cprFile.value = null
    return
  }

  if (!file.name.toLowerCase().endsWith(expectedExt)) {
    uploadError.value = `只能選擇 ${expectedExt} 檔案`
    input.value = ''
    if (kind === 'crc') crcFile.value = null
    else cprFile.value = null
    return
  }

  if (kind === 'crc') crcFile.value = file
  else cprFile.value = file

  fillMetadataFromFile(file)
  const filenameError = validateSelectedFileNames()
  if (filenameError) uploadError.value = filenameError
}

function fillMetadataFromFile(file: File) {
  const baseName = file.name.replace(/\.(crc|cpr)$/i, '').trim()
  if (!singleForm.position) singleForm.position = standardPos(baseName)
}

function validateSingleUpload(): string {
  if (!selectedTarget.value) return '請先選擇或新增靶材'
  if (!crcFile.value || !cprFile.value) return '請同時選擇 .crc 與 .cpr 檔案'
  if (!crcFile.value.name.toLowerCase().endsWith('.crc')) return 'CRC 欄位只能上傳 .crc'
  if (!cprFile.value.name.toLowerCase().endsWith('.cpr')) return 'CPR 欄位只能上傳 .cpr'
  const filenameError = validateSelectedFileNames()
  if (filenameError) return filenameError
  if (!singleForm.position.trim()) return '請輸入位置'
  if (!normalizedSinglePosition.value) return '位置格式不正確，請選擇或輸入 C-U、M-M、E-B 這類標準位置'
  return ''
}

function validateSelectedFileNames(): string {
  if (!crcFile.value || !cprFile.value) return ''
  const crcBase = localFileBaseName(crcFile.value.name)
  const cprBase = localFileBaseName(cprFile.value.name)
  if (crcBase !== cprBase) {
    return `CRC 與 CPR 檔名需一致，目前是 ${crcBase}.crc / ${cprBase}.cpr`
  }
  return ''
}

async function uploadSinglePair() {
  uploadError.value = validateSingleUpload()
  uploadMessage.value = ''
  if (uploadError.value || !crcFile.value || !cprFile.value) return

  uploading.value = true
  try {
    const normalizedPosition = normalizedSinglePosition.value
    if (!normalizedPosition) throw new Error('位置格式不正確')
    singleForm.position = normalizedPosition
    const saved = await savePairToLibrary({
      crc: crcFile.value,
      cpr: cprFile.value,
      sample: selectedTarget.value,
      position: normalizedPosition,
      name: `${selectedTarget.value}${selectedSingleVersion.value.label}-${normalizedPosition}`,
      versionKey: selectedSingleVersion.value.key,
      versionLabel: selectedSingleVersion.value.label,
      versionNum: selectedSingleVersion.value.num,
    })

    uploadMessage.value = `已保存 ${selectedTarget.value}${selectedSingleVersion.value.label} / ${normalizedPosition} (${shortHash(saved.pair_hash)})`
    resetUploadForm(false)
    listTargetFilter.value = selectedTarget.value
    await refreshAll()
  } catch (e: unknown) {
    const err = e as { data?: { detail?: string; message?: string }; message?: string }
    uploadError.value = err.data?.detail || err.data?.message || err.message || '保存失敗'
  } finally {
    uploading.value = false
  }
}

async function savePairToLibrary(payload: {
  crc: File
  cpr: File
  sample: string
  position: string
  name: string
  folder?: string
  versionKey?: string
  versionLabel?: string
  versionNum?: number
}) {
  const body = new FormData()
  body.append('crc', payload.crc)
  body.append('cpr', payload.cpr)
  body.append('sample', payload.sample)
  body.append('position', payload.position)
  body.append('name', payload.name)
  if (payload.folder) body.append('folder', payload.folder)
  if (payload.versionKey) body.append('version_key', payload.versionKey)
  if (payload.versionLabel) body.append('version_label', payload.versionLabel)
  if (payload.versionNum) body.append('version_num', String(payload.versionNum))

  return await $fetch<SaveEbsdPairResponse>('/api/ebsd/pairs', {
    method: 'POST',
    body,
    credentials: 'include',
  })
}

function resetUploadForm(clearMessage = true) {
  crcFile.value = null
  cprFile.value = null
  singleForm.position = ''
  singleForm.versionChoice = selectedTargetVersionOptions.value[0]?.key ?? 'v1'
  singleForm.newVersionNum = nextSingleVersionNum.value
  if (crcInput.value) crcInput.value.value = ''
  if (cprInput.value) cprInput.value.value = ''
  if (clearMessage) {
    uploadError.value = ''
    uploadMessage.value = ''
  }
}

function standardPos(pos: string): string {
  const trimmed = pos.trim()
  const dashMatch = trimmed.match(/^([CME])-([UMB])$/i)
  if (dashMatch) return `${dashMatch[1].toUpperCase()}-${dashMatch[2].toUpperCase()}`

  const colMap: Record<string, string> = { E: 'E', I: 'C', M: 'M', A: 'C', B: 'M', C: 'E' }
  const rowMap: Record<string, string> = { U: 'U', M: 'M', B: 'B', D: 'B' }
  if (trimmed.length >= 2) {
    const col = colMap[trimmed[0].toUpperCase()]
    const row = rowMap[trimmed[1].toUpperCase()]
    if (col && row) return `${col}-${row}`
  }
  return trimmed
}

function normalizeStandardPosition(pos: string): StandardPosition | '' {
  const normalized = standardPos(pos)
  return STANDARD_POSITIONS.includes(normalized as StandardPosition)
    ? normalized as StandardPosition
    : ''
}

function setSinglePosition(pos: StandardPosition) {
  singleForm.position = pos
  uploadError.value = ''
  uploadMessage.value = ''
}

function normalizeSinglePositionInput() {
  const normalized = normalizedSinglePosition.value
  if (normalized) singleForm.position = normalized
}

function localFileBaseName(filename: string): string {
  return filename.replace(/\.(crc|cpr)$/i, '').trim()
}

async function deletePair(pair: StoredEbsdPair) {
  if (deletingPairIds.value.has(pair.id)) return
  const ok = window.confirm(`移除 ${pair.version_label || `${pair.sample}-1`} / ${pair.position}？`)
  if (!ok) return

  deletingPairIds.value = new Set([...deletingPairIds.value, pair.id])
  listError.value = ''
  try {
    await $fetch(`/api/ebsd/pairs/${pair.id}`, {
      method: 'DELETE',
      credentials: 'include',
    })
    await refreshAll()
  } catch (e: unknown) {
    const err = e as { data?: { detail?: string; message?: string }; message?: string }
    listError.value = err.data?.detail || err.data?.message || err.message || '移除掃描檔失敗'
  } finally {
    const next = new Set(deletingPairIds.value)
    next.delete(pair.id)
    deletingPairIds.value = next
  }
}

async function deleteTarget(sample: string) {
  if (deletingTarget.value) return
  const ok = window.confirm(`刪除靶材 ${sample} 與目前帳號底下的所有掃描檔？`)
  if (!ok) return

  deletingTarget.value = sample
  listError.value = ''
  try {
    await $fetch(`/api/ebsd/targets/${encodeURIComponent(sample)}`, {
      method: 'DELETE',
      credentials: 'include',
    })
    if (selectedTarget.value === sample) selectedTarget.value = ''
    if (listTargetFilter.value === sample) listTargetFilter.value = ''
    const nextViews = { ...selectedVersionView.value }
    delete nextViews[sample]
    selectedVersionView.value = nextViews
    await refreshAll()
  } catch (e: unknown) {
    const err = e as { data?: { detail?: string; message?: string }; message?: string }
    listError.value = err.data?.detail || err.data?.message || err.message || '刪除靶材失敗'
  } finally {
    deletingTarget.value = ''
  }
}

function shortHash(value: string): string {
  return value ? `${value.slice(0, 8)}...${value.slice(-6)}` : ''
}

function sortText(a: string, b: string): number {
  return a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' })
}

function versionNum(pair: StoredEbsdPair): number {
  return pair.version_num ?? 1
}

function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB']
  let size = bytes
  let unitIndex = 0
  while (size >= 1024 && unitIndex < units.length - 1) {
    size /= 1024
    unitIndex++
  }
  return `${size.toFixed(unitIndex === 0 ? 0 : 1)} ${units[unitIndex]}`
}

function formatDate(value: string): string {
  if (!value) return ''
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) return value
  return new Intl.DateTimeFormat('zh-TW', {
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
  }).format(date)
}
</script>

<style scoped>
.library-page {
  max-width: 1180px;
  margin: 0 auto;
  padding: 2rem 1.5rem 3rem;
  color: #111827;
}

.page-head,
.panel-title-row,
.action-row,
.target-card-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 1rem;
}

.page-head {
  margin-bottom: 1.25rem;
}

.page-head h1,
.panel h2,
.target-group h3 {
  margin: 0;
  font-weight: 650;
  letter-spacing: 0;
}

.page-head h1 {
  font-size: 1.8rem;
}

.page-head p,
.panel-title-row span,
.target-card-head p {
  margin-top: 0.35rem;
  color: #6b7280;
}

.workspace-grid {
  display: grid;
  grid-template-columns: minmax(260px, 0.85fr) minmax(0, 1.45fr);
  gap: 1.25rem;
  align-items: start;
}

.panel {
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  background: #fff;
  padding: 1.25rem;
  margin-bottom: 1.25rem;
}

.create-target-row {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  gap: 0.6rem;
  margin-top: 1rem;
}

.target-list {
  display: grid;
  gap: 0.5rem;
  margin-top: 1rem;
}

.folder-drop {
  border: 1px dashed #cbd5e1;
  border-radius: 8px;
  cursor: pointer;
  display: grid;
  gap: .35rem;
  margin-top: 1rem;
  padding: 1rem;
  transition: border-color .2s, background .2s;
}

.folder-drop:hover,
.folder-drop.active {
  border-color: #2563eb;
  background: #eff6ff;
}

.folder-drop input {
  display: none;
}

.folder-drop span {
  color: #6b7280;
  line-height: 1.5;
}

.folder-summary {
  display: grid;
  gap: .5rem;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  margin-top: 1rem;
}

.folder-summary div {
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: .75rem;
  padding: .65rem .75rem;
}

.folder-summary strong {
  overflow-wrap: anywhere;
}

.folder-summary span {
  color: #6b7280;
  white-space: nowrap;
}

.target-item {
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  background: #fff;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 0.75rem;
  padding: 0.75rem;
  text-align: left;
}

.target-item.active {
  border-color: #2563eb;
  background: #eff6ff;
}

.target-item span {
  font-weight: 700;
  overflow-wrap: anywhere;
}

.target-item small {
  color: #6b7280;
  white-space: nowrap;
}

.pair-badge {
  border: 1px solid #d1d5db;
  border-radius: 999px;
  color: #6b7280;
  padding: 0.25rem 0.7rem;
  font-size: 0.85rem;
}

.pair-badge.ready {
  border-color: #0f766e;
  color: #0f766e;
  background: #f0fdfa;
}

.upload-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 0.9rem;
  margin-top: 1rem;
}

.version-select-grid {
  display: grid;
  grid-template-columns: minmax(0, 1fr) minmax(120px, .45fr);
  gap: .9rem;
}

.single-position-panel {
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  display: grid;
  gap: .75rem;
  margin-top: 1rem;
  padding: .9rem;
  background: #fff;
}

.position-picker-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: .75rem;
}

.position-picker-head span {
  color: #374151;
  font-size: .9rem;
  font-weight: 700;
}

.position-picker-head small {
  color: #64748b;
  font-weight: 600;
}

.single-position-map {
  justify-self: start;
  width: min(100%, 320px);
  border: 1px solid #d9e2ec;
  border-radius: 8px;
  padding: .75rem;
}

.single-axis-labels {
  display: grid;
  grid-template-columns: repeat(3, 54px);
  gap: 12px;
  margin-left: auto;
  margin-right: 12px;
  width: max-content;
  color: #64748b;
  font-size: .78rem;
  font-weight: 800;
  text-align: center;
}

.single-map-grid {
  display: grid;
  gap: .45rem;
  margin-left: auto;
  margin-right: 12px;
  margin-top: .45rem;
  width: max-content;
}

.single-map-row {
  display: grid;
  grid-template-columns: 20px repeat(3, 54px);
  gap: 12px;
  align-items: center;
}

.position-pick-btn {
  border: 1px solid #d1d5db;
  border-radius: 999px;
  background: #fff;
  color: #374151;
  cursor: pointer;
  font-size: .72rem;
  font-weight: 850;
  height: 30px;
  transition: border-color .15s ease, background .15s ease, box-shadow .15s ease, transform .15s ease;
}

.position-pick-btn:hover,
.position-pick-btn:focus-visible {
  transform: translateY(-1px);
  box-shadow: 0 8px 18px rgba(15, 23, 42, .10);
}

.position-pick-btn.active {
  border-color: #16a34a;
  background: #dcfce7;
  color: #166534;
  box-shadow: 0 0 0 2px rgba(22, 163, 74, .12);
}

.field {
  display: grid;
  gap: 0.35rem;
  margin-top: 1rem;
}

.field.compact {
  max-width: 260px;
}

.field span {
  color: #374151;
  font-size: 0.9rem;
  font-weight: 600;
}

.field input,
.field select,
.create-target-row input {
  width: 100%;
  border: 1px solid #d1d5db;
  border-radius: 6px;
  padding: 0.65rem 0.75rem;
  min-height: 40px;
  background: #fff;
}

.file-pair-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 0.9rem;
  margin-top: 1rem;
}

.file-drop {
  border: 1px dashed #cbd5e1;
  border-radius: 8px;
  padding: 1rem;
  cursor: pointer;
  display: grid;
  gap: 0.35rem;
  min-height: 112px;
  transition: border-color 0.2s, background 0.2s;
}

.file-drop:hover,
.file-drop.selected {
  border-color: #2563eb;
  background: #eff6ff;
}

.file-drop input {
  display: none;
}

.file-kind {
  color: #2563eb;
  font-size: 0.8rem;
  font-weight: 700;
}

.file-drop strong {
  overflow-wrap: anywhere;
}

.file-drop small {
  color: #6b7280;
  display: block;
  margin-top: 0.2rem;
}

.action-row {
  justify-content: flex-start;
  margin-top: 1rem;
  flex-wrap: wrap;
}

.primary-btn,
.ghost-btn {
  border: 0;
  border-radius: 6px;
  min-height: 38px;
  padding: 0 0.95rem;
  cursor: pointer;
  font-weight: 600;
}

.primary-btn {
  background: #111827;
  color: #fff;
}

.ghost-btn {
  background: #f3f4f6;
  color: #111827;
}

.danger-btn,
.inline-danger-btn {
  border: 1px solid #fecaca;
  border-radius: 6px;
  background: #fff;
  color: #b91c1c;
  cursor: pointer;
  font-weight: 700;
}

.danger-btn {
  min-height: 36px;
  padding: 0 .8rem;
}

.inline-danger-btn {
  min-height: 28px;
  padding: 0 .55rem;
}

.danger-btn:hover,
.inline-danger-btn:hover {
  background: #fef2f2;
}

.primary-btn:disabled,
.ghost-btn:disabled,
.danger-btn:disabled,
.inline-danger-btn:disabled {
  cursor: not-allowed;
  opacity: 0.55;
}

.filter-row {
  margin-top: 0.4rem;
}

.target-groups {
  display: grid;
  gap: 1rem;
  margin-top: 1rem;
}

.target-group {
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  padding: 1rem;
}

.target-card-head {
  margin-bottom: .9rem;
}

.target-card-actions {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: .75rem;
  flex-wrap: wrap;
}

.coverage-meter {
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  display: grid;
  gap: .1rem;
  min-width: 128px;
  padding: .55rem .7rem;
  text-align: center;
  background: #f9fafb;
}

.coverage-meter strong {
  font-size: 1.35rem;
}

.coverage-meter span {
  color: #6b7280;
  font-size: .82rem;
}

.coverage-meter small {
  color: #b45309;
  font-size: .75rem;
  font-weight: 700;
}

.coverage-meter small.complete {
  color: #047857;
}

.mini-map-wrap {
  display: grid;
  gap: .65rem;
  justify-items: start;
}

.mini-wafer-map {
  position: relative;
  isolation: isolate;
  width: min(100%, 360px);
  min-height: 156px;
  border: 1px solid #d9e2ec;
  border-radius: 8px;
  padding: .85rem 1rem .95rem .85rem;
  background: #fff;
}

.axis-labels {
  position: relative;
  z-index: 1;
  display: grid;
  grid-template-columns: repeat(3, 54px);
  gap: 14px;
  margin-left: auto;
  margin-right: 16px;
  width: max-content;
  color: #64748b;
  font-size: .78rem;
  font-weight: 800;
  text-align: center;
}

.mini-map-grid {
  position: relative;
  z-index: 1;
  display: grid;
  gap: .5rem;
  margin-left: auto;
  margin-right: 16px;
  margin-top: .45rem;
  width: max-content;
}

.mini-map-row {
  display: grid;
  grid-template-columns: 20px repeat(3, 54px);
  gap: 14px;
  align-items: center;
}

.row-label {
  color: #64748b;
  font-size: .78rem;
  font-weight: 800;
}

.map-dot {
  border: 1px solid;
  border-radius: 999px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 44px;
  height: 30px;
  font-size: .72rem;
  font-weight: 850;
  letter-spacing: 0;
  transition: transform .15s ease, box-shadow .15s ease;
}

.map-dot:hover {
  transform: translateY(-1px);
  box-shadow: 0 8px 18px rgba(15, 23, 42, .12);
}

.map-dot.present {
  background: #dcfce7;
  border-color: #16a34a;
  color: #166534;
}

.map-dot.missing {
  background: #fff7ed;
  border-color: #f97316;
  border-style: dashed;
  color: #9a3412;
}

.map-legend,
.extra-position-row {
  display: flex;
  flex-wrap: wrap;
  gap: .55rem;
  align-items: center;
}

.map-legend {
  color: #64748b;
  font-size: .82rem;
  font-weight: 700;
}

.map-legend span {
  display: inline-flex;
  align-items: center;
  gap: .28rem;
}

.legend-dot {
  border: 1px solid;
  border-radius: 50%;
  display: inline-block;
  width: 10px;
  height: 10px;
}

.legend-dot.present {
  background: #dcfce7;
  border-color: #16a34a;
}

.legend-dot.missing {
  background: #fff7ed;
  border-color: #f97316;
}

.version-strip {
  display: flex;
  flex-wrap: wrap;
  gap: .5rem;
  align-items: stretch;
}

.version-pill {
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  cursor: pointer;
  display: grid;
  gap: .15rem;
  padding: .45rem .6rem;
  min-width: 96px;
  background: #fff;
  color: #0f172a;
  text-align: left;
  transition: border-color .15s ease, background .15s ease, box-shadow .15s ease, transform .15s ease;
}

.version-pill.latest {
  border-color: #bfdbfe;
  background: #eff6ff;
}

.version-pill.active {
  border-color: #2563eb;
  box-shadow: 0 0 0 2px rgba(37, 99, 235, .12);
  background: #fff;
}

.version-pill.complete-state {
  border-color: #111827;
  background: #111827;
  color: #fff;
  min-width: 124px;
}

.version-pill.complete-state span {
  color: #d1d5db;
}

.version-pill.complete-state.active {
  border-color: #111827;
  background: #111827;
  box-shadow: 0 0 0 2px rgba(17, 24, 39, .16);
}

.version-pill.complete-state:hover,
.version-pill.complete-state:focus-visible {
  border-color: #111827;
}

.version-pill:hover,
.version-pill:focus-visible {
  border-color: #2563eb;
  transform: translateY(-1px);
}

.version-pill strong {
  font-size: .86rem;
}

.version-pill span {
  color: #64748b;
  font-size: .76rem;
  font-weight: 700;
}

.details-panel {
  border-top: 1px solid #e5e7eb;
  margin-top: 1rem;
  padding-top: .8rem;
}

.details-panel summary {
  color: #4b5563;
  cursor: pointer;
  font-weight: 700;
}

.scan-list {
  display: grid;
  gap: .65rem;
  margin-top: .75rem;
}

.scan-item {
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  padding: .75rem;
}

.scan-item > div:first-child {
  display: flex;
  align-items: center;
  gap: .55rem;
  margin-bottom: .6rem;
  flex-wrap: wrap;
}

.scan-item strong {
  overflow-wrap: anywhere;
}

.scan-item small {
  color: #64748b;
  font-weight: 700;
}

.scan-item dl {
  display: grid;
  gap: .4rem;
  margin: 0;
}

.scan-item dl div {
  display: grid;
  grid-template-columns: 42px minmax(0, 1fr);
  gap: .5rem;
}

.scan-item dt {
  color: #6b7280;
  font-weight: 700;
}

.scan-item dd {
  margin: 0;
  overflow-wrap: anywhere;
}

.pos-chip {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 44px;
  border-radius: 999px;
  font-weight: 700;
  padding: 0.2rem 0.45rem;
}

.pos-chip.present {
  background: #ecfdf5;
  color: #047857;
}

.pos-chip.missing {
  background: #f3f4f6;
  color: #9ca3af;
}

.pos-chip.extra {
  background: #eef2ff;
  color: #3730a3;
}

.empty-state {
  border: 1px dashed #d1d5db;
  border-radius: 8px;
  color: #6b7280;
  margin-top: 1rem;
  padding: 1.5rem;
  text-align: center;
}

.error-state {
  color: #b91c1c;
  border-color: #fecaca;
  background: #fef2f2;
}

@media (max-width: 860px) {
  .page-head,
  .panel-title-row,
  .target-card-head {
    align-items: flex-start;
    flex-direction: column;
  }

  .workspace-grid,
  .version-select-grid,
  .upload-grid,
  .file-pair-grid {
    grid-template-columns: 1fr;
  }
}
</style>
