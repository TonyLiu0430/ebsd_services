<template>
  <div class="page-shell">
    <header class="page-head">
      <div>
        <h1>EBSD 自動化報表</h1>
        <p>{{ libraryPairs.length }} 組掃描檔 · {{ sampleOptions.length }} 筆靶材</p>
      </div>
      <button class="ghost-btn" type="button" :disabled="libraryLoading" @click="refreshLibrary">
        {{ libraryLoading ? '讀取中' : '重新讀取' }}
      </button>
    </header>

    <section class="panel report-setup-panel">
      <div class="panel-title-row">
        <div>
          <h2>報表資料來源</h2>
          <p>從檔案庫選擇分析樣本與 Golden 樣本，報表會使用各靶材最新版本的完整位置狀態。</p>
        </div>
        <NuxtLink class="ghost-link" to="/">檔案庫</NuxtLink>
      </div>

      <div v-if="libraryError" class="empty-state error-state">{{ libraryError }}</div>
      <div v-else-if="libraryLoading" class="empty-state">正在讀取檔案庫</div>
      <div v-else-if="libraryPairs.length === 0" class="empty-state">
        檔案庫尚未有掃描檔，請先新增靶材與位置掃描檔。
      </div>
      <div v-else class="setup-grid">
        <div class="target-picker">
          <div class="section-subhead">
            <strong>選擇樣本</strong>
            <span>{{ comparablePositionCount }} 個可比對位置</span>
          </div>
          <div class="selector-grid">
            <label class="field">
              <span>分析樣本</span>
              <select v-model="selectedSample">
                <option value="">請選擇</option>
                <option v-for="s in sampleOptions" :key="s" :value="s">{{ s }}</option>
              </select>
            </label>
            <label class="field">
              <span>Golden 樣本</span>
              <select v-model="goldenSample">
                <option value="">請選擇</option>
                <option v-for="s in sampleOptions" :key="s" :value="s">{{ s }}</option>
              </select>
            </label>
          </div>

          <div class="selected-summary">
            <article class="selected-card">
              <span>分析</span>
              <strong>{{ selectedSample || '未選擇' }}</strong>
              <small>{{ selectedSampleSummary }}</small>
            </article>
            <article class="selected-card golden">
              <span>Golden</span>
              <strong>{{ goldenSample || '未選擇' }}</strong>
              <small>{{ goldenSampleSummary }}</small>
            </article>
          </div>

          <div class="action-row">
            <button class="primary-btn" @click="generateReport" :disabled="!canGenerate || loading">
              <span v-if="loading">處理中 {{ doneCount }}/{{ loadingTotal || selectedLibraryPairs.length }}</span>
              <span v-else>產生報表</span>
            </button>
            <el-text v-if="error" type="danger" class="err-msg">{{ error }}</el-text>
          </div>
        </div>

        <div class="library-overview">
          <div class="section-subhead">
            <strong>檔案庫概覽</strong>
            <span>{{ librarySampleCards.length }} 筆靶材</span>
          </div>
          <p class="interaction-hint">左鍵選分析樣本，右鍵指定 Golden。</p>
          <div class="library-card-list">
            <button
              v-for="card in librarySampleCards"
              :key="card.sample"
              type="button"
              class="library-target-card"
              :class="{ selected: card.sample === selectedSample, golden: card.sample === goldenSample }"
              title="左鍵選分析，右鍵指定 Golden"
              @click="selectAnalysisSample(card.sample)"
              @contextmenu.prevent="selectGoldenSample(card.sample)"
            >
              <div>
                <strong>
                  {{ card.sample }}
                  <em v-if="card.sample === selectedSample" class="role-pill analysis">分析</em>
                  <em v-if="card.sample === goldenSample" class="role-pill golden">Golden</em>
                </strong>
                <span>{{ card.positions.size }} 個位置 · {{ card.versions.length }} 個版本</span>
              </div>
              <small>{{ card.latestLabel }}</small>
            </button>
          </div>
        </div>
      </div>
    </section>

    <!-- ══════════════ REPORT ══════════════ -->
    <template v-if="reportData">
      <div
        class="report-version-dock pdf-exclude"
        aria-label="Report version switcher"
        data-html2canvas-ignore="true"
      >
        <div class="report-version-dock__title">Report 版本</div>
        <div class="report-version-dock__meta">
          <span>{{ selectedSample }}目前版本: {{ currentSelectedVersionLabel }}</span>
        </div>
        <el-slider
          v-model="reportVersionIndex"
          :min="0"
          :max="reportVersionSliderMax"
          :step="1"
          :show-stops="reportVersionStepCount > 1"
          :show-tooltip="false"
          :disabled="reportVersionStepCount <= 1"
        />
        <div class="dock-divider"></div>

        <div class="report-version-dock__title">Grain size 設定</div>
        <div class="grain-dock-summary">當前：{{ grainSettings.minGrainSize }} px</div>

        <label class="grain-dock-field">
          <span>Min-grain size (px)</span>
          <el-input-number
            v-model="grainMinGrainSizeDraft"
            :min="1"
            :max="1000000"
            :step="1"
            step-strictly
            controls-position="right"
            size="small"
            class="grain-dock-number"
          />
        </label>

        <button class="grain-dock-apply" type="button" :disabled="grainSettingsApplying" @click="applyGlobalMinGrainSize">
          {{ grainSettingsApplying ? '更新中…' : '套用 Min-grain' }}
        </button>
        <el-text v-if="grainSettingsError" type="danger" class="grain-settings-error">
          {{ grainSettingsError }}
        </el-text>
      </div>

      <div class="report-action-bar pdf-exclude" data-html2canvas-ignore="true">
        <button class="pdf-btn" @click="exportReportPdf" :disabled="exportingPdf">
          <span v-if="exportingPdf">輸出中… {{ pdfExportProgress }}</span>
          <span v-else>輸出 PDF</span>
        </button>
        <button class="pdf-btn" @click="exportReportPpt" :disabled="exportingPpt">
          <span v-if="exportingPpt">輸出 PPT 中…</span>
          <span v-else>輸出 PPT</span>
        </button>
        <el-text v-if="pdfError" type="danger" class="err-msg">{{ pdfError }}</el-text>
        <el-text v-if="pptError" type="danger" class="err-msg">{{ pptError }}</el-text>
      </div>

      <!-- ── Section 1: Grain distribution curves ──────────── -->
      <section class="card" data-pdf-page="grain-distribution">
        <div class="grain-card-head">
          <div>
            <h2 class="section-title">Grain 粒徑分布比對</h2>
            <div class="grain-settings-summary">{{ grainSettingsSummary }}</div>
          </div>
          <button class="grain-settings-btn" type="button" @click="openHistogramSettings">設定</button>
        </div>
        <div class="grain-mode-switch">
          <span class="grain-mode-label">圖形模式</span>
          <button
            class="grain-mode-btn"
            :class="{ active: grainChartMode === 'cdf' }"
            @click="grainChartMode = 'cdf'"
          >累積曲線</button>
          <button
            class="grain-mode-btn"
            :class="{ active: grainChartMode === 'hist' }"
            @click="grainChartMode = 'hist'"
          >分布直方圖</button>
          <button
            class="grain-mode-btn"
            :class="{ active: grainChartMode === 'areaHist' }"
            @click="grainChartMode = 'areaHist'"
          >面積加權直方圖</button>
        </div>

        <p v-if="grainChartMode === 'areaHist'" class="grain-mode-note">
          以 grain 等效直徑 d 換算面積權重，單顆 grain 權重採用 π(d/2)^2，圖上顯示落各粒徑區間的面積占比。
        </p>

        <div class="chart-legend-row">
          <svg v-if="grainChartMode === 'cdf'" width="30" height="14"><line x1="0" y1="7" x2="30" y2="7" stroke="#3B82F6" stroke-width="2.5"/></svg>
          <svg v-else width="16" height="14">
            <rect x="2" y="2" width="12" height="10" fill="#0f766e" opacity="0.28"/>
            <rect x="2" y="2" width="12" height="10" fill="none" stroke="#0f766e" stroke-width="1"/>
          </svg>
          <span class="legend-lbl">{{ currentSelectedVersionLabel }}</span>
          <svg v-if="grainChartMode === 'cdf'" width="30" height="14"><line x1="0" y1="7" x2="30" y2="7" stroke="#F59E0B" stroke-width="2.5" stroke-dasharray="5,2.5"/></svg>
          <svg v-else width="16" height="14">
            <rect x="2" y="2" width="12" height="10" fill="#F59E0B" opacity="0.28"/>
            <rect x="2" y="2" width="12" height="10" fill="none" stroke="#F59E0B" stroke-width="1" stroke-dasharray="2,1"/>
          </svg>
          <span class="legend-lbl">{{ currentGoldenVersionLabel }} (黃金)</span>
        </div>

        <div class="nine-cdf-grid">
          <template v-for="(rowLabel, rowKey) in ROW_LABELS" :key="rowKey">
            <template v-for="colKey in COL_KEYS" :key="colKey">
              <div
                class="cdf-cell"
                :class="positionStatusClass(`${colKey}-${rowKey}`)"
              >
                <div class="cdf-pos-label">{{ colKey }}-{{ rowKey }}</div>
                <div v-if="isMissingGoldenPos(`${colKey}-${rowKey}`)" class="cell-status-tag warning">缺 Golden</div>
                <div v-else-if="isMissingSelectedPos(`${colKey}-${rowKey}`)" class="cell-status-tag muted">未上傳</div>
                <GrainCdfChart
                  :sample="getCompareSampleGrains(`${colKey}-${rowKey}`)"
                  :golden="getCompareGoldenGrains(`${colKey}-${rowKey}`)"
                  :sampleLabel="currentSelectedVersionLabel"
                  :goldenLabel="currentGoldenVersionLabel"
                  :mode="grainChartMode"
                  :fixedXMin="sharedGrainBins.min"
                  :fixedXMax="sharedGrainBins.max"
                  :binCount="sharedGrainBins.binCount"
                  :binWidth="sharedGrainBins.binWidth"
                />
              </div>
            </template>
          </template>
        </div>
      </section>

      <el-dialog
        v-model="histogramSettingsOpen"
        title="直方圖設定"
        width="520px"
        class="grain-settings-dialog"
        append-to-body
      >
        <div class="grain-settings-panel">
          <div class="grain-setting-block">
            <div class="grain-setting-title">直方圖分箱起點</div>
            <el-radio-group v-model="histBinStartModeDraft" class="grain-radio-row">
              <el-radio-button label="zero" value="zero">從 0 開始</el-radio-button>
              <el-radio-button label="min" value="min">從最小值開始</el-radio-button>
            </el-radio-group>
          </div>

          <div class="grain-setting-block">
            <div class="grain-setting-title">直方圖間距大小</div>
            <el-radio-group v-model="histBinWidthModeDraft" class="grain-radio-row">
              <el-radio-button label="ratio" value="ratio">比例</el-radio-button>
              <el-radio-button label="absolute" value="absolute">絕對數值</el-radio-button>
            </el-radio-group>
            <div class="grain-setting-input-row">
              <el-input-number
                v-model="histBinWidthValueDraft"
                :min="histBinWidthModeDraft === 'ratio' ? 0.5 : 0.1"
                :max="histBinWidthModeDraft === 'ratio' ? 100 : 200"
                :step="histBinWidthModeDraft === 'ratio' ? 0.5 : 0.5"
                controls-position="right"
                class="grain-number-input"
              />
              <span class="grain-input-unit">{{ histBinWidthModeDraft === 'ratio' ? '% of range' : 'μm' }}</span>
            </div>
            <div class="grain-setting-desc">{{ grainBinPreviewText }}</div>
          </div>
        </div>

        <template #footer>
          <div class="grain-settings-footer">
            <button class="grain-dialog-cancel" type="button" @click="histogramSettingsOpen = false">取消</button>
            <button class="grain-dialog-apply" type="button" @click="applyHistogramSettings">套用設定</button>
          </div>
        </template>
      </el-dialog>

      <!-- ── Section 1b: IPF Map grid ────────── -->
      <section v-if="pairs.length" class="card" data-pdf-page="ipf-map">
        <h2 class="section-title">IPF 晶粒取向分佈圖 — {{ currentSelectedVersionLabel || selectedSample }}</h2>
        <IpfMapGrid :pairs="ipfMapPairs" :sample="selectedSample" />
      </section>

      <!-- ── Section 2: Nine-grid full data ────────── -->
      <section class="card" data-pdf-page="nine-grid-data">
        <h2 class="section-title">九宮格完整數據 — {{ currentSelectedVersionLabel || selectedSample }}</h2>

        <div class="nine-grid-wrapper">
          <div class="ng-header-row">
            <div class="ng-corner"></div>
            <div v-for="(colLabel, colKey) in COL_LABELS" :key="colKey" class="ng-col-header">
              {{ colLabel }}<br/><small>({{ colKey }})</small>
            </div>
          </div>
          <div v-for="(rowLabel, rowKey) in ROW_LABELS" :key="rowKey" class="ng-data-row">
            <div class="ng-row-header">{{ rowLabel }}</div>
            <div
              v-for="(_, colKey) in COL_LABELS"
              :key="colKey"
              class="ng-cell"
              :class="{
                'ng-cell-retest': isGridPosRetestedNext(`${colKey}-${rowKey}`),
                ...positionStatusClass(`${colKey}-${rowKey}`),
              }"
            >
              <div class="ng-cell-inner">
                <div v-if="isMissingGoldenPos(`${colKey}-${rowKey}`)" class="cell-status-tag warning">Sample 有資料，Golden 缺少</div>
                <div v-else-if="isMissingSelectedPos(`${colKey}-${rowKey}`)" class="cell-status-tag muted">Sample 未上傳</div>
                <div class="stat-block">
                  <div class="stat-title">Grain Size (μm)</div>
                  <div class="stat-row">
                    <span class="stat-key">Mean</span>
                    <span class="stat-val">{{ fmtGrain(`${colKey}-${rowKey}`, 'mean') }}</span>
                  </div>
                  <div class="stat-row">
                    <span class="stat-key">Max</span>
                    <span class="stat-val">{{ fmtGrain(`${colKey}-${rowKey}`, 'max') }}</span>
                  </div>
                  <div class="stat-row">
                    <span class="stat-key">P75</span>
                    <span class="stat-val">{{ fmtGrain(`${colKey}-${rowKey}`, 'p75') }}</span>
                  </div>
                  <div class="stat-row">
                    <span class="stat-key">Count</span>
                    <span class="stat-val">{{ fmtGrain(`${colKey}-${rowKey}`, 'count') }}</span>
                  </div>
                </div>
                <div class="stat-block orient-block">
                  <div class="stat-title">Orientation Ratio</div>
                  <table class="orient-table">
                    <thead>
                      <tr><th></th><th>[001]</th><th>[110]</th><th>[111]</th></tr>
                    </thead>
                    <tbody>
                      <tr>
                        <td class="dev-label">20°</td>
                        <td>{{ fmtOrient(`${colKey}-${rowKey}`, '20%', 0) }}%</td>
                        <td>{{ fmtOrient(`${colKey}-${rowKey}`, '20%', 1) }}%</td>
                        <td>{{ fmtOrient(`${colKey}-${rowKey}`, '20%', 2) }}%</td>
                      </tr>
                      <tr>
                        <td class="dev-label">15°</td>
                        <td>{{ fmtOrient(`${colKey}-${rowKey}`, '15%', 0) }}%</td>
                        <td>{{ fmtOrient(`${colKey}-${rowKey}`, '15%', 1) }}%</td>
                        <td>{{ fmtOrient(`${colKey}-${rowKey}`, '15%', 2) }}%</td>
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
      <section class="card" data-pdf-page="orientation-triangle-20">
        <h2 class="section-title">晶粒取向比例 — Misorientation 20°</h2>
        <div class="orient-controls">
          <div class="orient-legend-row">
            <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#6b7280" stroke-width="1.5"/></svg>
            <span class="legend-lbl">{{ selectedSample }} · {{ currentSelectedVersionLabel }}（實線）</span>
            <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#6b7280" stroke-width="1.5" stroke-dasharray="4,2"/></svg>
            <span class="legend-lbl">{{ goldenSample }} · {{ currentGoldenVersionLabel }}（虛線）</span>
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
              <button class="simple-toggle-btn" :class="{ active: showTriangleLabels }" @click="toggleTriangleLabels()">{{ showTriangleLabels ? '隱藏' : '顯示' }}</button>
            </div>
          </div>
        </div>
        <div class="triangle-row">
          <div v-for="(colLabel, colKey) in COL_LABELS" :key="colKey" class="triangle-panel">
            <div class="tri-col-title">{{ colLabel }} ({{ colKey }})</div>
            <TriangleChart :series="buildOrientSeries(colKey, '20%')" :showValueLabels="showTriangleLabels" />
          </div>
        </div>
      </section>

      <!-- ── Section 3b: Orientation triangles 15° ── -->
      <section class="card" data-pdf-page="orientation-triangle-15">
        <h2 class="section-title">晶粒取向比例 — Misorientation 15°</h2>
        <div class="orient-controls">
          <div class="orient-legend-row">
            <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#6b7280" stroke-width="1.5"/></svg>
            <span class="legend-lbl">{{ selectedSample }} · {{ currentSelectedVersionLabel }}（實線）</span>
            <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#6b7280" stroke-width="1.5" stroke-dasharray="4,2"/></svg>
            <span class="legend-lbl">{{ goldenSample }} · {{ currentGoldenVersionLabel }}（虛線）</span>
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
              <button class="simple-toggle-btn" :class="{ active: showTriangleLabels }" @click="toggleTriangleLabels()">{{ showTriangleLabels ? '隱藏' : '顯示' }}</button>
            </div>
          </div>
        </div>
        <div class="triangle-row">
          <div v-for="(colLabel, colKey) in COL_LABELS" :key="colKey" class="triangle-panel">
            <div class="tri-col-title">{{ colLabel }} ({{ colKey }})</div>
            <TriangleChart :series="buildOrientSeries(colKey, '15%')" :showValueLabels="showTriangleLabels" />
          </div>
        </div>
      </section>

      <!-- Section 3c: Orientation line charts 20度 -->
      <section class="card" data-pdf-page="orientation-line-20">
        <h2 class="section-title">晶粒取向折線圖 · Misorientation 20°</h2>
        <div class="orient-controls orient-line-controls">
          <div class="orient-legend-row">
            <template v-for="info in COL_LINE_SERIES_INFO" :key="info.colKey">
              <svg width="28" height="12">
                <line
                  x1="1"
                  y1="6"
                  x2="27"
                  y2="6"
                  :stroke="info.color"
                  stroke-width="2.75"
                  stroke-linecap="round"
                />
              </svg>
              <span class="legend-lbl">{{ info.label }}</span>
            </template>
            <span class="orient-line-version">{{ currentSelectedVersionLabel || selectedSample }}</span>
          </div>
          <div class="orient-toggle-bar">
            <div class="row-toggle-group">
              <span class="toggle-label">線條顯示:</span>
              <button
                v-for="info in COL_LINE_SERIES_INFO"
                :key="info.colKey"
                class="row-toggle-btn"
                :class="{ active: visibleLineCols.has(info.colKey) }"
                :style="visibleLineCols.has(info.colKey) ? { background: info.color, borderColor: info.color } : {}"
                @click="toggleLineCol(info.colKey)"
              >{{ info.label }}</button>
            </div>
            <div class="label-toggle-group">
              <span class="toggle-label">數字顯示:</span>
              <button class="simple-toggle-btn" :class="{ active: showOrientLineLabels }" @click="toggleOrientLineLabels()">{{ showOrientLineLabels ? '隱藏' : '顯示' }}</button>
            </div>
          </div>
        </div>
        <div class="orient-line-grid">
          <div v-for="rowInfo in orientLineCharts20" :key="rowInfo.rowKey" class="orient-line-panel">
            <div class="tri-col-title">{{ rowInfo.title }}</div>
            <svg
              class="orient-line-svg"
              :viewBox="rowInfo.chart.viewBox"
              role="img"
              :aria-label="`Misorientation 20度 ${rowInfo.title} 晶粒取向折線圖`"
            >
              <line
                v-for="tick in rowInfo.chart.yTicks"
                :key="`grid-20-${rowInfo.rowKey}-${tick.label}`"
                :x1="rowInfo.chart.plot.left"
                :y1="tick.y"
                :x2="rowInfo.chart.plot.right"
                :y2="tick.y"
                class="orient-line-gridline"
              />
              <line
                :x1="rowInfo.chart.plot.left"
                :y1="rowInfo.chart.plot.top"
                :x2="rowInfo.chart.plot.left"
                :y2="rowInfo.chart.plot.bottom"
                class="orient-line-axis"
              />
              <line
                :x1="rowInfo.chart.plot.left"
                :y1="rowInfo.chart.plot.bottom"
                :x2="rowInfo.chart.plot.right"
                :y2="rowInfo.chart.plot.bottom"
                class="orient-line-axis"
              />
              <text
                v-for="tick in rowInfo.chart.yTicks"
                :key="`ylabel-20-${rowInfo.rowKey}-${tick.label}`"
                :x="rowInfo.chart.plot.left - 8"
                :y="tick.y + 1"
                class="orient-line-ytext"
              >{{ tick.label }}</text>
              <text
                v-for="tick in rowInfo.chart.xTicks"
                :key="`xlabel-20-${rowInfo.rowKey}-${tick.label}`"
                :x="tick.x"
                :y="rowInfo.chart.plot.bottom + 20"
                class="orient-line-xtext"
              >{{ tick.label }}</text>
              <g v-for="series in rowInfo.chart.series" :key="`series-20-${rowInfo.rowKey}-${series.colKey}`">
                <polyline :points="series.polyline" :stroke="series.color" class="orient-line-path" />
                <circle
                  v-for="point in series.points"
                  :key="`point-20-${rowInfo.rowKey}-${series.colKey}-${point.x}`"
                  :cx="point.x"
                  :cy="point.y"
                  r="3.25"
                  :fill="series.color"
                  class="orient-line-point"
                />
                <text
                  v-if="showOrientLineLabels"
                  v-for="point in series.points"
                  :key="`label-20-${rowInfo.rowKey}-${series.colKey}-${point.x}`"
                  :x="point.x"
                  :y="point.labelY"
                  :fill="series.color"
                  class="orient-line-value"
                >{{ point.label }}</text>
              </g>
            </svg>
          </div>
        </div>
      </section>

      <!-- Section 3d: Orientation line charts 15度 -->
      <section class="card" data-pdf-page="orientation-line-15">
        <h2 class="section-title">晶粒取向折線圖 · Misorientation 15°</h2>
        <div class="orient-controls orient-line-controls">
          <div class="orient-legend-row">
            <template v-for="info in COL_LINE_SERIES_INFO" :key="info.colKey">
              <svg width="28" height="12">
                <line
                  x1="1"
                  y1="6"
                  x2="27"
                  y2="6"
                  :stroke="info.color"
                  stroke-width="2.75"
                  stroke-linecap="round"
                />
              </svg>
              <span class="legend-lbl">{{ info.label }}</span>
            </template>
            <span class="orient-line-version">{{ currentSelectedVersionLabel || selectedSample }}</span>
          </div>
          <div class="orient-toggle-bar">
            <div class="row-toggle-group">
              <span class="toggle-label">線條顯示:</span>
              <button
                v-for="info in COL_LINE_SERIES_INFO"
                :key="info.colKey"
                class="row-toggle-btn"
                :class="{ active: visibleLineCols.has(info.colKey) }"
                :style="visibleLineCols.has(info.colKey) ? { background: info.color, borderColor: info.color } : {}"
                @click="toggleLineCol(info.colKey)"
              >{{ info.label }}</button>
            </div>
            <div class="label-toggle-group">
              <span class="toggle-label">數字顯示:</span>
              <button class="simple-toggle-btn" :class="{ active: showOrientLineLabels }" @click="toggleOrientLineLabels()">{{ showOrientLineLabels ? '隱藏' : '顯示' }}</button>
            </div>
          </div>
        </div>
        <div class="orient-line-grid">
          <div v-for="rowInfo in orientLineCharts15" :key="rowInfo.rowKey" class="orient-line-panel">
            <div class="tri-col-title">{{ rowInfo.title }}</div>
            <svg
              class="orient-line-svg"
              :viewBox="rowInfo.chart.viewBox"
              role="img"
              :aria-label="`Misorientation 15度 ${rowInfo.title} 晶粒取向折線圖`"
            >
              <line
                v-for="tick in rowInfo.chart.yTicks"
                :key="`grid-15-${rowInfo.rowKey}-${tick.label}`"
                :x1="rowInfo.chart.plot.left"
                :y1="tick.y"
                :x2="rowInfo.chart.plot.right"
                :y2="tick.y"
                class="orient-line-gridline"
              />
              <line
                :x1="rowInfo.chart.plot.left"
                :y1="rowInfo.chart.plot.top"
                :x2="rowInfo.chart.plot.left"
                :y2="rowInfo.chart.plot.bottom"
                class="orient-line-axis"
              />
              <line
                :x1="rowInfo.chart.plot.left"
                :y1="rowInfo.chart.plot.bottom"
                :x2="rowInfo.chart.plot.right"
                :y2="rowInfo.chart.plot.bottom"
                class="orient-line-axis"
              />
              <text
                v-for="tick in rowInfo.chart.yTicks"
                :key="`ylabel-15-${rowInfo.rowKey}-${tick.label}`"
                :x="rowInfo.chart.plot.left - 8"
                :y="tick.y + 1"
                class="orient-line-ytext"
              >{{ tick.label }}</text>
              <text
                v-for="tick in rowInfo.chart.xTicks"
                :key="`xlabel-15-${rowInfo.rowKey}-${tick.label}`"
                :x="tick.x"
                :y="rowInfo.chart.plot.bottom + 20"
                class="orient-line-xtext"
              >{{ tick.label }}</text>
              <g v-for="series in rowInfo.chart.series" :key="`series-15-${rowInfo.rowKey}-${series.colKey}`">
                <polyline :points="series.polyline" :stroke="series.color" class="orient-line-path" />
                <circle
                  v-for="point in series.points"
                  :key="`point-15-${rowInfo.rowKey}-${series.colKey}-${point.x}`"
                  :cx="point.x"
                  :cy="point.y"
                  r="3.25"
                  :fill="series.color"
                  class="orient-line-point"
                />
                <text
                  v-if="showOrientLineLabels"
                  v-for="point in series.points"
                  :key="`label-15-${rowInfo.rowKey}-${series.colKey}-${point.x}`"
                  :x="point.x"
                  :y="point.labelY"
                  :fill="series.color"
                  class="orient-line-value"
                >{{ point.label }}</text>
              </g>
            </svg>
          </div>
        </div>
      </section>

      <!-- ── Section 4: Python Analysis ──────── -->
      <section class="card" :data-pdf-page="analysisRes ? 'feature-analysis' : undefined">
        <h2 class="section-title">④ 特徵分析 </h2>
        <div class="feat-label">選擇分析項目</div>
        <el-checkbox-group v-model="selectedFeatures" class="feat-checkbox-group">
          <el-checkbox v-for="feat in featureOptionsList" :key="feat" :label="feat" :value="feat" />
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
definePageMeta({
  middleware: 'auth'
})

interface FilePair {
  id: string
  name: string
  sample: string
  pos: string
  versionKey: string
  versionLabel: string
  versionNum: number
}
type CppFeatures = {
  grains: number[]
  'orientation_ratio(20%)': number[]
  'orientation_ratio(15%)': number[]
}
type AllDataResult = Record<string, Record<string, CppFeatures>>
type VersionedDataResult = Record<string, Record<string, Record<string, CppFeatures>>>
type VersionOption = { key: string; label: string; num: number }
type OrientDev = '20%' | '15%'
type HistBinStartMode = 'zero' | 'min'
type HistBinWidthMode = 'ratio' | 'absolute'
type GrainSettings = {
  minGrainSize: number
  histBinStartMode: HistBinStartMode
  histBinWidthMode: HistBinWidthMode
  histBinWidthRatio: number
  histBinWidthAbsolute: number
}
type ReportDataResponse = {
  reportData: AllDataResult
  versionedReportData: VersionedDataResult
  rawVersionPositions: Record<string, Record<string, string[]>>
}

type AnalysisResult = Record<string, Record<string, number>>
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

const COL_KEYS = ['C', 'M', 'E'] as const
const COL_LABELS: Record<string, string> = { C: '內 Center', M: '中 Middle', E: '外 Edge' }
const ROW_LABELS: Record<string, string> = { U: '上 Up', M: '中 Middle', B: '下 Bottom' }
const ROW_SERIES_INFO = [
  { rowKey: 'U', color: '#EF4444', label: '上 (U)' },
  { rowKey: 'M', color: '#3B82F6', label: '中 (M)' },
  { rowKey: 'B', color: '#10B981', label: '下 (B)' },
]
const COL_LINE_SERIES_INFO = [
  { colKey: 'C', color: '#2563EB', label: '內(C)' },
  { colKey: 'M', color: '#10B981', label: '中(M)' },
  { colKey: 'E', color: '#F59E0B', label: '外(E)' },
] as const
const ORIENT_LINE_ROWS = [
  { rowKey: 'U', title: ROW_LABELS.U },
  { rowKey: 'M', title: ROW_LABELS.M },
  { rowKey: 'B', title: ROW_LABELS.B },
] as const
const ORIENT_LINE_X_LABELS = ['001', '110', '111'] as const
const ORIENT_LINE_Y_TICKS = [0, 25, 50, 75, 100] as const
const ORIENT_LINE_LAYOUT = {
  width: 320,
  height: 220,
  left: 42,
  right: 16,
  top: 18,
  bottom: 36,
  maxValue: 100,
} as const
const HIST_BIN_COUNT = 20
const DEFAULT_GRAIN_MIN_SIZE = 10
const DEFAULT_HIST_BIN_RATIO = Number((100 / HIST_BIN_COUNT).toFixed(2))
const DEFAULT_HIST_BIN_ABSOLUTE = 10

const visibleRows = ref(new Set<string>(['U', 'M', 'B']))
const showTriangleLabels = ref(true)
function toggleTriangleLabels() {
  showTriangleLabels.value = !showTriangleLabels.value
}
function toggleRow(rowKey: string) {
  if (visibleRows.value.has(rowKey)) {
    if (visibleRows.value.size > 1) visibleRows.value.delete(rowKey)
  } else {
    visibleRows.value.add(rowKey)
  }
  visibleRows.value = new Set(visibleRows.value)
}
const visibleLineCols = ref(new Set<string>(['C', 'M', 'E']))
const showOrientLineLabels = ref(true)
function toggleOrientLineLabels() {
  showOrientLineLabels.value = !showOrientLineLabels.value
}
function toggleLineCol(colKey: string) {
  if (visibleLineCols.value.has(colKey)) {
    if (visibleLineCols.value.size > 1) visibleLineCols.value.delete(colKey)
  } else {
    visibleLineCols.value.add(colKey)
  }
  visibleLineCols.value = new Set(visibleLineCols.value)
}

const pairs = ref<FilePair[]>([])
const samples = ref(new Set<string>())
const sampleOptions = computed(() =>
  Array.from(samples.value).sort((a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' })),
)
const libraryPairs = ref<StoredEbsdPair[]>([])
const libraryLoading = ref(false)
const libraryError = ref('')
const selectedSample = ref('')
const goldenSample = ref('')
const loading = ref(false)
const doneCount = ref(0)
const loadingTotal = ref(0)
const error = ref('')
const pdfError = ref('')
const pptError = ref('')
const exportingPdf = ref(false)
const exportingPpt = ref(false)
const pdfExportProgress = ref('')
const reportData = ref<AllDataResult | null>(null)
const versionedReportData = ref<VersionedDataResult | null>(null)
const versionOptionsMap = ref<Record<string, VersionOption[]>>({})
const rawVersionPositions = ref<Record<string, Record<string, string[]>>>({})

const DEFAULT_FEATURE_OPTIONS = [
  'overall distribution : wasserstein_distance / mean',
  'mean',
  'max',
  '25 percentiles',
  '75 percentiles',
  'standard deviation',
  '[001] (deviation 20%)',
  '[110] (deviation 20%)',
  '[111] (deviation 20%)',
  '[001] (deviation 15%)',
  '[110] (deviation 15%)',
  '[111] (deviation 15%)',
] as const

const { data: featuresOptions } = await useFetch<string[]>('/api/ebsd_features')
const selectedFeatures = ref<string[]>([])
const colorThresholds = ref([30, 70])
const analysisRes = ref<AnalysisResult | null>(null)
const grainChartMode = ref<'cdf' | 'hist' | 'areaHist'>('cdf')
const histogramSettingsOpen = ref(false)
const grainSettingsApplying = ref(false)
const grainSettingsError = ref('')
const grainSettings = ref<GrainSettings>({
  minGrainSize: DEFAULT_GRAIN_MIN_SIZE,
  histBinStartMode: 'min',
  histBinWidthMode: 'ratio',
  histBinWidthRatio: DEFAULT_HIST_BIN_RATIO,
  histBinWidthAbsolute: DEFAULT_HIST_BIN_ABSOLUTE,
})
const grainMinGrainSizeDraft = ref(DEFAULT_GRAIN_MIN_SIZE)
const histBinStartModeDraft = ref<HistBinStartMode>('min')
const histBinWidthModeDraft = ref<HistBinWidthMode>('ratio')
const histBinWidthValueDraft = ref(DEFAULT_HIST_BIN_RATIO)

watch(histBinWidthModeDraft, (mode) => {
  if (!histogramSettingsOpen.value) return
  histBinWidthValueDraft.value = mode === 'ratio'
    ? grainSettings.value.histBinWidthRatio
    : grainSettings.value.histBinWidthAbsolute
})

const reportVersionIndex = ref(0)
const PDF_CAPTURE_WIDTH = 1120
const PDF_CAPTURE_SCALE = 1.35
const PDF_MARGIN_MM = 8
const featureOptionsList = computed(() =>
  featuresOptions.value?.length ? featuresOptions.value : [...DEFAULT_FEATURE_OPTIONS],
)

const canGenerate = computed(
  () =>
    selectedSample.value !== '' &&
    goldenSample.value !== '' &&
    selectedSample.value !== goldenSample.value,
)
const selectedLibraryPairs = computed(() => {
  const needed = new Set([selectedSample.value, goldenSample.value])
  return libraryPairs.value.filter((pair) => needed.has(pair.sample))
})
const librarySampleCards = computed(() => {
  const map = new Map<string, StoredEbsdPair[]>()
  for (const pair of libraryPairs.value) {
    const items = map.get(pair.sample) ?? []
    items.push(pair)
    map.set(pair.sample, items)
  }

  return Array.from(map.entries())
    .map(([sample, items]) => {
      const positions = new Set(items.map((item) => item.position))
      const versionMap = new Map<string, VersionOption>()
      for (const item of items) {
        const num = item.version_num ?? 1
        const key = item.version_key || `v${num}`
        versionMap.set(key, {
          key,
          label: item.version_label || `${sample}-${num}`,
          num,
        })
      }
      const versions = Array.from(versionMap.values()).sort((a, b) =>
        a.num - b.num || a.label.localeCompare(b.label, undefined, { numeric: true, sensitivity: 'base' }),
      )
      return {
        sample,
        positions,
        versions,
        latestLabel: versions[versions.length - 1]?.label ?? `${sample}-1`,
        updatedAt: items
          .map((item) => item.updated_at || item.created_at)
          .sort((a, b) => new Date(b).getTime() - new Date(a).getTime())[0] ?? '',
      }
    })
    .sort((a, b) => a.sample.localeCompare(b.sample, undefined, { numeric: true, sensitivity: 'base' }))
})
const selectedSampleCard = computed(() =>
  librarySampleCards.value.find((card) => card.sample === selectedSample.value),
)
const goldenSampleCard = computed(() =>
  librarySampleCards.value.find((card) => card.sample === goldenSample.value),
)
const selectedSampleSummary = computed(() => formatSampleSummary(selectedSampleCard.value))
const goldenSampleSummary = computed(() => formatSampleSummary(goldenSampleCard.value))
const comparablePositionCount = computed(() => {
  if (!selectedSampleCard.value || !goldenSampleCard.value) return 0
  const goldenPositions = goldenSampleCard.value.positions
  return Array.from(selectedSampleCard.value.positions).filter((pos) => goldenPositions.has(pos)).length
})

watch(selectedSample, (sample) => {
  if (sample && sample === goldenSample.value) goldenSample.value = ''
})

watch(goldenSample, (sample) => {
  if (sample && sample === selectedSample.value) selectedSample.value = ''
})

function formatSampleSummary(card: typeof librarySampleCards.value[number] | undefined): string {
  if (!card) return '尚未選擇'
  return `${card.positions.size} 個位置 / ${card.versions.length} 個版本 / 最新 ${card.latestLabel}`
}

function selectAnalysisSample(sample: string) {
  selectedSample.value = sample
  if (goldenSample.value === sample) goldenSample.value = ''
}

function selectGoldenSample(sample: string) {
  goldenSample.value = sample
  if (selectedSample.value === sample) selectedSample.value = ''
}

onMounted(async () => {
  await refreshLibrary()
})

async function refreshLibrary() {
  libraryLoading.value = true
  libraryError.value = ''
  reportData.value = null
  versionedReportData.value = null
  analysisRes.value = null
  error.value = ''
  pdfError.value = ''
  pptError.value = ''
  grainSettingsError.value = ''
  pairs.value = []

  try {
    const rows = await $fetch<StoredEbsdPair[]>('/api/ebsd/pairs/metadata', {
      credentials: 'include',
    })
    libraryPairs.value = rows
    samples.value = new Set(rows.map((row) => row.sample))
    versionOptionsMap.value = buildVersionOptions(rows)

    const opts = sampleOptions.value
    if (!opts.includes(selectedSample.value)) selectedSample.value = ''
    if (!opts.includes(goldenSample.value) || goldenSample.value === selectedSample.value) goldenSample.value = ''
  } catch (e: unknown) {
    const err = e as { data?: { detail?: string; message?: string }; message?: string }
    libraryError.value = err.data?.detail || err.data?.message || err.message || '讀取檔案庫失敗'
  } finally {
    libraryLoading.value = false
  }
}

function buildVersionOptions(rows: StoredEbsdPair[]): Record<string, VersionOption[]> {
  const map: Record<string, Map<string, VersionOption>> = {}
  for (const row of rows) {
    const num = row.version_num ?? 1
    const key = row.version_key || `v${num}`
    if (!map[row.sample]) map[row.sample] = new Map()
    map[row.sample].set(key, {
      key,
      label: row.version_label || `${row.sample}-${num}`,
      num,
    })
  }

  return Object.fromEntries(
    Object.entries(map).map(([sample, options]) => [
      sample,
      Array.from(options.values()).sort((a, b) =>
        a.num - b.num || a.label.localeCompare(b.label, undefined, { numeric: true, sensitivity: 'base' }),
      ),
    ]),
  )
}

function getVersionOptions(sample: string): VersionOption[] {
  return versionOptionsMap.value[sample] ?? []
}

const selectedSampleVersionOptions = computed(() => getVersionOptions(selectedSample.value))
const goldenSampleVersionOptions = computed(() => getVersionOptions(goldenSample.value))

function resolveVersionOption(options: VersionOption[], index: number): VersionOption | undefined {
  if (!options.length) return undefined
  return options[Math.min(index, options.length - 1)]
}

function getLatestVersionOption(options: VersionOption[]): VersionOption | undefined {
  return options[options.length - 1]
}

const reportVersionSliderMax = computed(() =>
  Math.max(0, selectedSampleVersionOptions.value.length - 1),
)
const reportVersionStepCount = computed(() =>
  selectedSampleVersionOptions.value.length,
)

watch(selectedSampleVersionOptions, (selectedOpts) => {
  reportVersionIndex.value = Math.max(0, selectedOpts.length - 1)
}, { immediate: true })

const currentSelectedVersionOption = computed(() =>
  resolveVersionOption(selectedSampleVersionOptions.value, reportVersionIndex.value),
)
const currentGoldenVersionOption = computed(() =>
  getLatestVersionOption(goldenSampleVersionOptions.value),
)

const currentSelectedVersionLabel = computed(() => currentSelectedVersionOption.value?.label ?? selectedSample.value)
const currentGoldenVersionLabel = computed(() => currentGoldenVersionOption.value?.label ?? goldenSample.value)

const ipfMapPairs = computed(() => {
  const versionOpt = currentSelectedVersionOption.value
  const sample = selectedSample.value
  if (!versionOpt || !sample) return pairs.value.filter(p => p.sample === sample)

  const currentNum = versionOpt.num
  const posMap = new Map<string, FilePair>()

  for (const p of pairs.value) {
    if (p.sample !== sample) continue
    if (p.versionNum > currentNum) continue
    const existing = posMap.get(p.pos)
    if (!existing || p.versionNum > existing.versionNum) {
      posMap.set(p.pos, p)
    }
  }
  return Array.from(posMap.values())
})

async function generateReport() {
  if (!canGenerate.value) return
  loading.value = true
  error.value = ''
  pdfError.value = ''
  pptError.value = ''
  grainSettingsError.value = ''
  reportData.value = null
  versionedReportData.value = null
  analysisRes.value = null
  doneCount.value = 0
  loadingTotal.value = 0
  pairs.value = []

  try {
    const filteredPairs = selectedLibraryPairs.value
    loadingTotal.value = filteredPairs.length
    if (!filteredPairs.length) {
      throw new Error('檔案庫中找不到可用的掃描檔')
    }

    pairs.value = filteredPairs.map(toReportPair)
    const res = await $fetch<ReportDataResponse>('/api/reports/data', {
      method: 'POST',
      credentials: 'include',
      body: {
        sample: selectedSample.value,
        golden: goldenSample.value,
        min_grain_size: grainSettings.value.minGrainSize,
      },
    })
    reportData.value = res.reportData
    versionedReportData.value = res.versionedReportData
    rawVersionPositions.value = res.rawVersionPositions
    doneCount.value = loadingTotal.value
  } catch (e: unknown) {
    const err = e as { data?: { detail?: string; message?: string }; message?: string }
    error.value = err.data?.detail || err.data?.message || err.message || '未知錯誤'
  } finally {
    loading.value = false
  }
}

function toReportPair(storedPair: StoredEbsdPair): FilePair {
  return {
    id: storedPair.id,
    name: storedPair.name,
    sample: storedPair.sample,
    pos: storedPair.position,
    versionKey: storedPair.version_key || `v${storedPair.version_num ?? 1}`,
    versionLabel: storedPair.version_label || `${storedPair.sample}-${storedPair.version_num ?? 1}`,
    versionNum: storedPair.version_num ?? 1,
  }
}

function getVersionSnapshot(sample: string, versionKey: string): Record<string, CppFeatures> {
  return versionedReportData.value?.[sample]?.[versionKey] ?? reportData.value?.[sample] ?? {}
}

function getDisplayedVersionKey(sample: string): string {
  if (sample === selectedSample.value) {
    return currentSelectedVersionOption.value?.key ?? ''
  }
  return getLatestVersionOption(getVersionOptions(sample))?.key ?? ''
}

function getLatestSampleSnapshot(sample: string): Record<string, CppFeatures> {
  return reportData.value?.[sample] ?? {}
}

function getDisplayedSampleSnapshot(sample: string): Record<string, CppFeatures> {
  const versionKey = getDisplayedVersionKey(sample)
  return versionKey ? getVersionSnapshot(sample, versionKey) : getLatestSampleSnapshot(sample)
}

function getDisplayedPosData(sample: string, pos: string): CppFeatures | undefined {
  return getDisplayedSampleSnapshot(sample)?.[pos]
}

async function loadReportDataWithMinGrainSize(minGrainSize: number) {
  const res = await $fetch<ReportDataResponse>('/api/reports/data', {
    method: 'POST',
    credentials: 'include',
    body: {
      sample: selectedSample.value,
      golden: goldenSample.value,
      min_grain_size: minGrainSize,
    },
  })
  reportData.value = res.reportData
  versionedReportData.value = res.versionedReportData
  rawVersionPositions.value = res.rawVersionPositions
}

async function applyGlobalMinGrainSize() {
  const nextMinGrainSize = Math.max(1, Math.floor(Number(grainMinGrainSizeDraft.value) || DEFAULT_GRAIN_MIN_SIZE))
  grainSettingsError.value = ''
  const minSizeChanged = nextMinGrainSize !== grainSettings.value.minGrainSize
  if (minSizeChanged && reportData.value) {
    grainSettingsApplying.value = true
    try {
      await loadReportDataWithMinGrainSize(nextMinGrainSize)
      analysisRes.value = null
    } catch (e: unknown) {
      const err = e as { data?: { detail?: string; message?: string }; message?: string }
      grainSettingsError.value = err.data?.detail || err.data?.message || err.message || 'Grain 設定資料更新失敗'
      return
    } finally {
      grainSettingsApplying.value = false
    }
  }

  grainSettings.value = {
    ...grainSettings.value,
    minGrainSize: nextMinGrainSize,
  }
}

function openHistogramSettings() {
  histBinStartModeDraft.value = grainSettings.value.histBinStartMode
  histBinWidthModeDraft.value = grainSettings.value.histBinWidthMode
  histBinWidthValueDraft.value = grainSettings.value.histBinWidthMode === 'ratio'
    ? grainSettings.value.histBinWidthRatio
    : grainSettings.value.histBinWidthAbsolute
  histogramSettingsOpen.value = true
}

function applyHistogramSettings() {
  const value = Math.max(histBinWidthModeDraft.value === 'ratio' ? 0.5 : 0.1, Number(histBinWidthValueDraft.value) || 0)
  grainSettings.value = {
    ...grainSettings.value,
    histBinStartMode: histBinStartModeDraft.value,
    histBinWidthMode: histBinWidthModeDraft.value,
    histBinWidthRatio: histBinWidthModeDraft.value === 'ratio' ? value : grainSettings.value.histBinWidthRatio,
    histBinWidthAbsolute: histBinWidthModeDraft.value === 'absolute' ? value : grainSettings.value.histBinWidthAbsolute,
  }
  histogramSettingsOpen.value = false
}

const grainSettingsSummary = computed(() => {
  const startLabel = grainSettings.value.histBinStartMode === 'zero' ? '0' : '最小值'
  const widthLabel = grainSettings.value.histBinWidthMode === 'ratio'
    ? `${formatSettingNumber(grainSettings.value.histBinWidthRatio)}% range`
    : `${formatSettingNumber(grainSettings.value.histBinWidthAbsolute)} μm`
  return `分箱起點 ${startLabel} / 間距 ${widthLabel}`
})

const grainBinPreviewText = computed(() => {
  const start = previewHistBinStart.value
  const width = previewHistBinWidth.value
  const bins = Math.max(1, Math.min(80, Math.ceil((sharedGrainBins.value.max - start) / Math.max(width, 1e-6))))
  return `預估分箱：${bins} 格，每格約 ${formatSettingNumber(width)} μm`
})

const previewHistBinStart = computed(() => {
  if (histBinStartModeDraft.value === 'zero') return 0
  return sharedGrainDataMin.value
})

const previewHistBinWidth = computed(() => {
  const start = previewHistBinStart.value
  const range = Math.max(sharedGrainBins.value.max - start, 0.1)
  const value = Math.max(Number(histBinWidthValueDraft.value) || 0, histBinWidthModeDraft.value === 'ratio' ? 0.5 : 0.1)
  if (histBinWidthModeDraft.value === 'ratio') return Math.max(0.1, range * value / 100)
  return value
})

function formatSettingNumber(value: number): string {
  if (Math.abs(value) >= 100 || Number.isInteger(value)) return String(Math.round(value))
  return value.toFixed(2).replace(/\.00$/, '').replace(/0$/, '')
}

const selectedDisplayedPositions = computed(() =>
  Object.keys(getDisplayedSampleSnapshot(selectedSample.value)).sort(sortPositionKeys),
)
const goldenDisplayedPositions = computed(() =>
  Object.keys(getDisplayedSampleSnapshot(goldenSample.value)).sort(sortPositionKeys),
)
const comparablePositions = computed(() => {
  const goldenSet = new Set(goldenDisplayedPositions.value)
  return selectedDisplayedPositions.value.filter((pos) => goldenSet.has(pos))
})
const missingGoldenPositions = computed(() => {
  const goldenSet = new Set(goldenDisplayedPositions.value)
  return selectedDisplayedPositions.value.filter((pos) => !goldenSet.has(pos))
})
const missingSelectedPositions = computed(() => {
  const selectedSet = new Set(selectedDisplayedPositions.value)
  return goldenDisplayedPositions.value.filter((pos) => !selectedSet.has(pos))
})
const positionStatusCards = computed(() => [
  { kind: 'ok', title: '可比較', items: comparablePositions.value },
  { kind: 'warning', title: 'Sample 有資料但 Golden 缺少', items: missingGoldenPositions.value },
  { kind: 'muted', title: 'Golden 有資料但 Sample 未上傳', items: missingSelectedPositions.value },
])

const currentReportData = computed<AllDataResult>(() => {
  const sampleNames = new Set([
    ...Object.keys(versionedReportData.value ?? {}),
    ...Object.keys(reportData.value ?? {}),
  ])

  return Object.fromEntries(
    Array.from(sampleNames).map((sample) => [sample, getDisplayedSampleSnapshot(sample)]),
  )
})

function getCompareSampleGrains(pos: string): number[] {
  return getDisplayedPosData(selectedSample.value, pos)?.grains ?? []
}
function getCompareGoldenGrains(pos: string): number[] {
  return getDisplayedPosData(goldenSample.value, pos)?.grains ?? []
}
function getGridPosData(pos: string): CppFeatures | undefined {
  return getDisplayedPosData(selectedSample.value, pos)
}

function sortPositionKeys(a: string, b: string): number {
  return a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' })
}

function isMissingGoldenPos(pos: string): boolean {
  return missingGoldenPositions.value.includes(pos)
}

function isMissingSelectedPos(pos: string): boolean {
  return missingSelectedPositions.value.includes(pos) || !getGridPosData(pos)
}

function positionStatusClass(pos: string): Record<string, boolean> {
  return {
    'pos-missing-golden': isMissingGoldenPos(pos),
    'pos-missing-selected': !isMissingGoldenPos(pos) && isMissingSelectedPos(pos),
  }
}

const sharedGrainValues = computed(() => {
  const values: number[] = []
  const selectedSnapshot = getDisplayedSampleSnapshot(selectedSample.value)
  const goldenSnapshot = getDisplayedSampleSnapshot(goldenSample.value)

  for (const pos of Object.keys(selectedSnapshot)) {
    values.push(...(selectedSnapshot[pos]?.grains ?? []))
  }
  for (const pos of Object.keys(goldenSnapshot)) {
    values.push(...(goldenSnapshot[pos]?.grains ?? []))
  }

  return values.filter((v) => Number.isFinite(v) && v > 0)
})

const sharedGrainDataMin = computed(() => {
  const finitePositive = sharedGrainValues.value
  if (!finitePositive.length) {
    return 7.1
  }

  return Math.floor(Math.min(...finitePositive) * 10) / 10
})

const sharedGrainBins = computed(() => {
  const min = grainSettings.value.histBinStartMode === 'zero' ? 0 : sharedGrainDataMin.value
  const max = 200
  const range = Math.max(max - min, 0.1)
  const rawWidth = grainSettings.value.histBinWidthMode === 'ratio'
    ? range * Math.max(grainSettings.value.histBinWidthRatio, 0.5) / 100
    : Math.max(grainSettings.value.histBinWidthAbsolute, 0.1)
  const binWidth = Math.max(rawWidth, 0.1)
  const binCount = Math.max(1, Math.min(80, Math.ceil(range / binWidth)))
  return { min, max, binCount, binWidth }
})


function fmtGrain(pos: string, stat: 'mean' | 'max' | 'p75' | 'count'): string {
  const g = getGridPosData(pos)?.grains ?? []
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
  const arr = getGridPosData(pos)?.[key] as number[] | undefined
  if (!arr) return '—'
  return (arr[idx] * 100).toFixed(1)
}

function getOrientLineValues(rowKey: string, dev: OrientDev, colKey: string): number[] {
  const key = `orientation_ratio(${dev})` as keyof CppFeatures
  const arr = getGridPosData(`${colKey}-${rowKey}`)?.[key] as number[] | undefined
  return [0, 1, 2].map((idx) => Number(((arr?.[idx] ?? 0) * 100).toFixed(1)))
}

function orientLineX(index: number): number {
  const plotWidth = ORIENT_LINE_LAYOUT.width - ORIENT_LINE_LAYOUT.left - ORIENT_LINE_LAYOUT.right
  const segments = Math.max(1, ORIENT_LINE_X_LABELS.length - 1)
  return ORIENT_LINE_LAYOUT.left + (plotWidth * index) / segments
}

function orientLineY(value: number): number {
  const plotHeight = ORIENT_LINE_LAYOUT.height - ORIENT_LINE_LAYOUT.top - ORIENT_LINE_LAYOUT.bottom
  const clampedValue = Math.min(ORIENT_LINE_LAYOUT.maxValue, Math.max(0, value))
  return ORIENT_LINE_LAYOUT.top + plotHeight * (1 - clampedValue / ORIENT_LINE_LAYOUT.maxValue)
}

function orientLineLabelY(y: number): number {
  return Math.max(ORIENT_LINE_LAYOUT.top + 11, y - 8)
}

function buildOrientLineChart(rowKey: string, dev: OrientDev) {
  const plot = {
    left: ORIENT_LINE_LAYOUT.left,
    right: ORIENT_LINE_LAYOUT.width - ORIENT_LINE_LAYOUT.right,
    top: ORIENT_LINE_LAYOUT.top,
    bottom: ORIENT_LINE_LAYOUT.height - ORIENT_LINE_LAYOUT.bottom,
  }
  return {
    viewBox: `0 0 ${ORIENT_LINE_LAYOUT.width} ${ORIENT_LINE_LAYOUT.height}`,
    plot,
    xTicks: ORIENT_LINE_X_LABELS.map((label, index) => ({
      label,
      x: orientLineX(index),
    })),
    yTicks: ORIENT_LINE_Y_TICKS.map((value) => ({
      label: String(value),
      y: orientLineY(value),
    })),
    series: COL_LINE_SERIES_INFO.filter((info) => visibleLineCols.value.has(info.colKey)).map((info) => {
      const points = getOrientLineValues(rowKey, dev, info.colKey).map((value, index) => ({
        x: orientLineX(index),
        y: orientLineY(value),
        labelY: orientLineLabelY(orientLineY(value)),
        label: `${value.toFixed(1)}%`,
      }))
      return {
        ...info,
        points,
        polyline: points.map((point) => `${point.x},${point.y}`).join(' '),
      }
    }),
  }
}

const orientLineCharts20 = computed(() =>
  ORIENT_LINE_ROWS.map((row) => ({
    ...row,
    chart: buildOrientLineChart(row.rowKey, '20%'),
  })),
)

const orientLineCharts15 = computed(() =>
  ORIENT_LINE_ROWS.map((row) => ({
    ...row,
    chart: buildOrientLineChart(row.rowKey, '15%'),
  })),
)

function isGridPosRetestedNext(pos: string): boolean {
  const opts = selectedSampleVersionOptions.value
  const currentIdx = Math.min(reportVersionIndex.value, Math.max(0, opts.length - 1))
  const nextKey = opts[currentIdx + 1]?.key
  if (!nextKey) return false
  const nextPositions = rawVersionPositions.value?.[selectedSample.value]?.[nextKey] ?? []
  return nextPositions.includes(pos)
}

function getColorForValue(valuePercent: number): string {
  const [low, high] = colorThresholds.value
  if (Math.abs(valuePercent) <= low) return '#10B981'
  if (Math.abs(valuePercent) >= high) return '#EF4444'
  return '#F59E0B'
}

function safeFileName(value: string): string {
  return value.trim().replace(/[\\/:*?"<>|]+/g, '_').replace(/\s+/g, '_') || 'report'
}

async function waitForImages(root: HTMLElement) {
  const images = Array.from(root.querySelectorAll('img'))
  await Promise.all(images.map((img) => {
    if (img.complete) return Promise.resolve()
    return new Promise<void>((resolve) => {
      img.onload = () => resolve()
      img.onerror = () => resolve()
    })
  }))
}

function createPdfCaptureClone(section: HTMLElement) {
  const root = document.createElement('div')
  root.className = 'report-pdf-capture-root'
  root.style.width = `${PDF_CAPTURE_WIDTH}px`

  const clone = section.cloneNode(true) as HTMLElement
  clone.style.margin = '0'
  root.appendChild(clone)
  document.body.appendChild(root)

  return { root, clone }
}

function waitForNextFrame() {
  return new Promise<void>((resolve) => {
    requestAnimationFrame(() => resolve())
  })
}

async function exportReportPdf() {
  if (!reportData.value || exportingPdf.value) return
  pdfError.value = ''
  pdfExportProgress.value = ''
  exportingPdf.value = true
  document.body.classList.add('report-pdf-exporting')

  try {
    await nextTick()

    const sections = Array.from(document.querySelectorAll<HTMLElement>('[data-pdf-page]'))
    if (!sections.length) {
      throw new Error('找不到可輸出的報表區塊')
    }

    const [{ jsPDF }, html2canvasModule] = await Promise.all([
      import('jspdf'),
      import('html2canvas'),
    ])
    const html2canvas = html2canvasModule.default
    const pdf = new jsPDF({ orientation: 'landscape', unit: 'mm', format: 'a4', compress: true })
    const pageWidth = pdf.internal.pageSize.getWidth()
    const pageHeight = pdf.internal.pageSize.getHeight()
    const maxImageWidth = pageWidth - PDF_MARGIN_MM * 2
    const maxImageHeight = pageHeight - PDF_MARGIN_MM * 2

    for (const [index, section] of sections.entries()) {
      pdfExportProgress.value = `${index + 1}/${sections.length}`
      await waitForNextFrame()

      const { root, clone } = createPdfCaptureClone(section)
      try {
        await waitForImages(clone)
        await nextTick()

        const canvas = await html2canvas(clone, {
          backgroundColor: '#ffffff',
          scale: PDF_CAPTURE_SCALE,
          useCORS: true,
          allowTaint: false,
          width: clone.scrollWidth,
          height: clone.scrollHeight,
          windowWidth: PDF_CAPTURE_WIDTH,
          ignoreElements: (element) => element.classList?.contains('pdf-exclude') ?? false,
        })

        if (index > 0) pdf.addPage()
        const imageData = canvas.toDataURL('image/jpeg', 0.95)
        const ratio = Math.min(maxImageWidth / canvas.width, maxImageHeight / canvas.height)
        const imageWidth = canvas.width * ratio
        const imageHeight = canvas.height * ratio
        const x = (pageWidth - imageWidth) / 2
        const y = (pageHeight - imageHeight) / 2
        pdf.addImage(imageData, 'JPEG', x, y, imageWidth, imageHeight, undefined, 'FAST')
      } finally {
        root.remove()
      }
    }

    const stamp = new Date().toISOString().slice(0, 10)
    pdf.save(`EBSD-report-${safeFileName(selectedSample.value)}-${stamp}.pdf`)
  } catch (e: unknown) {
    const err = e as { message?: string }
    pdfError.value = err.message || 'PDF 輸出失敗'
  } finally {
    document.body.classList.remove('report-pdf-exporting')
    exportingPdf.value = false
    pdfExportProgress.value = ''
  }
}

async function exportReportPpt() {
  if (!reportData.value || exportingPpt.value) return
  pptError.value = ''
  exportingPpt.value = true

  try {
    const blob = await $fetch<Blob>('/api/reports/pptx', {
      method: 'POST',
      responseType: 'blob',
      credentials: 'include',
      body: {
        sample: selectedSample.value,
        golden: goldenSample.value,
        version_key: currentSelectedVersionOption.value?.key,
        min_grain_size: grainSettings.value.minGrainSize,
      },
    })
    const stamp = new Date().toISOString().slice(0, 10)
    const url = URL.createObjectURL(blob)
    const link = document.createElement('a')
    link.href = url
    link.download = `EBSD-report-${safeFileName(selectedSample.value)}-${stamp}.pptx`
    document.body.appendChild(link)
    link.click()
    link.remove()
    URL.revokeObjectURL(url)
  } catch (e: unknown) {
    const err = e as { data?: { detail?: string; message?: string }; message?: string }
    pptError.value = err.data?.detail || err.data?.message || err.message || 'PPT 輸出失敗'
  } finally {
    exportingPpt.value = false
  }
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
  if (comparablePositions.value.length === 0) {
    ElMessageBox.alert('目前分析樣本與 Golden Sample 沒有共同位置，無法進行比較；報表仍會保留已上傳位置並標示缺少對位資料。', '缺少對應位置', {
      confirmButtonText: 'OK',
    })
    return
  }
  if (missingGoldenPositions.value.length > 0) {
    ElMessage.warning(`缺少 Golden 對應位置：${missingGoldenPositions.value.join('、')}，這些位置不納入分析`)
  }
  analysisRes.value = null
  const res = await $fetch<AnalysisResult>('/api/analysis', {
    method: 'POST',
    body: {
      golden: goldenSample.value,
      features: selectedFeatures.value,
      data: currentReportData.value,
    },
  })
  analysisRes.value = res
}

watch(reportVersionIndex, async () => {
  if (analysisRes.value && selectedFeatures.value.length > 0 && goldenSample.value !== '') {
    await analysis()
  }
})

function buildOrientSeries(colKey: string, dev: '20%' | '15%') {
  const ratioKey = `orientation_ratio(${dev})` as keyof CppFeatures
  const series: Array<{ name: string; color: string; dashed: boolean; values: [number, number, number] }> = []

  for (const { rowKey, color } of ROW_SERIES_INFO) {
    if (!visibleRows.value.has(rowKey)) continue
    const pos = `${colKey}-${rowKey}`

    const sampleArr = getDisplayedPosData(selectedSample.value, pos)?.[ratioKey] as number[] | undefined
    series.push({
      name: `${pos} (${selectedSample.value})`,
      color,
      dashed: false,
      values: sampleArr
        ? [(sampleArr[0] ?? 0) * 100, (sampleArr[1] ?? 0) * 100, (sampleArr[2] ?? 0) * 100]
        : [0, 0, 0],
    })

    const goldenArr = getDisplayedPosData(goldenSample.value, pos)?.[ratioKey] as number[] | undefined
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
.page-shell {
  max-width: 1120px;
  margin: 0 auto;
  padding: 2rem 1.5rem 7rem;
  color: #111827;
}

.page-head,
.panel-title-row,
.action-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 1rem;
}

.page-head {
  margin-bottom: 1.25rem;
}

.page-head h1,
.panel-title-row h2 {
  margin: 0;
  font-weight: 650;
  letter-spacing: 0;
}

.page-head h1 {
  font-size: 1.8rem;
}

.page-head p,
.panel-title-row p {
  margin: .35rem 0 0;
  color: #6b7280;
}

.panel,
.card {
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  padding: 1.25rem;
  margin-bottom: 1.25rem;
}

.card {
  padding: 1.5rem;
}

.section-title {
  margin: 0 0 1rem;
  font-size: 1.1rem;
  font-weight: 700;
  color: #1f2937;
}

.ghost-btn,
.ghost-link,
.primary-btn {
  border-radius: 6px;
  min-height: 38px;
  padding: 0 .95rem;
  cursor: pointer;
  font-weight: 700;
}

.ghost-btn,
.ghost-link {
  border: 0;
  background: #f3f4f6;
  color: #111827;
  display: inline-flex;
  align-items: center;
  text-decoration: none;
}

.primary-btn {
  border: 0;
  background: #111827;
  color: #fff;
}

.primary-btn:disabled,
.ghost-btn:disabled {
  cursor: not-allowed;
  opacity: .55;
}

.report-setup-panel {
  padding: 1.25rem;
}

.setup-grid {
  display: grid;
  grid-template-columns: minmax(0, 1.1fr) minmax(280px, .9fr);
  gap: 1rem;
  margin-top: 1rem;
  align-items: start;
}

.target-picker,
.library-overview {
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  padding: 1rem;
}

.section-subhead {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: .75rem;
  margin-bottom: .75rem;
}

.section-subhead strong {
  font-weight: 600;
}

.section-subhead span {
  color: #6b7280;
  font-size: .85rem;
  font-weight: 700;
}

.selector-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: .9rem;
}

.field {
  display: grid;
  gap: .35rem;
}

.field span {
  color: #374151;
  font-size: .9rem;
  font-weight: 700;
}

.field select {
  width: 100%;
  border: 1px solid #d1d5db;
  border-radius: 6px;
  min-height: 40px;
  padding: .65rem .75rem;
  background: #fff;
}

.selected-summary {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: .75rem;
  margin-top: .9rem;
}

.selected-card {
  border: 1px solid #dbeafe;
  border-radius: 8px;
  background: #eff6ff;
  display: grid;
  gap: .18rem;
  padding: .75rem;
}

.selected-card.golden {
  border-color: #fed7aa;
  background: #fff7ed;
}

.selected-card span {
  color: #64748b;
  font-size: .76rem;
  font-weight: 800;
}

.selected-card strong {
  overflow-wrap: anywhere;
}

.selected-card small {
  color: #64748b;
  font-weight: 700;
}

.action-row {
  justify-content: flex-start;
  margin-top: 1rem;
  flex-wrap: wrap;
}

.library-card-list {
  display: grid;
  gap: .55rem;
  max-height: 320px;
  overflow: auto;
}

.interaction-hint {
  color: #64748b;
  font-size: .82rem;
  font-weight: 700;
  margin: .35rem 0 .6rem;
}

.library-target-card {
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  background: #fff;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: .75rem;
  padding: .75rem;
  text-align: left;
}

.library-target-card:hover,
.library-target-card.selected {
  border-color: #2563eb;
  background: #eff6ff;
}

.library-target-card.golden {
  border-color: #f59e0b;
  background: #fff7ed;
}

.library-target-card div {
  display: grid;
  gap: .16rem;
  min-width: 0;
}

.library-target-card strong {
  align-items: center;
  display: flex;
  flex-wrap: wrap;
  gap: .35rem;
  overflow-wrap: anywhere;
}

.library-target-card span,
.library-target-card small {
  color: #64748b;
  font-weight: 700;
}

.role-pill {
  border-radius: 999px;
  font-size: .68rem;
  font-style: normal;
  font-weight: 900;
  line-height: 1;
  padding: .24rem .42rem;
}

.role-pill.analysis {
  background: #dbeafe;
  color: #1d4ed8;
}

.role-pill.golden {
  background: #ffedd5;
  color: #c2410c;
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

.err-msg { font-size: .85rem; }

.report-action-bar {
  display: flex;
  justify-content: flex-end;
  align-items: center;
  gap: .75rem;
  margin: 0 0 1rem;
}
.pdf-btn {
  padding: .52rem 1.25rem;
  background: #2563EB;
  color: #fff;
  border: none;
  border-radius: 8px;
  font-size: .9rem;
  font-weight: 700;
  cursor: pointer;
  transition: background .15s;
}
.pdf-btn:hover:not(:disabled) { background: #1d4ed8; }
.pdf-btn:disabled {
  background: #93c5fd;
  cursor: not-allowed;
}

.position-status-card {
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 12px;
  padding: 1.25rem;
  margin-bottom: 1.5rem;
  box-shadow: 0 1px 4px rgba(0,0,0,.04);
}
.status-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: .8rem;
}
.status-block {
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  padding: .8rem;
  background: #f9fafb;
}
.status-ok {
  border-color: #bbf7d0;
  background: #f0fdf4;
}
.status-warning {
  border-color: #fed7aa;
  background: #fff7ed;
}
.status-muted {
  border-color: #e5e7eb;
  background: #f9fafb;
}
.status-title {
  font-weight: 800;
  color: #111827;
}
.status-meta {
  color: #6b7280;
  font-size: .82rem;
  margin-top: .2rem;
}
.status-tags {
  display: flex;
  flex-wrap: wrap;
  gap: .35rem;
  margin-top: .65rem;
}
.status-tags span {
  border-radius: 999px;
  background: #fff;
  border: 1px solid #e5e7eb;
  color: #374151;
  font-size: .78rem;
  font-weight: 700;
  padding: .18rem .5rem;
}
.status-tags .empty-tag {
  color: #9ca3af;
  font-weight: 600;
}

.chart-legend-row, .orient-legend-row {
  display: flex;
  align-items: center;
  gap: .5rem;
  margin-bottom: 1rem;
  flex-wrap: wrap;
}
.legend-lbl { font-size: .82rem; color: #374151; margin-right: .5rem; }
.grain-card-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 1rem;
  margin-bottom: .65rem;
}
.grain-card-head .section-title {
  margin-bottom: .2rem;
}
.grain-settings-summary {
  color: #64748b;
  font-size: .78rem;
  font-weight: 600;
  line-height: 1.45;
}
.grain-settings-btn {
  display: inline-flex;
  align-items: center;
  border: 1px solid #bfdbfe;
  border-radius: 999px;
  background: #eff6ff;
  color: #1d4ed8;
  cursor: pointer;
  font-size: .82rem;
  font-weight: 800;
  padding: .42rem .9rem;
  white-space: nowrap;
}
.grain-settings-btn:hover {
  border-color: #60a5fa;
  background: #dbeafe;
}
.grain-settings-panel {
  display: grid;
  gap: .9rem;
}
.grain-setting-block {
  border: 1px solid #e5e7eb;
  border-radius: 14px;
  background: #f8fafc;
  padding: .95rem;
}
.grain-setting-title {
  color: #0f172a;
  font-size: .92rem;
  font-weight: 850;
  margin-bottom: .22rem;
}
.grain-setting-desc {
  color: #64748b;
  font-size: .8rem;
  line-height: 1.5;
}
.grain-radio-row {
  margin-top: .55rem;
}
.grain-setting-input-row {
  display: flex;
  align-items: center;
  gap: .65rem;
  margin: .7rem 0 .35rem;
}
.grain-number-input {
  width: 180px;
}
.grain-input-unit {
  color: #475569;
  font-size: .82rem;
  font-weight: 800;
}
.grain-settings-footer {
  display: flex;
  justify-content: flex-end;
  gap: .6rem;
}
.grain-dialog-cancel,
.grain-dialog-apply {
  border-radius: 999px;
  cursor: pointer;
  font-size: .84rem;
  font-weight: 800;
  padding: .48rem 1rem;
}
.grain-dialog-cancel {
  border: 1px solid #d1d5db;
  background: #fff;
  color: #475569;
}
.grain-dialog-apply {
  border: 1px solid #2563eb;
  background: #2563eb;
  color: #fff;
}
.grain-dialog-cancel:hover {
  background: #f8fafc;
}
.grain-dialog-apply:hover {
  background: #1d4ed8;
}
.grain-settings-dialog :deep(.el-dialog) {
  border-radius: 18px;
}
.grain-settings-dialog :deep(.el-dialog__header) {
  border-bottom: 1px solid #e5e7eb;
  margin-right: 0;
  padding-bottom: .85rem;
}
.grain-settings-dialog :deep(.el-dialog__title) {
  color: #0f172a;
  font-weight: 900;
}
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
  margin: .3rem 0 1rem;
  color: #6b7280;
  font-size: .82rem;
  line-height: 1.55;
}
.report-version-dock {
  position: fixed;
  left: 18px;
  bottom: 18px;
  z-index: 40;
  width: min(340px, calc(100vw - 28px));
  padding: .95rem 1.05rem .82rem;
  border: 1px solid rgba(191, 219, 254, 0.95);
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.94);
  box-shadow: 0 14px 30px rgba(15, 23, 42, 0.12);
  backdrop-filter: blur(10px);
  max-height: calc(100vh - 36px);
  overflow-x: hidden;
  overflow-y: auto;
}
.report-version-dock * {
  box-sizing: border-box;
}
.report-version-dock__title {
  margin-bottom: .4rem;
  font-size: .82rem;
  font-weight: 800;
  letter-spacing: .04em;
  color: #1d4ed8;
}
.report-version-dock__meta {
  display: flex;
  flex-direction: column;
  gap: .22rem;
  font-size: .83rem;
  line-height: 1.35;
  color: #475569;
}
.report-version-dock__meta span {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.report-version-dock :deep(.el-slider) {
  margin: .7rem 0 .1rem;
}
.report-version-dock :deep(.el-slider__runway) {
  margin: 10px 0 4px;
  height: 6px;
}
.report-version-dock :deep(.el-slider__button) {
  width: 18px;
  height: 18px;
}
.report-version-dock :deep(.el-slider__stop) {
  width: 6px;
  height: 6px;
}
.dock-divider {
  height: 1px;
  margin: .9rem 0 .75rem;
  background: #cbd5e1;
}
.grain-dock-summary {
  color: #475569;
  font-size: .78rem;
  font-weight: 700;
  line-height: 1.45;
  margin: -.15rem 0 .45rem;
}
.grain-dock-field {
  display: grid;
  gap: .35rem;
  margin-top: .62rem;
}
.grain-dock-field > span {
  color: #334155;
  font-size: .76rem;
  font-weight: 850;
}
.grain-dock-field small {
  color: #64748b;
  font-size: .72rem;
  font-weight: 650;
  line-height: 1.4;
}
.grain-dock-number {
  max-width: 100%;
  width: 148px;
}
.grain-dock-apply {
  width: 100%;
  border: 1px solid #2563eb;
  border-radius: 999px;
  background: #2563eb;
  color: #fff;
  cursor: pointer;
  font-size: .82rem;
  font-weight: 850;
  margin-top: .75rem;
  padding: .48rem .75rem;
}
.grain-dock-apply:hover {
  background: #1d4ed8;
}
.grain-dock-apply:disabled {
  cursor: not-allowed;
  opacity: .65;
}
.grain-settings-error {
  display: block;
  font-size: .76rem;
  line-height: 1.4;
  margin-top: .45rem;
}
.version-slider-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
  margin-bottom: 1rem;
}
.grain-version-grid {
  grid-template-columns: 1fr;
}
.version-slider-card {
  border: 1px solid #e5e7eb;
  background: #f9fafb;
  border-radius: 10px;
  padding: .9rem 1rem .65rem;
}
.grid-version-card {
  margin-bottom: 1rem;
}
.version-slider-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 1rem;
  margin-bottom: .45rem;
  font-size: .86rem;
  color: #374151;
}
.version-slider-title {
  font-weight: 700;
}
.version-slider-ends {
  display: flex;
  justify-content: space-between;
  gap: 1rem;
  margin-top: .3rem;
  font-size: .76rem;
  color: #6b7280;
}
.version-slider-card :deep(.el-slider) {
  margin: .45rem 0 .1rem;
}
.version-slider-card :deep(.el-slider__runway) {
  margin: 8px 0;
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
.cdf-cell.pos-missing-golden {
  background: #fff7ed;
  border-color: #fdba74;
}
.cdf-cell.pos-missing-selected {
  background: #f3f4f6;
  border-color: #d1d5db;
  opacity: .82;
}
.cdf-pos-label {
  font-size: .78rem;
  font-weight: 700;
  color: #374151;
  margin-bottom: 4px;
  text-align: center;
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
.ng-cell.pos-missing-golden {
  background: #fff7ed;
  border-color: #fdba74;
  border-left-color: #ea580c;
}
.ng-cell.pos-missing-selected {
  background: #f3f4f6;
  border-color: #d1d5db;
  border-left-color: #9ca3af;
}
.ng-cell-inner {
  padding: 10px 12px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.cell-status-tag {
  border-radius: 999px;
  align-self: flex-start;
  font-size: .72rem;
  font-weight: 800;
  padding: .18rem .55rem;
}
.cell-status-tag.warning {
  background: #ffedd5;
  color: #9a3412;
  border: 1px solid #fdba74;
}
.cell-status-tag.muted {
  background: #e5e7eb;
  color: #4b5563;
  border: 1px solid #d1d5db;
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
.orient-line-controls {
  margin-bottom: .85rem;
}
.orient-line-version {
  margin-left: auto;
  display: inline-flex;
  align-items: center;
  padding: .24rem .72rem;
  border-radius: 999px;
  border: 1px solid #bfdbfe;
  background: #eff6ff;
  color: #1d4ed8;
  font-size: .76rem;
  font-weight: 700;
}
.orient-line-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 12px;
}
.orient-line-panel {
  background: #f9fafb;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  padding: 12px 10px 10px;
}
.orient-line-svg {
  display: block;
  width: 100%;
  height: auto;
}
.orient-line-axis {
  stroke: #94a3b8;
  stroke-width: 1.2;
}
.orient-line-gridline {
  stroke: #e5e7eb;
  stroke-width: 1;
  stroke-dasharray: 3 4;
}
.orient-line-path {
  fill: none;
  stroke-width: 3;
  stroke-linecap: round;
  stroke-linejoin: round;
}
.orient-line-point {
  stroke: #fff;
  stroke-width: 1.5;
}
.orient-line-xtext,
.orient-line-ytext {
  fill: #6b7280;
  font-size: 11px;
  font-weight: 600;
}
.orient-line-value {
  font-size: 10px;
  font-weight: 700;
  text-anchor: middle;
  paint-order: stroke;
  stroke: rgba(255, 255, 255, 0.96);
  stroke-width: 3px;
  stroke-linejoin: round;
}
.orient-line-xtext {
  text-anchor: middle;
}
.orient-line-ytext {
  text-anchor: end;
  dominant-baseline: middle;
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

.feat-label { font-size: .88rem; font-weight: 600; color: #374151; margin-bottom: .4rem; }
.feat-checkbox-group { display: flex; flex-wrap: wrap; gap: .3rem .8rem; margin-bottom: 1rem; }
.feat-checkbox-group :deep(.el-checkbox) {
  margin-right: 1rem;
  min-height: 32px;
}
.feat-checkbox-group :deep(.el-checkbox__label) {
  color: #374151;
  font-size: .95rem;
}

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

@media (max-width: 900px) {
  .setup-grid,
  .selector-grid,
  .selected-summary {
    grid-template-columns: 1fr;
  }
  .version-slider-grid { grid-template-columns: 1fr; }
  .status-grid { grid-template-columns: 1fr; }
}
@media (max-width: 700px) {
  .page-shell { padding-bottom: 13rem; }
  .page-head,
  .panel-title-row,
  .section-subhead {
    align-items: flex-start;
    flex-direction: column;
  }
  .report-version-dock {
    left: 12px;
    right: 12px;
    bottom: 12px;
    width: auto;
    max-height: min(70vh, 520px);
  }
  .grain-card-head {
    align-items: stretch;
    flex-direction: column;
  }
  .grain-settings-btn {
    justify-content: center;
    width: 100%;
  }
  .grain-setting-input-row,
  .grain-settings-footer {
    align-items: stretch;
    flex-direction: column;
  }
  .grain-number-input {
    width: 100%;
  }
  .grain-dock-number { width: 100%; }
  .nine-cdf-grid { grid-template-columns: repeat(2, 1fr); }
  .triangle-row { grid-template-columns: 1fr; }
  .orient-line-grid { grid-template-columns: 1fr; }
  .ng-header-row, .ng-data-row { grid-template-columns: 60px repeat(3, 1fr); }
}

:global(.report-pdf-capture-root) {
  position: fixed;
  left: -12000px;
  top: 0;
  width: 1120px;
  background: #fff;
  color: #111827;
  pointer-events: none;
  z-index: -1;
}
:global(.report-pdf-capture-root .card) {
  margin: 0;
  box-shadow: none;
}
:global(.report-pdf-capture-root .pdf-exclude),
:global(body.report-pdf-exporting .pdf-exclude) {
  display: none !important;
}
</style>
