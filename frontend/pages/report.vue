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
            <option v-for="s in sampleOptions" :key="s" :value="s">{{ s }}</option>
          </select>
        </div>
        <div class="selector-group">
          <label>黃金樣本（基準）</label>
          <select v-model="goldenSample" class="sel">
            <option value="">— 請選擇 —</option>
            <option v-for="s in sampleOptions" :key="s" :value="s">{{ s }}</option>
          </select>
        </div>
      </div>
      <div class="btn-row">
        <button class="gen-btn" @click="generateReport" :disabled="!canGenerate || loading">
          <span v-if="loading">處理中… ({{ doneCount }}/{{ loadingTotal || pairs.length }})</span>
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

        <div class="version-slider-grid grain-version-grid">
          <div class="version-slider-card">
            <div class="version-slider-head">
              <span class="version-slider-title">分析樣本版本</span>
              <strong>{{ currentSelectedGrainVersionLabel }}</strong>
            </div>
            <el-slider
              v-model="grainSampleVersionIndex"
              :min="0"
              :max="Math.max(0, selectedSampleVersionOptions.length - 1)"
              :step="1"
              :show-stops="selectedSampleVersionOptions.length > 1"
              :show-tooltip="false"
              :disabled="selectedSampleVersionOptions.length <= 1"
            />
            <div class="version-slider-ends">
              <span>{{ selectedSampleVersionOptions[0]?.label || '—' }}</span>
              <span>{{ selectedSampleVersionOptions[selectedSampleVersionOptions.length - 1]?.label || '—' }}</span>
            </div>
          </div>
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
          <span class="legend-lbl">{{ currentSelectedGrainVersionLabel }}</span>
          <svg v-if="grainChartMode === 'cdf'" width="30" height="14"><line x1="0" y1="7" x2="30" y2="7" stroke="#F59E0B" stroke-width="2.5" stroke-dasharray="5,2.5"/></svg>
          <svg v-else width="16" height="14">
            <rect x="2" y="2" width="12" height="10" fill="#F59E0B" opacity="0.28"/>
            <rect x="2" y="2" width="12" height="10" fill="none" stroke="#F59E0B" stroke-width="1" stroke-dasharray="2,1"/>
          </svg>
          <span class="legend-lbl">{{ currentGoldenGrainVersionLabel }} (黃金)</span>
        </div>

        <div class="nine-cdf-grid">
          <template v-for="(rowLabel, rowKey) in ROW_LABELS" :key="rowKey">
            <template v-for="colKey in COL_KEYS" :key="colKey">
              <div class="cdf-cell">
                <div class="cdf-pos-label">{{ colKey }}-{{ rowKey }}</div>
                <GrainCdfChart
                  :sample="getCompareSampleGrains(`${colKey}-${rowKey}`)"
                  :golden="getCompareGoldenGrains(`${colKey}-${rowKey}`)"
                  :sampleLabel="currentSelectedGrainVersionLabel"
                  :goldenLabel="currentGoldenGrainVersionLabel"
                  :mode="grainChartMode"
                  :fixedXMin="sharedGrainBins.min"
                  :fixedXMax="sharedGrainBins.max"
                  :binCount="sharedGrainBins.binCount"
                />
              </div>
            </template>
          </template>
        </div>
      </section>

      <!-- ── Section 2: Nine-grid full data ────────── -->
      <section class="card">
        <h2 class="section-title">九宮格完整數據 — {{ currentGridVersionLabel || selectedSample }}</h2>
        <div class="version-slider-card grid-version-card">
          <div class="version-slider-head">
            <span class="version-slider-title">九宮格版本</span>
            <strong>{{ currentGridVersionLabel }}</strong>
          </div>
          <el-slider
            v-model="gridVersionIndex"
            :min="0"
            :max="Math.max(0, selectedSampleVersionOptions.length - 1)"
            :step="1"
            :show-stops="selectedSampleVersionOptions.length > 1"
            :show-tooltip="false"
            :disabled="selectedSampleVersionOptions.length <= 1"
          />
          <div class="version-slider-ends">
            <span>{{ selectedSampleVersionOptions[0]?.label || '—' }}</span>
            <span>{{ selectedSampleVersionOptions[selectedSampleVersionOptions.length - 1]?.label || '—' }}</span>
          </div>
        </div>

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
              :class="{ 'ng-cell-retest': isGridPosRetestedNext(`${colKey}-${rowKey}`) }"
            >
              <div class="ng-cell-inner">
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
      <section class="card">
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
            <span class="orient-line-version">{{ currentGridVersionLabel || selectedSample }}</span>
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
      <section class="card">
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
            <span class="orient-line-version">{{ currentGridVersionLabel || selectedSample }}</span>
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
interface FilePair {
  name: string
  crc: File
  cpr: File
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

type AnalysisResult = Record<string, Record<string, number>>

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
const HIST_BIN_COUNT = 16

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

const folderInput = ref<HTMLInputElement>()
const pairs = ref<FilePair[]>([])
const samples = ref(new Set<string>())
const sampleOptions = computed(() =>
  Array.from(samples.value).sort((a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' })),
)
const selectedSample = ref('')
const goldenSample = ref('')
const loading = ref(false)
const doneCount = ref(0)
const loadingTotal = ref(0)
const error = ref('')
const reportData = ref<AllDataResult | null>(null)
const versionedReportData = ref<VersionedDataResult | null>(null)
const versionOptionsMap = ref<Record<string, VersionOption[]>>({})
const rawVersionPositions = ref<Record<string, Record<string, string[]>>>({})

const { data: featuresOptions } = await useFetch<string[]>('/api/ebsd_features')
const selectedFeatures = ref<string[]>([])
const colorThresholds = ref([30, 70])
const analysisRes = ref<AnalysisResult | null>(null)
const grainChartMode = ref<'cdf' | 'hist' | 'areaHist'>('cdf')

const gridVersionIndex = ref(0)
const grainSampleVersionIndex = ref(0)
const grainGoldenVersionIndex = ref(0)

const canGenerate = computed(
  () =>
    selectedSample.value !== '' &&
    goldenSample.value !== '' &&
    selectedSample.value !== goldenSample.value,
)

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

function parseSampleFolder(subdir: string) {
  const trimmed = subdir.trim()
  const m = trimmed.match(/^(.*?)-(\d+)$/)
  if (!m) {
    return { sample: trimmed, versionKey: '01', versionLabel: trimmed, versionNum: 1 }
  }
  return {
    sample: m[1],
    versionKey: m[2].padStart(2, '0'),
    versionLabel: trimmed,
    versionNum: Number.parseInt(m[2], 10) || 1,
  }
}

function processFiles(fileList: FileList | null | undefined) {
  if (!fileList?.length) return
  reportData.value = null
  versionedReportData.value = null
  error.value = ''

  const allFiles = Array.from(fileList)
  const crcFiles = new Map<string, { file: File; sample: string; pos: string; versionKey: string; versionLabel: string; versionNum: number }>()
  const cprFiles = new Map<string, File>()
  const versionMap: Record<string, Map<string, VersionOption>> = {}

  samples.value = new Set()
  pairs.value = []

  for (const f of allFiles) {
    if (!f.name.endsWith('.crc') && !f.name.endsWith('.cpr')) continue
    const parts = f.webkitRelativePath.split('/')
    const subdir = (parts[1] || '').trim()
    if (!subdir) continue

    const { sample, versionKey, versionLabel, versionNum } = parseSampleFolder(subdir)
    const pos = standard_pos(f.name.replace(/\.(crc|cpr)$/i, '').trim())
    const key = `${sample}::${versionKey}::${pos}`

    samples.value.add(sample)
    if (!versionMap[sample]) versionMap[sample] = new Map()
    versionMap[sample].set(versionKey, { key: versionKey, label: versionLabel, num: versionNum })

    if (f.name.endsWith('.crc')) {
      crcFiles.set(key, { file: f, sample, pos, versionKey, versionLabel, versionNum })
    } else {
      cprFiles.set(key, f)
    }
  }

  const matchedPairs: FilePair[] = []
  for (const [key, info] of crcFiles) {
    const cpr = cprFiles.get(key)
    if (!cpr) continue
    matchedPairs.push({
      name: key,
      crc: info.file,
      cpr,
      sample: info.sample,
      pos: info.pos,
      versionKey: info.versionKey,
      versionLabel: info.versionLabel,
      versionNum: info.versionNum,
    })
  }

  matchedPairs.sort((a, b) =>
    a.sample.localeCompare(b.sample, undefined, { numeric: true, sensitivity: 'base' }) ||
    a.versionNum - b.versionNum ||
    a.pos.localeCompare(b.pos, undefined, { numeric: true, sensitivity: 'base' }),
  )

  pairs.value = matchedPairs
  versionOptionsMap.value = Object.fromEntries(
    Object.entries(versionMap).map(([sample, map]) => [
      sample,
      Array.from(map.values()).sort((a, b) => a.num - b.num || a.label.localeCompare(b.label, undefined, { numeric: true, sensitivity: 'base' })),
    ]),
  )
}

function getVersionOptions(sample: string): VersionOption[] {
  return versionOptionsMap.value[sample] ?? []
}

const selectedSampleVersionOptions = computed(() => getVersionOptions(selectedSample.value))
const goldenSampleVersionOptions = computed(() => getVersionOptions(goldenSample.value))

watch(selectedSampleVersionOptions, (opts) => {
  const last = Math.max(0, opts.length - 1)
  gridVersionIndex.value = last
  grainSampleVersionIndex.value = last
}, { immediate: true })

watch(goldenSampleVersionOptions, (opts) => {
  grainGoldenVersionIndex.value = Math.max(0, opts.length - 1)
}, { immediate: true })

const currentGridVersionKey = computed(() => selectedSampleVersionOptions.value[Math.min(gridVersionIndex.value, Math.max(0, selectedSampleVersionOptions.value.length - 1))]?.key ?? '')
const currentSelectedGrainVersionKey = computed(() => selectedSampleVersionOptions.value[Math.min(grainSampleVersionIndex.value, Math.max(0, selectedSampleVersionOptions.value.length - 1))]?.key ?? '')
const currentGoldenGrainVersionKey = computed(() => goldenSampleVersionOptions.value[Math.min(grainGoldenVersionIndex.value, Math.max(0, goldenSampleVersionOptions.value.length - 1))]?.key ?? '')

const currentGridVersionLabel = computed(() => selectedSampleVersionOptions.value[Math.min(gridVersionIndex.value, Math.max(0, selectedSampleVersionOptions.value.length - 1))]?.label ?? selectedSample.value)
const currentSelectedGrainVersionLabel = computed(() => selectedSampleVersionOptions.value[Math.min(grainSampleVersionIndex.value, Math.max(0, selectedSampleVersionOptions.value.length - 1))]?.label ?? selectedSample.value)
const currentGoldenGrainVersionLabel = computed(() => goldenSampleVersionOptions.value[Math.min(grainGoldenVersionIndex.value, Math.max(0, goldenSampleVersionOptions.value.length - 1))]?.label ?? goldenSample.value)

async function generateReport() {
  if (!canGenerate.value) return
  loading.value = true
  error.value = ''
  reportData.value = null
  versionedReportData.value = null
  doneCount.value = 0
  loadingTotal.value = 0

  try {
    const needed = new Set([selectedSample.value, goldenSample.value])
    const filteredPairs = pairs.value.filter((p) => needed.has(p.sample))
    loadingTotal.value = filteredPairs.length

    const promises = filteredPairs.map(async (p) => {
      const formData = new FormData()
      formData.append('crc', p.crc)
      formData.append('cpr', p.cpr)
      const features = await $fetch<CppFeatures>('/cppapi/features', {
        method: 'POST',
        body: formData,
      })
      doneCount.value++
      return { sample: p.sample, versionKey: p.versionKey, versionLabel: p.versionLabel, versionNum: p.versionNum, pos: p.pos, features }
    })

    const results = await Promise.all(promises)
    const rawData: VersionedDataResult = {}
    const rawPosMap: Record<string, Record<string, Set<string>>> = {}

    for (const r of results) {
      if (!rawData[r.sample]) rawData[r.sample] = {}
      if (!rawData[r.sample][r.versionKey]) rawData[r.sample][r.versionKey] = {}
      rawData[r.sample][r.versionKey][r.pos] = r.features

      if (!rawPosMap[r.sample]) rawPosMap[r.sample] = {}
      if (!rawPosMap[r.sample][r.versionKey]) rawPosMap[r.sample][r.versionKey] = new Set()
      rawPosMap[r.sample][r.versionKey].add(r.pos)
    }

    const mergedLatest: AllDataResult = {}
    const cumulative: VersionedDataResult = {}

    for (const sample of Object.keys(rawData)) {
      const opts = getVersionOptions(sample)
      let acc: Record<string, CppFeatures> = {}
      cumulative[sample] = {}

      for (const opt of opts) {
        const patch = rawData[sample]?.[opt.key] ?? {}
        acc = { ...acc, ...patch }
        cumulative[sample][opt.key] = { ...acc }
      }

      const latestKey = opts[opts.length - 1]?.key
      if (latestKey) mergedLatest[sample] = cumulative[sample][latestKey] ?? {}
    }

    reportData.value = mergedLatest
    versionedReportData.value = cumulative
    rawVersionPositions.value = Object.fromEntries(
      Object.entries(rawPosMap).map(([sample, versions]) => [
        sample,
        Object.fromEntries(
          Object.entries(versions).map(([versionKey, posSet]) => [versionKey, Array.from(posSet)]),
        ),
      ]),
    )
  } catch (e: unknown) {
    const err = e as { data?: { message?: string }; message?: string }
    error.value = err.data?.message || err.message || '未知錯誤'
  } finally {
    loading.value = false
  }
}

function getVersionSnapshot(sample: string, versionKey: string): Record<string, CppFeatures> {
  return versionedReportData.value?.[sample]?.[versionKey] ?? reportData.value?.[sample] ?? {}
}

function getCompareSampleGrains(pos: string): number[] {
  return getVersionSnapshot(selectedSample.value, currentSelectedGrainVersionKey.value)?.[pos]?.grains ?? []
}
function getCompareGoldenGrains(pos: string): number[] {
  return getVersionSnapshot(goldenSample.value, currentGoldenGrainVersionKey.value)?.[pos]?.grains ?? []
}
function getGridPosData(pos: string): CppFeatures | undefined {
  return getVersionSnapshot(selectedSample.value, currentGridVersionKey.value)?.[pos]
}
function getLatestPosData(sample: string, pos: string): CppFeatures | undefined {
  return reportData.value?.[sample]?.[pos]
}

const sharedGrainBins = computed(() => {
  const values: number[] = []
  const selectedSnapshot = getVersionSnapshot(selectedSample.value, currentSelectedGrainVersionKey.value)
  const goldenSnapshot = getVersionSnapshot(goldenSample.value, currentGoldenGrainVersionKey.value)

  for (const pos of Object.keys(selectedSnapshot)) {
    values.push(...(selectedSnapshot[pos]?.grains ?? []))
  }
  for (const pos of Object.keys(goldenSnapshot)) {
    values.push(...(goldenSnapshot[pos]?.grains ?? []))
  }

  const finitePositive = values.filter((v) => Number.isFinite(v) && v > 0)
  if (!finitePositive.length) {
    return { min: 7.1, max: 200, binCount: HIST_BIN_COUNT }
  }

  const min = Math.floor(Math.min(...finitePositive) * 10) / 10
  return { min, max: 200, binCount: HIST_BIN_COUNT }
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
  const currentIdx = Math.min(gridVersionIndex.value, Math.max(0, opts.length - 1))
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

function buildOrientSeries(colKey: string, dev: '20%' | '15%') {
  const ratioKey = `orientation_ratio(${dev})` as keyof CppFeatures
  const series: Array<{ name: string; color: string; dashed: boolean; values: [number, number, number] }> = []

  for (const { rowKey, color } of ROW_SERIES_INFO) {
    if (!visibleRows.value.has(rowKey)) continue
    const pos = `${colKey}-${rowKey}`

    const sampleArr = getLatestPosData(selectedSample.value, pos)?.[ratioKey] as number[] | undefined
    series.push({
      name: `${pos} (${selectedSample.value})`,
      color,
      dashed: false,
      values: sampleArr
        ? [(sampleArr[0] ?? 0) * 100, (sampleArr[1] ?? 0) * 100, (sampleArr[2] ?? 0) * 100]
        : [0, 0, 0],
    })

    const goldenArr = getLatestPosData(goldenSample.value, pos)?.[ratioKey] as number[] | undefined
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
.report-page {
  max-width: 1100px;
  margin: 0 auto;
  padding: 1.5rem 1rem 4rem;
  font-family: ui-sans-serif, system-ui, 'Segoe UI', 'Noto Sans TC', Arial, sans-serif;
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
  margin: .3rem 0 1rem;
  color: #6b7280;
  font-size: .82rem;
  line-height: 1.55;
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
  .version-slider-grid { grid-template-columns: 1fr; }
}
@media (max-width: 700px) {
  .nine-cdf-grid { grid-template-columns: repeat(2, 1fr); }
  .triangle-row { grid-template-columns: 1fr; }
  .orient-line-grid { grid-template-columns: 1fr; }
  .ng-header-row, .ng-data-row { grid-template-columns: 60px repeat(3, 1fr); }
}
</style>