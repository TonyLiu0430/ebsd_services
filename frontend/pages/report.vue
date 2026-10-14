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

    <section id="report-setup" class="panel report-setup-panel">
      <div class="panel-title-row">
        <div>
          <h2>報表資料來源</h2>
          <p>從檔案庫選擇分析樣本；Golden 樣本為選填，未選擇時顯示單筆樣本資料。</p>
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
            <span>{{ comparablePositionCount }} 個{{ goldenSample ? '可比對' : '樣本' }}位置</span>
          </div>
          <div class="selector-grid">
            <label class="field">
              <span>分析樣本</span>
              <select v-model="selectedSample">
                <option value="">請選擇</option>
                <optgroup v-for="group in libraryMaterialGroups" :key="group.key" :label="group.displayName">
                  <option v-for="card in group.samples" :key="card.sample" :value="card.sample">
                    {{ card.sample }}
                  </option>
                </optgroup>
              </select>
            </label>
            <label class="field">
              <span>Golden 樣本（選填）</span>
              <select v-model="goldenSample">
                <option value="">不使用 Golden</option>
                <optgroup v-for="group in goldenMaterialGroups" :key="group.key" :label="group.displayName">
                  <option v-for="card in group.samples" :key="card.sample" :value="card.sample">
                    {{ card.sample }}
                  </option>
                </optgroup>
              </select>
            </label>
          </div>

          <div class="selected-summary">
            <article class="selected-card">
              <span>分析</span>
              <strong>{{ selectedSample || '未選擇' }}</strong>
              <em v-if="selectedSampleCard" class="selected-material">{{ selectedSampleCard.materialDisplayName }}</em>
              <small>{{ selectedSampleSummary }}</small>
            </article>
            <article class="selected-card golden">
              <span>Golden</span>
              <strong>{{ goldenSample || '單筆模式' }}</strong>
              <em v-if="goldenSampleCard" class="selected-material">{{ goldenSampleCard.materialDisplayName }}</em>
              <small>{{ goldenSample ? goldenSampleSummary : '不進行 Golden 比較與特徵分析' }}</small>
            </article>
          </div>

          <div class="action-row">
            <button class="primary-btn" @click="generateReport" :disabled="!canGenerate || loading">
              <span v-if="loading">處理中 {{ doneCount }}/{{ loadingTotal || selectedLibraryPairs.length }}</span>
              <span v-else>產生報表</span>
            </button>
            <el-text v-if="materialMismatchMessage" type="danger" class="err-msg">{{ materialMismatchMessage }}</el-text>
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
            <details
              v-for="group in libraryMaterialGroups"
              :key="group.key"
              class="library-material-section"
              :style="materialStyle(group.element)"
              open
            >
              <summary class="library-material-head">
                <strong>{{ group.displayName }}</strong>
                <small>{{ group.samples.length }} 筆靶材</small>
              </summary>
              <button
                v-for="card in group.samples"
                :key="card.sample"
                type="button"
                class="library-target-card"
                :class="{
                  selected: card.sample === selectedSample,
                  golden: card.sample === goldenSample,
                  incompatible: !isCompatibleGoldenCard(card),
                }"
                :title="isCompatibleGoldenCard(card) ? '左鍵選分析，右鍵指定 Golden' : 'Golden 必須與分析樣本是相同金屬'"
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
            </details>
          </div>
        </div>
      </div>
    </section>

    <!-- ══════════════ REPORT ══════════════ -->
    <template v-if="reportData">
      <div
        v-if="!versionDockCollapsed"
        class="report-version-dock"
        aria-label="Report version switcher"
      >
        <button
          class="report-version-dock__collapse"
          type="button"
          aria-label="收合左下角控制面板"
          title="收合"
          @click="versionDockCollapsed = true"
        >收合</button>
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

        <div class="grain-dock-control-row">
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
            {{ grainSettingsApplying ? '更新中…' : '套用' }}
          </button>
        </div>
        <el-text v-if="grainSettingsError" type="danger" class="grain-settings-error">
          {{ grainSettingsError }}
        </el-text>
      </div>
      <button
        v-else
        class="report-version-dock-dot"
        type="button"
        aria-label="展開左下角控制面板"
        title="展開控制面板"
        @click="versionDockCollapsed = false"
      ></button>

      <div class="report-action-bar">
        <button class="export-btn" @click="exportReportPpt" :disabled="exportingPpt">
          <span v-if="exportingPpt">輸出 PPT 中… {{ pptElapsedSeconds }} 秒</span>
          <span v-else>輸出 PPT</span>
        </button>
        <el-text v-if="pptError" type="danger" class="err-msg">{{ pptError }}</el-text>
      </div>

      <!-- ── Section 1: Grain distribution curves ──────────── -->
      <section id="grain-distribution" class="card">
        <div class="grain-card-head">
          <div>
            <h2 class="section-title">Grain 粒徑分布{{ goldenSample ? '比對' : '' }}</h2>
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
          <template v-if="grainChartMode === 'hist'">
            <span class="grain-mode-divider"></span>
            <span class="grain-mode-label">Y 軸</span>
            <button
              class="grain-mode-btn"
              :class="{ active: histogramYMode === 'count' }"
              @click="histogramYMode = 'count'"
            >Count</button>
            <button
              class="grain-mode-btn"
              :class="{ active: histogramYMode === 'ratio' }"
              @click="histogramYMode = 'ratio'"
            >比例 (%)</button>
          </template>
        </div>

        <p v-if="grainChartMode === 'areaHist'" class="grain-mode-note">
          以 grain 等效直徑 d 換算面積權重，單顆 grain 權重採用 π(d/2)^2，圖上顯示落各粒徑區間的面積占比。
        </p>

        <div class="chart-legend-row">
          <svg v-if="grainChartMode === 'cdf'" width="30" height="14"><line x1="0" y1="7" x2="30" y2="7" stroke="#DC2626" stroke-width="2.5" stroke-dasharray="5,2.5"/></svg>
          <svg v-else width="16" height="14">
            <rect x="2" y="2" width="12" height="10" fill="#DC2626" opacity="0.22"/>
            <rect x="2" y="2" width="12" height="10" fill="none" stroke="#DC2626" stroke-width="1" stroke-dasharray="2,1"/>
          </svg>
          <span class="legend-lbl">{{ currentSelectedVersionLabel }}</span>
          <template v-if="goldenSample">
            <svg v-if="grainChartMode === 'cdf'" width="30" height="14"><line x1="0" y1="7" x2="30" y2="7" stroke="#2563EB" stroke-width="2.5"/></svg>
            <svg v-else width="16" height="14">
              <rect x="2" y="2" width="12" height="10" fill="#2563EB" opacity="0.24"/>
              <rect x="2" y="2" width="12" height="10" fill="none" stroke="#2563EB" stroke-width="1"/>
            </svg>
            <span class="legend-lbl">{{ currentGoldenVersionLabel }} (黃金)</span>
          </template>
        </div>

        <div class="nine-cdf-grid" :style="dynamicCardGridStyle(reportPositionMatrix.columnCount)">
          <div
            v-for="pos in reportPositionMatrix.positions"
            :key="pos"
            class="cdf-cell"
            :class="positionStatusClass(pos)"
          >
            <div class="cdf-pos-label">{{ pos }}</div>
            <div v-if="isMissingGoldenPos(pos)" class="cell-status-tag warning">缺 Golden</div>
            <div v-else-if="isMissingSelectedPos(pos)" class="cell-status-tag muted">未上傳</div>
            <GrainCdfChart
              :sample="getCompareSampleGrains(pos)"
              :golden="getCompareGoldenGrains(pos)"
              :sampleLabel="currentSelectedVersionLabel"
              :goldenLabel="currentGoldenVersionLabel"
              :mode="grainChartMode"
              :histYMode="histogramYMode"
              :fixedXMin="sharedGrainBins.min"
              :fixedXMax="sharedGrainBins.max"
              :binCount="sharedGrainBins.binCount"
              :binWidth="sharedGrainBins.binWidth"
              :fixedYMax="sharedGrainYAxisMax"
              :maxXTicks="6"
              :showXAxis="true"
              :showYAxis="true"
            />
          </div>
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
              <span class="grain-input-unit">{{ histBinWidthModeDraft === 'ratio' ? '% of range' : 'µm' }}</span>
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
      <section id="ipf-map" v-if="pairs.length" class="card">
        <div class="ipf-section-head">
          <div>
            <h2 class="section-title">IPF 晶粒取向分佈圖 — {{ ipfDisplayTitle }}</h2>
            <div class="ipf-settings-summary">參考方向向量：({{ formattedIpfReferenceVector }}) / De-noindex：{{ formattedIpfDenoindexThreshold }}% / Misorientation：{{ formattedIpfDenoindexMisorientationThreshold }}°</div>
          </div>
          <div class="ipf-head-actions">
            <div class="ipf-legend-popover" tabindex="0">
              <button class="ipf-legend-trigger" type="button">IPF 色鍵</button>
              <div class="ipf-legend-card" role="tooltip">
                <div class="ipf-legend-title">IPF 色鍵</div>
                <div class="ipf-legend-figure">
                  <img :src="ipfLegendUrl" alt="IPF triangle color key" class="ipf-legend-img" />
                  <span class="ipf-legend-label label-111">{{ ipfLegendLabels.top }}</span>
                  <span class="ipf-legend-label label-001">{{ ipfLegendLabels.left }}</span>
                  <span class="ipf-legend-label label-101">{{ ipfLegendLabels.right }}</span>
                </div>
                <small>色鍵用來解讀 IPF map 的晶向顏色。</small>
              </div>
            </div>
            <div v-if="goldenSample" class="sample-display-toggle" aria-label="IPF 顯示樣本">
              <button
                type="button"
                :class="{ active: ipfDisplayTarget === 'analysis' }"
                @click="ipfDisplayTarget = 'analysis'"
              >分析樣本</button>
              <button
                type="button"
                :class="{ active: ipfDisplayTarget === 'golden' }"
                @click="ipfDisplayTarget = 'golden'"
              >Golden 樣本</button>
            </div>
            <button class="ipf-settings-btn" type="button" @click="openIpfSettings">設定</button>
          </div>
        </div>
        <IpfMapGrid
          :pairs="ipfMapPairs"
          :sample="ipfDisplaySample"
          :referenceVector="ipfReferenceVector"
          :denoindexThreshold="ipfDenoindexThreshold"
          :denoindexMisorientationThreshold="ipfDenoindexMisorientationThreshold"
        />
      </section>

      <el-dialog
        v-model="ipfSettingsOpen"
        title="IPF 參考方向向量設定"
        width="520px"
        class="ipf-settings-dialog"
        append-to-body
      >
        <div class="ipf-settings-panel">
          <div class="ipf-setting-block">
            <div class="ipf-setting-title">快速選擇</div>
            <el-radio-group v-model="ipfPresetDraft" class="ipf-preset-row" @change="applyIpfPresetDraft">
              <el-radio-button label="x" value="x">X</el-radio-button>
              <el-radio-button label="y" value="y">Y</el-radio-button>
              <el-radio-button label="z" value="z">Z</el-radio-button>
            </el-radio-group>
          </div>

          <div class="ipf-setting-block">
            <div class="ipf-setting-title">具體向量</div>
            <div class="ipf-vector-inputs">
              <label>
                <span>X</span>
                <el-input-number v-model="ipfVectorDraft.x" :step="0.1" controls-position="right" />
              </label>
              <label>
                <span>Y</span>
                <el-input-number v-model="ipfVectorDraft.y" :step="0.1" controls-position="right" />
              </label>
              <label>
                <span>Z</span>
                <el-input-number v-model="ipfVectorDraft.z" :step="0.1" controls-position="right" />
              </label>
            </div>
            <div class="ipf-setting-desc">套用時會自動歸一化；零向量不可使用。</div>
          </div>

          <div class="ipf-setting-block">
            <div class="ipf-setting-title">De-noindex 參數</div>
            <div class="ipf-threshold-input">
              <label>
                <span>Threshold (%)</span>
                <el-input-number
                  v-model="ipfDenoindexThresholdDraft"
                  :min="0"
                  :max="100"
                  :step="1"
                  controls-position="right"
                />
              </label>
              <label>
                <span>Misorientation (°)</span>
                <el-input-number
                  v-model="ipfDenoindexMisorientationThresholdDraft"
                  :min="0"
                  :max="180"
                  :step="1"
                  controls-position="right"
                />
              </label>
            </div>
            <div class="ipf-setting-desc">控制 IPF map 產生前填補 no-index 點的門檻；Threshold 預設 100%，Misorientation 預設 10°。</div>
          </div>
        </div>

        <template #footer>
          <div class="ipf-settings-footer">
            <button class="ipf-dialog-cancel" type="button" @click="ipfSettingsOpen = false">取消</button>
            <button class="ipf-dialog-apply" type="button" @click="applyIpfSettings">套用設定</button>
          </div>
        </template>
      </el-dialog>

      <!-- ── Section 2: Dynamic-position full data ────────── -->
      <section id="nine-grid-data" class="card">
        <div class="grid-section-head">
          <h2 class="section-title">完整位置數據 — {{ gridDisplayTitle }}</h2>
          <div v-if="goldenSample" class="sample-display-toggle" aria-label="完整位置顯示樣本">
            <button
              type="button"
              :class="{ active: gridDisplayTarget === 'analysis' }"
              @click="gridDisplayTarget = 'analysis'"
            >分析樣本</button>
            <button
              type="button"
              :class="{ active: gridDisplayTarget === 'golden' }"
              @click="gridDisplayTarget = 'golden'"
            >Golden 樣本</button>
          </div>
        </div>

        <div class="nine-grid-wrapper">
          <div class="ng-header-row" :style="dynamicTableGridStyle(reportPositionMatrix.columnCount)">
            <div class="ng-corner"></div>
            <div v-for="column in reportPositionMatrix.columns" :key="column" class="ng-col-header">
              {{ column }}
            </div>
          </div>
          <div
            v-for="row in reportPositionMatrix.rows"
            :key="row"
            class="ng-data-row"
            :style="dynamicTableGridStyle(reportPositionMatrix.columnCount)"
          >
            <div class="ng-row-header">{{ row }}</div>
            <div
              v-for="column in reportPositionMatrix.columns"
              :key="column"
              class="ng-cell"
              :class="{
                'ng-cell-retest': isGridPosRetestedNext(positionKey(column, row)),
                ...gridPositionStatusClass(positionKey(column, row)),
              }"
            >
              <div class="ng-cell-inner">
                <div class="ng-position-label">{{ positionKey(column, row) }}</div>
                <div v-if="!getGridPosData(positionKey(column, row))" class="cell-status-tag muted">無數據</div>
                <div class="stat-block">
                  <div class="stat-title">Grain Size (µm)</div>
                  <div class="stat-row">
                    <span class="stat-key">Mean</span>
                    <span class="stat-val">{{ fmtGrain(positionKey(column, row), 'mean') }}</span>
                  </div>
                  <div class="stat-row">
                    <span class="stat-key">Max</span>
                    <span class="stat-val">{{ fmtGrain(positionKey(column, row), 'max') }}</span>
                  </div>
                  <div class="stat-row">
                    <span class="stat-key">P75</span>
                    <span class="stat-val">{{ fmtGrain(positionKey(column, row), 'p75') }}</span>
                  </div>
                  <div class="stat-row">
                    <span class="stat-key">Count</span>
                    <span class="stat-val">{{ fmtGrain(positionKey(column, row), 'count') }}</span>
                  </div>
                </div>
                <div class="stat-block orient-block">
                  <div class="stat-title">Orientation Ratio</div>
                  <table class="orient-table">
                    <thead>
                      <tr>
                        <th></th>
                        <th v-for="label in currentOrientationLabels" :key="label">{{ label }}</th>
                      </tr>
                    </thead>
                    <tbody>
                      <tr>
                        <td class="dev-label">20°</td>
                        <td v-for="(_, idx) in currentOrientationLabels" :key="`20-${idx}`">{{ fmtOrient(positionKey(column, row), '20%', idx) }}%</td>
                      </tr>
                      <tr>
                        <td class="dev-label">15°</td>
                        <td v-for="(_, idx) in currentOrientationLabels" :key="`15-${idx}`">{{ fmtOrient(positionKey(column, row), '15%', idx) }}%</td>
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
      <section id="orientation-triangle-20" class="card">
        <h2 class="section-title">晶粒取向比例 — Misorientation 20°</h2>
        <div class="orient-controls">
          <div class="orient-legend-row">
            <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#2563EB" stroke-width="1.5"/></svg>
            <span class="legend-lbl">{{ selectedSample }} · {{ currentSelectedVersionLabel }}（實線）</span>
            <template v-if="goldenSample">
              <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#F59E0B" stroke-width="1.5" stroke-dasharray="4,2"/></svg>
              <span class="legend-lbl">{{ goldenSample }} · {{ currentGoldenVersionLabel }}（虛線）</span>
            </template>
          </div>
          <div class="orient-toggle-bar">
            <div class="label-toggle-group">
              <span class="toggle-label">數字顯示:</span>
              <button class="simple-toggle-btn" :class="{ active: showTriangleLabels }" @click="toggleTriangleLabels()">{{ showTriangleLabels ? '隱藏' : '顯示' }}</button>
            </div>
          </div>
        </div>
        <div class="triangle-row" :style="dynamicCardGridStyle(reportPositionMatrix.columnCount)">
          <div
            v-for="pos in reportPositionMatrix.positions"
            :key="pos"
            class="triangle-panel"
            :class="positionStatusClass(pos)"
          >
            <div class="tri-col-title">{{ pos }}</div>
            <div v-if="isMissingGoldenPos(pos)" class="cell-status-tag warning">缺 Golden</div>
            <div v-else-if="isMissingSelectedPos(pos)" class="cell-status-tag muted">未上傳</div>
            <TriangleChart :series="buildOrientSeries(pos, '20%')" :axisLabels="currentOrientationLabels" :showValueLabels="showTriangleLabels" :maxValue="orientationTriangleScaleMax('20%')" />
          </div>
        </div>
      </section>

      <!-- ── Section 3b: Orientation triangles 15° ── -->
      <section id="orientation-triangle-15" class="card">
        <h2 class="section-title">晶粒取向比例 — Misorientation 15°</h2>
        <div class="orient-controls">
          <div class="orient-legend-row">
            <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#2563EB" stroke-width="1.5"/></svg>
            <span class="legend-lbl">{{ selectedSample }} · {{ currentSelectedVersionLabel }}（實線）</span>
            <template v-if="goldenSample">
              <svg width="24" height="12"><line x1="0" y1="6" x2="24" y2="6" stroke="#F59E0B" stroke-width="1.5" stroke-dasharray="4,2"/></svg>
              <span class="legend-lbl">{{ goldenSample }} · {{ currentGoldenVersionLabel }}（虛線）</span>
            </template>
          </div>
          <div class="orient-toggle-bar">
            <div class="label-toggle-group">
              <span class="toggle-label">數字顯示:</span>
              <button class="simple-toggle-btn" :class="{ active: showTriangleLabels }" @click="toggleTriangleLabels()">{{ showTriangleLabels ? '隱藏' : '顯示' }}</button>
            </div>
          </div>
        </div>
        <div class="triangle-row" :style="dynamicCardGridStyle(reportPositionMatrix.columnCount)">
          <div
            v-for="pos in reportPositionMatrix.positions"
            :key="pos"
            class="triangle-panel"
            :class="positionStatusClass(pos)"
          >
            <div class="tri-col-title">{{ pos }}</div>
            <div v-if="isMissingGoldenPos(pos)" class="cell-status-tag warning">缺 Golden</div>
            <div v-else-if="isMissingSelectedPos(pos)" class="cell-status-tag muted">未上傳</div>
            <TriangleChart :series="buildOrientSeries(pos, '15%')" :axisLabels="currentOrientationLabels" :showValueLabels="showTriangleLabels" :maxValue="orientationTriangleScaleMax('15%')" />
          </div>
        </div>
      </section>

      <!-- Section 3c: Orientation line charts 20度 -->
      <section id="orientation-line-20" class="card">
        <h2 class="section-title">晶粒取向折線圖 · Misorientation 20°</h2>
        <div class="orient-controls orient-line-controls">
          <div class="orient-legend-row">
            <div class="orient-line-legend-group sample-style">
              <span class="orient-line-legend-title">樣本</span>
              <span class="orient-line-style-chip analysis">
                <svg width="28" height="12"><line x1="1" y1="6" x2="27" y2="6" stroke="#409eff" stroke-width="2.75" stroke-linecap="round" /></svg>
                <span>分析樣本 · {{ currentSelectedVersionLabel || selectedSample }}</span>
              </span>
              <span v-if="goldenSample" class="orient-line-style-chip golden">
                <svg width="28" height="12"><line x1="1" y1="6" x2="27" y2="6" stroke="#e6a23c" stroke-width="2.75" stroke-linecap="round" stroke-dasharray="5 4" /></svg>
                <span>Golden 樣本 · {{ currentGoldenVersionLabel || goldenSample }}</span>
              </span>
            </div>
          </div>
          <div class="orient-toggle-bar">
            <div class="label-toggle-group">
              <span class="toggle-label">數字顯示:</span>
              <button class="simple-toggle-btn" :class="{ active: showOrientLineLabels }" @click="toggleOrientLineLabels()">{{ showOrientLineLabels ? '隱藏' : '顯示' }}</button>
            </div>
            <label class="orient-y-max-control">
              <span>Y 軸上限</span>
              <input v-model.number="orientationLineYMax" type="number" min="1" step="10" placeholder="自動" />
              <button type="button" @click="orientationLineYMax = undefined">自動</button>
            </label>
          </div>
        </div>
        <div class="orient-line-grid">
          <div
            v-for="chartInfo in orientLineCharts20"
            :key="chartInfo.key"
            class="orient-line-panel"
            :style="{ backgroundColor: chartInfo.backgroundColor, borderColor: chartInfo.borderColor }"
          >
            <div class="tri-col-title">{{ chartInfo.title }}</div>
            <svg
              class="orient-line-svg"
              :viewBox="chartInfo.chart.viewBox"
              :style="{ minWidth: `${chartInfo.chart.width}px` }"
              role="img"
              :aria-label="`Misorientation 20度 ${chartInfo.title} 晶粒取向折線圖`"
            >
              <line
                v-for="tick in chartInfo.chart.yTicks"
                :key="`grid-20-${chartInfo.key}-${tick.label}`"
                :x1="chartInfo.chart.plot.left"
                :y1="tick.y"
                :x2="chartInfo.chart.plot.right"
                :y2="tick.y"
                class="orient-line-gridline"
              />
              <line
                :x1="chartInfo.chart.plot.left"
                :y1="chartInfo.chart.plot.top"
                :x2="chartInfo.chart.plot.left"
                :y2="chartInfo.chart.plot.bottom"
                class="orient-line-axis"
              />
              <line
                :x1="chartInfo.chart.plot.left"
                :y1="chartInfo.chart.plot.bottom"
                :x2="chartInfo.chart.plot.right"
                :y2="chartInfo.chart.plot.bottom"
                class="orient-line-axis"
              />
              <text
                v-for="tick in chartInfo.chart.yTicks"
                :key="`ylabel-20-${chartInfo.key}-${tick.label}`"
                :x="chartInfo.chart.plot.left - 8"
                :y="tick.y + 1"
                class="orient-line-ytext"
              >{{ tick.label }}</text>
              <text
                v-for="tick in chartInfo.chart.xTicks"
                :key="`xlabel-20-${chartInfo.key}-${tick.label}`"
                :x="tick.x"
                :y="chartInfo.chart.plot.bottom + 20"
                class="orient-line-xtext"
              >{{ tick.label }}</text>
              <g v-for="series in chartInfo.chart.series" :key="`series-20-${chartInfo.key}-${series.key}`">
                <polyline :points="series.polyline" :stroke="series.color" :stroke-dasharray="series.dashed ? '5 4' : undefined" class="orient-line-path" />
                <circle
                  v-for="point in series.points"
                  :key="`point-20-${chartInfo.key}-${series.key}-${point.x}`"
                  :cx="point.x"
                  :cy="point.y"
                  r="3.25"
                  :fill="series.color"
                  class="orient-line-point"
                />
                <text
                  v-if="showOrientLineLabels"
                  v-for="point in series.points"
                  :key="`label-20-${chartInfo.key}-${series.key}-${point.x}`"
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
      <section id="orientation-line-15" class="card">
        <h2 class="section-title">晶粒取向折線圖 · Misorientation 15°</h2>
        <div class="orient-controls orient-line-controls">
          <div class="orient-legend-row">
            <div class="orient-line-legend-group sample-style">
              <span class="orient-line-legend-title">樣本</span>
              <span class="orient-line-style-chip analysis">
                <svg width="28" height="12"><line x1="1" y1="6" x2="27" y2="6" stroke="#409eff" stroke-width="2.75" stroke-linecap="round" /></svg>
                <span>分析樣本 · {{ currentSelectedVersionLabel || selectedSample }}</span>
              </span>
              <span v-if="goldenSample" class="orient-line-style-chip golden">
                <svg width="28" height="12"><line x1="1" y1="6" x2="27" y2="6" stroke="#e6a23c" stroke-width="2.75" stroke-linecap="round" stroke-dasharray="5 4" /></svg>
                <span>Golden 樣本 · {{ currentGoldenVersionLabel || goldenSample }}</span>
              </span>
            </div>
          </div>
          <div class="orient-toggle-bar">
            <div class="label-toggle-group">
              <span class="toggle-label">數字顯示:</span>
              <button class="simple-toggle-btn" :class="{ active: showOrientLineLabels }" @click="toggleOrientLineLabels()">{{ showOrientLineLabels ? '隱藏' : '顯示' }}</button>
            </div>
            <label class="orient-y-max-control">
              <span>Y 軸上限</span>
              <input v-model.number="orientationLineYMax" type="number" min="1" step="10" placeholder="自動" />
              <button type="button" @click="orientationLineYMax = undefined">自動</button>
            </label>
          </div>
        </div>
        <div class="orient-line-grid">
          <div
            v-for="chartInfo in orientLineCharts15"
            :key="chartInfo.key"
            class="orient-line-panel"
            :style="{ backgroundColor: chartInfo.backgroundColor, borderColor: chartInfo.borderColor }"
          >
            <div class="tri-col-title">{{ chartInfo.title }}</div>
            <svg
              class="orient-line-svg"
              :viewBox="chartInfo.chart.viewBox"
              :style="{ minWidth: `${chartInfo.chart.width}px` }"
              role="img"
              :aria-label="`Misorientation 15度 ${chartInfo.title} 晶粒取向折線圖`"
            >
              <line
                v-for="tick in chartInfo.chart.yTicks"
                :key="`grid-15-${chartInfo.key}-${tick.label}`"
                :x1="chartInfo.chart.plot.left"
                :y1="tick.y"
                :x2="chartInfo.chart.plot.right"
                :y2="tick.y"
                class="orient-line-gridline"
              />
              <line
                :x1="chartInfo.chart.plot.left"
                :y1="chartInfo.chart.plot.top"
                :x2="chartInfo.chart.plot.left"
                :y2="chartInfo.chart.plot.bottom"
                class="orient-line-axis"
              />
              <line
                :x1="chartInfo.chart.plot.left"
                :y1="chartInfo.chart.plot.bottom"
                :x2="chartInfo.chart.plot.right"
                :y2="chartInfo.chart.plot.bottom"
                class="orient-line-axis"
              />
              <text
                v-for="tick in chartInfo.chart.yTicks"
                :key="`ylabel-15-${chartInfo.key}-${tick.label}`"
                :x="chartInfo.chart.plot.left - 8"
                :y="tick.y + 1"
                class="orient-line-ytext"
              >{{ tick.label }}</text>
              <text
                v-for="tick in chartInfo.chart.xTicks"
                :key="`xlabel-15-${chartInfo.key}-${tick.label}`"
                :x="tick.x"
                :y="chartInfo.chart.plot.bottom + 20"
                class="orient-line-xtext"
              >{{ tick.label }}</text>
              <g v-for="series in chartInfo.chart.series" :key="`series-15-${chartInfo.key}-${series.key}`">
                <polyline :points="series.polyline" :stroke="series.color" :stroke-dasharray="series.dashed ? '5 4' : undefined" class="orient-line-path" />
                <circle
                  v-for="point in series.points"
                  :key="`point-15-${chartInfo.key}-${series.key}-${point.x}`"
                  :cx="point.x"
                  :cy="point.y"
                  r="3.25"
                  :fill="series.color"
                  class="orient-line-point"
                />
                <text
                  v-if="showOrientLineLabels"
                  v-for="point in series.points"
                  :key="`label-15-${chartInfo.key}-${series.key}-${point.x}`"
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
      <section v-if="goldenSample" id="feature-analysis" class="card">
        <h2 class="section-title">④ 特徵分析 </h2>
        <div class="feature-selector-head">
          <div class="feat-label">選擇分析項目</div>
          <span class="feature-family-chip">{{ currentAnalysisFamilyLabel }}</span>
        </div>
        <div class="feature-group-list">
          <section v-for="group in visibleFeatureGroups" :key="group.key" class="feature-group">
            <div class="feature-group-title">
              <span>{{ group.label }}</span>
              <span>{{ group.options.length }}</span>
            </div>
            <el-checkbox-group v-model="selectedFeatures" class="feat-checkbox-group">
              <el-checkbox v-for="feat in group.options" :key="feat" :label="feat" :value="feat" />
            </el-checkbox-group>
          </section>
        </div>
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
          <div v-for="(sampleData, sampleName) in analysisRes" :key="sampleName" class="analysis-sample-result">
            <h4>{{ sampleName }}</h4>
            <div class="analysis-table-wrap">
              <div
                class="analysis-table-row analysis-table-head"
                :style="dynamicAnalysisGridStyle(analysisPositionMatrix(sampleData).columnCount)"
              >
                <div class="analysis-axis-cell"></div>
                <div v-for="column in analysisPositionMatrix(sampleData).columns" :key="column" class="analysis-axis-cell">
                  {{ column }}
                </div>
              </div>
              <div
                v-for="row in analysisPositionMatrix(sampleData).rows"
                :key="row"
                class="analysis-table-row"
                :style="dynamicAnalysisGridStyle(analysisPositionMatrix(sampleData).columnCount)"
              >
                <div class="analysis-axis-cell">{{ row }}</div>
                <div
                  v-for="column in analysisPositionMatrix(sampleData).columns"
                  :key="column"
                  class="analysis-value-cell"
                  :class="{ empty: sampleData[positionKey(column, row)] == null }"
                  :style="analysisCellStyle(sampleData[positionKey(column, row)])"
                >
                  <strong>{{ positionKey(column, row) }}</strong>
                  <span>{{ formatAnalysisValue(sampleData[positionKey(column, row)]) }}</span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </section>

    </template>

    <aside
      class="report-anchor-dock"
      :class="{ 'report-anchor-dock--raised': reportData && !versionDockCollapsed }"
      aria-label="報表章節導覽"
    >
      <div class="report-anchor-dock__title">章節導覽</div>
      <el-anchor :offset="80">
        <el-anchor-link href="#report-setup" title="資料來源" />
        <template v-if="reportData">
          <el-anchor-link href="#grain-distribution" title="Grain 粒徑" />
          <el-anchor-link v-if="pairs.length" href="#ipf-map" title="IPF 取向圖" />
          <el-anchor-link href="#nine-grid-data" title="完整位置數據" />
          <el-anchor-link href="#orientation-triangle-20" title="取向比例 20°" />
          <el-anchor-link href="#orientation-triangle-15" title="取向比例 15°" />
          <el-anchor-link href="#orientation-line-20" title="取向折線 20°" />
          <el-anchor-link href="#orientation-line-15" title="取向折線 15°" />
          <el-anchor-link v-if="goldenSample" href="#feature-analysis" title="特徵分析" />
        </template>
      </el-anchor>
    </aside>
  </div>
</template>

<script setup lang="ts">
import type { PositionMatrix } from '~/utils/positions'
import { roundUpToNearestTen } from '~/utils/chartScale'
import { buildPositionMatrix, comparePositions, parsePosition, positionKey } from '~/utils/positions'

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
  orientation_family?: string
  orientation_labels?: string[]
}
type AllDataResult = Record<string, Record<string, CppFeatures>>
type VersionedDataResult = Record<string, Record<string, Record<string, CppFeatures>>>
type VersionOption = { key: string; label: string; num: number }
type OrientDev = '20%' | '15%'
type HistBinStartMode = 'zero' | 'min'
type HistBinWidthMode = 'ratio' | 'absolute'
type IpfPreset = 'x' | 'y' | 'z'
type SampleDisplayTarget = 'analysis' | 'golden'
type FeatureFamily = 'all' | 'cubic' | 'hcp'
type FeatureGroupKey = 'grain' | 'cubic20' | 'cubic15' | 'hcp20' | 'hcp15' | 'other'
type FeatureGroup = { key: FeatureGroupKey; label: string; options: string[] }
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
type PptJob = {
  id: string
  status: 'queued' | 'running' | 'completed' | 'failed' | 'downloading'
  error?: string
  download_url?: string
}

type AnalysisResult = Record<string, Record<string, number>>
type StoredEbsdPair = {
  id: string
  target_id: string
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
  material_key?: string | null
  material_display_name?: string | null
  material_element?: string | null
  material_phase?: string | null
  material_confidence?: string | null
  material_source?: string | null
  created_at: string
  updated_at: string
}

const DEFAULT_ORIENTATION_LABELS = ['[001]', '[011]', '[111]'] as const
const IPF_ORIENTATION_PANEL_COLORS = [
  { backgroundColor: 'rgba(242, 92, 92, 0.035)', borderColor: 'rgba(242, 92, 92, 0.12)' },
  { backgroundColor: 'rgba(89, 209, 99, 0.035)', borderColor: 'rgba(89, 209, 99, 0.12)' },
  { backgroundColor: 'rgba(102, 115, 230, 0.035)', borderColor: 'rgba(102, 115, 230, 0.12)' },
] as const
const ORIENT_LINE_LAYOUT = {
  height: 220,
  left: 42,
  right: 16,
  top: 18,
  bottom: 36,
} as const
const HIST_BIN_COUNT = 20
const DEFAULT_GRAIN_MIN_SIZE = 10
const DEFAULT_HIST_BIN_RATIO = Number((100 / HIST_BIN_COUNT).toFixed(2))
const DEFAULT_HIST_BIN_ABSOLUTE = 10
const SHOW_MICROSTRUCTURE_PAGE = false
const UNKNOWN_MATERIAL = {
  key: 'unknown',
  displayName: 'Unknown',
  element: '',
}

const showTriangleLabels = ref(true)
function toggleTriangleLabels() {
  showTriangleLabels.value = !showTriangleLabels.value
}
const showOrientLineLabels = ref(true)
function toggleOrientLineLabels() {
  showOrientLineLabels.value = !showOrientLineLabels.value
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
const pptError = ref('')
const exportingPpt = ref(false)
const pptElapsedSeconds = ref(0)
let pptElapsedTimer: ReturnType<typeof setInterval> | null = null
let pptPollingCancelled = false
const reportData = ref<AllDataResult | null>(null)
const versionedReportData = ref<VersionedDataResult | null>(null)
const versionOptionsMap = ref<Record<string, VersionOption[]>>({})
const rawVersionPositions = ref<Record<string, Record<string, string[]>>>({})
const versionDockCollapsed = ref(false)

function orientationLabelKey(label: string): string {
  return label.replace(/[\[\]]/g, '').replace('−', '-').trim()
}

function orientationLabelsFromSnapshot(snapshot: Record<string, CppFeatures>): string[] {
  for (const data of Object.values(snapshot)) {
    if (Array.isArray(data.orientation_labels) && data.orientation_labels.length >= 3) {
      return data.orientation_labels.slice(0, 3)
    }
  }
  return [...DEFAULT_ORIENTATION_LABELS]
}

const currentOrientationLabels = computed(() => {
  if (!selectedSample.value) return [...DEFAULT_ORIENTATION_LABELS]
  return orientationLabelsFromSnapshot(getDisplayedSampleSnapshot(selectedSample.value))
})

const orientLineSeriesInfo = computed(() =>
  currentOrientationLabels.value.map((label, index) => ({
    orientationKey: orientationLabelKey(label),
    index,
    label,
    ...IPF_ORIENTATION_PANEL_COLORS[index % IPF_ORIENTATION_PANEL_COLORS.length],
  })),
)

const DEFAULT_FEATURE_OPTIONS = [
  'overall distribution : wasserstein_distance / mean',
  'mean',
  'max',
  '25 percentiles',
  '75 percentiles',
  'standard deviation',
  '[001] (deviation 20%)',
  '[011] (deviation 20%)',
  '[111] (deviation 20%)',
  '[0001] (deviation 20%)',
  '[10-10] (deviation 20%)',
  '[11-20] (deviation 20%)',
  '[001] (deviation 15%)',
  '[011] (deviation 15%)',
  '[111] (deviation 15%)',
  '[0001] (deviation 15%)',
  '[10-10] (deviation 15%)',
  '[11-20] (deviation 15%)',
] as const
const GRAIN_FEATURE_OPTIONS = new Set<string>([
  'overall distribution : wasserstein_distance / mean',
  'mean',
  'max',
  '25 percentiles',
  '75 percentiles',
  'standard deviation',
])
const CUBIC_FEATURE_AXES = new Set(['[001]', '[011]', '[111]'])
const HCP_FEATURE_AXES = new Set(['[0001]', '[10-10]', '[11-20]'])
const FEATURE_GROUP_META: Record<FeatureGroupKey, { label: string; family: FeatureFamily }> = {
  grain: { label: '晶粒尺寸', family: 'all' },
  cubic20: { label: 'Cubic 晶向 20%', family: 'cubic' },
  cubic15: { label: 'Cubic 晶向 15%', family: 'cubic' },
  hcp20: { label: 'HCP 晶向 20%', family: 'hcp' },
  hcp15: { label: 'HCP 晶向 15%', family: 'hcp' },
  other: { label: '其他', family: 'all' },
}
const FEATURE_GROUP_ORDER: FeatureGroupKey[] = ['grain', 'cubic20', 'cubic15', 'hcp20', 'hcp15', 'other']

const { data: featuresOptions } = await useFetch<string[]>('/api/ebsd_features')
const selectedFeatures = ref<string[]>([])
const colorThresholds = ref([30, 70])
const analysisRes = ref<AnalysisResult | null>(null)
const grainChartMode = ref<'cdf' | 'hist' | 'areaHist'>('hist')
const histogramYMode = ref<'count' | 'ratio'>('ratio')
const orientationLineYMax = ref<number | undefined>(undefined)
const histogramSettingsOpen = ref(false)
const ipfSettingsOpen = ref(false)
const grainSettingsApplying = ref(false)
const grainSettingsError = ref('')
const ipfDisplayTarget = ref<SampleDisplayTarget>('analysis')
const gridDisplayTarget = ref<SampleDisplayTarget>('analysis')
const grainSettings = ref<GrainSettings>({
  minGrainSize: DEFAULT_GRAIN_MIN_SIZE,
  histBinStartMode: 'zero',
  histBinWidthMode: 'absolute',
  histBinWidthRatio: DEFAULT_HIST_BIN_RATIO,
  histBinWidthAbsolute: DEFAULT_HIST_BIN_ABSOLUTE,
})
const grainMinGrainSizeDraft = ref(DEFAULT_GRAIN_MIN_SIZE)
const histBinStartModeDraft = ref<HistBinStartMode>('zero')
const histBinWidthModeDraft = ref<HistBinWidthMode>('absolute')
const histBinWidthValueDraft = ref(DEFAULT_HIST_BIN_ABSOLUTE)
const ipfReferenceVector = ref<[number, number, number]>([0, 0, 1])
const ipfDenoindexThreshold = ref(100)
const ipfDenoindexMisorientationThreshold = ref(10)
const ipfPresetDraft = ref<IpfPreset | ''>('z')
const ipfVectorDraft = ref({ x: 0, y: 0, z: 1 })
const ipfDenoindexThresholdDraft = ref(100)
const ipfDenoindexMisorientationThresholdDraft = ref(10)

watch(histBinWidthModeDraft, (mode) => {
  if (!histogramSettingsOpen.value) return
  histBinWidthValueDraft.value = mode === 'ratio'
    ? grainSettings.value.histBinWidthRatio
    : grainSettings.value.histBinWidthAbsolute
})

watch(ipfVectorDraft, () => {
  if (!ipfSettingsOpen.value) return
  ipfPresetDraft.value = detectIpfPresetFromDraft()
}, { deep: true })

const reportVersionIndex = ref(0)
const featureOptionsList = computed(() =>
  featuresOptions.value?.length ? featuresOptions.value : [...DEFAULT_FEATURE_OPTIONS],
)
const currentAnalysisFamily = computed<Exclude<FeatureFamily, 'all'>>(() =>
  orientationFamilyFromSnapshot(getDisplayedSampleSnapshot(selectedSample.value)),
)
const currentAnalysisFamilyLabel = computed(() =>
  currentAnalysisFamily.value === 'hcp' ? 'HCP' : 'Cubic',
)
const visibleFeatureGroups = computed<FeatureGroup[]>(() => {
  const groups = new Map<FeatureGroupKey, string[]>()

  for (const feat of featureOptionsList.value) {
    const groupKey = featureGroupKeyForFeature(feat)
    const meta = FEATURE_GROUP_META[groupKey]
    if (meta.family !== 'all' && meta.family !== currentAnalysisFamily.value) continue
    groups.set(groupKey, [...(groups.get(groupKey) ?? []), feat])
  }

  return FEATURE_GROUP_ORDER
    .map((key) => ({
      key,
      label: FEATURE_GROUP_META[key].label,
      options: groups.get(key) ?? [],
    }))
    .filter((group) => group.options.length > 0)
})
const visibleFeatureOptionValues = computed(() =>
  visibleFeatureGroups.value.flatMap((group) => group.options),
)

const canGenerate = computed(
  () =>
    selectedSample.value !== '' &&
    (
      goldenSample.value === '' ||
      (
        selectedSample.value !== goldenSample.value &&
        selectedAndGoldenSameMetal.value
      )
    ),
)
const selectedLibraryPairs = computed(() => {
  const needed = new Set([selectedSample.value, goldenSample.value].filter(Boolean))
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
      const material = primaryMaterialFromPairs(items)
      return {
        sample,
        materialKey: material.key,
        materialDisplayName: material.displayName,
        materialElement: material.element,
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
const libraryMaterialGroups = computed(() => groupLibraryCardsByMaterial(librarySampleCards.value))
const goldenMaterialGroups = computed(() => {
  if (!selectedSampleCard.value?.materialElement) return libraryMaterialGroups.value
  return groupLibraryCardsByMaterial(
    librarySampleCards.value.filter((card) => card.materialElement === selectedSampleCard.value?.materialElement),
  )
})
const selectedSampleCard = computed(() =>
  librarySampleCards.value.find((card) => card.sample === selectedSample.value),
)
const goldenSampleCard = computed(() =>
  librarySampleCards.value.find((card) => card.sample === goldenSample.value),
)
const selectedAndGoldenSameMetal = computed(() => {
  if (!selectedSampleCard.value || !goldenSampleCard.value) return false
  const selectedElement = selectedSampleCard.value.materialElement
  const goldenElement = goldenSampleCard.value.materialElement
  return !!selectedElement && selectedElement === goldenElement
})
const materialMismatchMessage = computed(() => {
  if (!selectedSample.value || !goldenSample.value || selectedSample.value === goldenSample.value) return ''
  if (selectedAndGoldenSameMetal.value) return ''
  const selectedName = selectedSampleCard.value?.materialDisplayName || 'Unknown'
  const goldenName = goldenSampleCard.value?.materialDisplayName || 'Unknown'
  return `分析樣本與 Golden 樣本必須是相同金屬，目前為 ${selectedName} / ${goldenName}`
})
const selectedSampleSummary = computed(() => formatSampleSummary(selectedSampleCard.value))
const goldenSampleSummary = computed(() => formatSampleSummary(goldenSampleCard.value))
const comparablePositionCount = computed(() => {
  if (!selectedSampleCard.value) return 0
  if (!goldenSampleCard.value) return selectedSampleCard.value.positions.size
  const goldenPositions = goldenSampleCard.value.positions
  return Array.from(selectedSampleCard.value.positions).filter((pos) => goldenPositions.has(pos)).length
})

watch(selectedSample, (sample) => {
  if (sample && sample === goldenSample.value) goldenSample.value = ''
  if (sample && goldenSample.value && !selectedAndGoldenSameMetal.value) goldenSample.value = ''
})

watch(goldenSample, (sample) => {
  if (sample && sample === selectedSample.value) selectedSample.value = ''
  if (sample && selectedSample.value && !selectedAndGoldenSameMetal.value) goldenSample.value = ''
  if (!sample) {
    ipfDisplayTarget.value = 'analysis'
    gridDisplayTarget.value = 'analysis'
    analysisRes.value = null
  }
})

function formatSampleSummary(card: typeof librarySampleCards.value[number] | undefined): string {
  if (!card) return '尚未選擇'
  return `${card.positions.size} 個位置 / ${card.versions.length} 個版本 / 最新 ${card.latestLabel}`
}

function primaryMaterialFromPairs(items: StoredEbsdPair[]) {
  const counts = new Map<string, { key: string; displayName: string; element: string; count: number }>()
  for (const item of items) {
    const key = item.material_key || UNKNOWN_MATERIAL.key
    const material = counts.get(key) ?? {
      key,
      displayName: item.material_display_name || UNKNOWN_MATERIAL.displayName,
      element: item.material_element || UNKNOWN_MATERIAL.element,
      count: 0,
    }
    material.count += 1
    counts.set(key, material)
  }
  return Array.from(counts.values()).sort((a, b) =>
    b.count - a.count || a.displayName.localeCompare(b.displayName, undefined, { numeric: true, sensitivity: 'base' }),
  )[0] ?? UNKNOWN_MATERIAL
}

function groupLibraryCardsByMaterial(cards: Array<typeof librarySampleCards.value[number]>) {
  const map = new Map<string, {
    key: string
    displayName: string
    element: string
    samples: Array<typeof librarySampleCards.value[number]>
  }>()
  for (const card of cards) {
    const key = card.materialKey || UNKNOWN_MATERIAL.key
    const group = map.get(key) ?? {
      key,
      displayName: card.materialDisplayName || UNKNOWN_MATERIAL.displayName,
      element: card.materialElement || UNKNOWN_MATERIAL.element,
      samples: [],
    }
    group.samples.push(card)
    map.set(key, group)
  }
  return Array.from(map.values()).sort((a, b) =>
    materialSortRank(a.element) - materialSortRank(b.element) ||
    a.displayName.localeCompare(b.displayName, undefined, { numeric: true, sensitivity: 'base' }),
  )
}

function materialSortRank(element: string): number {
  if (element === 'Cu') return 0
  if (!element) return 2
  return 1
}

function materialStyle(element: string): Record<string, string> {
  if (element === 'Cu') {
    return {
      '--material-bg': '#fff7d6',
      '--material-border': '#d99a17',
      '--material-text': '#8a5a00',
    }
  }
  if (element === 'Al') {
    return {
      '--material-bg': '#f3f4f6',
      '--material-border': '#b8c0cc',
      '--material-text': '#4b5563',
    }
  }
  if (element === 'Ta') {
    return {
      '--material-bg': '#dce4ef',
      '--material-border': '#66758a',
      '--material-text': '#2f3a4a',
    }
  }
  if (element === 'Ti') {
    return {
      '--material-bg': '#eef2f7',
      '--material-border': '#8a95a6',
      '--material-text': '#3f4a5a',
    }
  }
  return {
    '--material-bg': '#e5e7eb',
    '--material-border': '#9ca3af',
    '--material-text': '#374151',
  }
}

function isCompatibleGoldenCard(card: typeof librarySampleCards.value[number]): boolean {
  if (!selectedSampleCard.value || card.sample === selectedSample.value) return true
  return !!selectedSampleCard.value.materialElement && card.materialElement === selectedSampleCard.value.materialElement
}

function selectAnalysisSample(sample: string) {
  selectedSample.value = sample
  if (goldenSample.value === sample) goldenSample.value = ''
  if (goldenSample.value && !selectedAndGoldenSameMetal.value) goldenSample.value = ''
}

function selectGoldenSample(sample: string) {
  const card = librarySampleCards.value.find((item) => item.sample === sample)
  if (card && !isCompatibleGoldenCard(card)) {
    error.value = 'Golden 樣本必須與分析樣本是相同金屬'
    return
  }
  error.value = ''
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

const ipfDisplaySample = computed(() => displaySampleForTarget(ipfDisplayTarget.value))
const ipfDisplayVersionOption = computed(() => displayVersionForTarget(ipfDisplayTarget.value))
const ipfDisplayTitle = computed(() => displayTitleForTarget(ipfDisplayTarget.value))
const gridDisplaySample = computed(() => displaySampleForTarget(gridDisplayTarget.value))
const gridDisplayTitle = computed(() => displayTitleForTarget(gridDisplayTarget.value))

const ipfMapPairs = computed(() => resolveDisplayPairs(ipfDisplaySample.value, ipfDisplayVersionOption.value))
const ipfLegendPair = computed(() => ipfMapPairs.value[0])
const ipfLegendUrl = computed(() => ipfLegendPair.value?.id
  ? `/api/ebsd/pairs/${encodeURIComponent(ipfLegendPair.value.id)}/ipf_legend`
  : '/api/ebsd/ipf_legend')
const ipfLegendLabels = computed(() => {
  const snapshot = getDisplayedSampleSnapshot(ipfDisplaySample.value)
  return orientationFamilyFromSnapshot(snapshot) === 'hcp'
    ? { top: '[0001]', left: '[10-10]', right: '[11-20]' }
    : { top: '[111]', left: '[001]', right: '[101]' }
})

function orientationFamilyFromSnapshot(snapshot: Record<string, CppFeatures>): Exclude<FeatureFamily, 'all'> {
  return Object.values(snapshot).some((data) => data.orientation_family === 'hcp') ? 'hcp' : 'cubic'
}

function featureAxisLabel(feature: string): string {
  return feature.match(/^\[[^\]]+\]/)?.[0] ?? ''
}

function featureGroupKeyForFeature(feature: string): FeatureGroupKey {
  if (GRAIN_FEATURE_OPTIONS.has(feature)) return 'grain'
  const axis = featureAxisLabel(feature)
  const deviation = feature.includes('20%') ? '20' : feature.includes('15%') ? '15' : ''
  if (CUBIC_FEATURE_AXES.has(axis)) return deviation === '20' ? 'cubic20' : deviation === '15' ? 'cubic15' : 'other'
  if (HCP_FEATURE_AXES.has(axis)) return deviation === '20' ? 'hcp20' : deviation === '15' ? 'hcp15' : 'other'
  return 'other'
}

function displaySampleForTarget(target: SampleDisplayTarget): string {
  return target === 'golden' ? goldenSample.value : selectedSample.value
}

function displayVersionForTarget(target: SampleDisplayTarget): VersionOption | undefined {
  return target === 'golden' ? currentGoldenVersionOption.value : currentSelectedVersionOption.value
}

function displayTitleForTarget(target: SampleDisplayTarget): string {
  if (target === 'golden') return currentGoldenVersionLabel.value || goldenSample.value || 'Golden 樣本未選擇'
  return currentSelectedVersionLabel.value || selectedSample.value || '分析樣本未選擇'
}

function resolveDisplayPairs(sample: string, versionOpt: VersionOption | undefined): FilePair[] {
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
}

const formattedIpfReferenceVector = computed(() =>
  ipfReferenceVector.value.map((v) => formatSettingNumber(v)).join(', '),
)
const formattedIpfDenoindexThreshold = computed(() => formatSettingNumber(ipfDenoindexThreshold.value))
const formattedIpfDenoindexMisorientationThreshold = computed(() =>
  formatSettingNumber(ipfDenoindexMisorientationThreshold.value),
)

function detectIpfPreset(vector: [number, number, number]): IpfPreset | '' {
  const eps = 1e-9
  if (vector[0] > 0 && Math.abs(vector[1]) < eps && Math.abs(vector[2]) < eps) return 'x'
  if (Math.abs(vector[0]) < eps && vector[1] > 0 && Math.abs(vector[2]) < eps) return 'y'
  if (Math.abs(vector[0]) < eps && Math.abs(vector[1]) < eps && vector[2] > 0) return 'z'
  return ''
}

function detectIpfPresetFromDraft(): IpfPreset | '' {
  const normalized = normalizeIpfVectorDraft()
  return normalized ? detectIpfPreset(normalized) : ''
}

function openIpfSettings() {
  const [x, y, z] = ipfReferenceVector.value
  ipfVectorDraft.value = { x, y, z }
  ipfPresetDraft.value = detectIpfPreset(ipfReferenceVector.value)
  ipfDenoindexThresholdDraft.value = ipfDenoindexThreshold.value
  ipfDenoindexMisorientationThresholdDraft.value = ipfDenoindexMisorientationThreshold.value
  ipfSettingsOpen.value = true
}

function applyIpfPresetDraft() {
  if (!ipfPresetDraft.value) return
  if (ipfPresetDraft.value === 'x') ipfVectorDraft.value = { x: 1, y: 0, z: 0 }
  if (ipfPresetDraft.value === 'y') ipfVectorDraft.value = { x: 0, y: 1, z: 0 }
  if (ipfPresetDraft.value === 'z') ipfVectorDraft.value = { x: 0, y: 0, z: 1 }
}

function normalizeIpfVectorDraft(): [number, number, number] | null {
  const x = Number(ipfVectorDraft.value.x)
  const y = Number(ipfVectorDraft.value.y)
  const z = Number(ipfVectorDraft.value.z)
  const norm = Math.sqrt(x * x + y * y + z * z)
  if (!Number.isFinite(norm) || norm <= 1e-12) return null
  return [x / norm, y / norm, z / norm]
}

function applyIpfSettings() {
  const normalized = normalizeIpfVectorDraft()
  if (!normalized) {
    ElMessage.error('參考方向向量不能是零向量')
    return
  }
  const threshold = Number(ipfDenoindexThresholdDraft.value)
  if (!Number.isFinite(threshold) || threshold < 0 || threshold > 100) {
    ElMessage.error('De-noindex threshold 必須介於 0 到 100')
    return
  }
  const misorientationThreshold = Number(ipfDenoindexMisorientationThresholdDraft.value)
  if (!Number.isFinite(misorientationThreshold) || misorientationThreshold < 0 || misorientationThreshold > 180) {
    ElMessage.error('Misorientation threshold 必須介於 0 到 180')
    return
  }
  ipfReferenceVector.value = normalized
  ipfDenoindexThreshold.value = threshold
  ipfDenoindexMisorientationThreshold.value = misorientationThreshold
  ipfSettingsOpen.value = false
}

function toBackendDenoindexThreshold(value: number): number {
  return 100 - value
}

async function generateReport() {
  if (!canGenerate.value) return
  loading.value = true
  error.value = ''
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

watch(visibleFeatureOptionValues, (availableFeatures) => {
  const available = new Set(availableFeatures)
  const nextFeatures = selectedFeatures.value.filter((feat) => available.has(feat))
  if (nextFeatures.length !== selectedFeatures.value.length) {
    selectedFeatures.value = nextFeatures
    analysisRes.value = null
  }
}, { immediate: true })

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
    : `${formatSettingNumber(grainSettings.value.histBinWidthAbsolute)} µm`
  return `分箱起點 ${startLabel} / 間距 ${widthLabel}`
})

const grainBinPreviewText = computed(() => {
  const start = previewHistBinStart.value
  const width = previewHistBinWidth.value
  const bins = Math.max(1, Math.min(80, Math.ceil((sharedGrainBins.value.max - start) / Math.max(width, 1e-6))))
  return `預估分箱：${bins} 格，每格約 ${formatSettingNumber(width)} µm`
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
const reportPositionMatrix = computed(() =>
  buildPositionMatrix([
    ...selectedDisplayedPositions.value,
    ...goldenDisplayedPositions.value,
  ]),
)
const orientLineRows = computed(() =>
  reportPositionMatrix.value.rows.map((row) => ({
    rowKey: row,
    title: `列 ${row}`,
  })),
)

const comparablePositions = computed(() => {
  if (!goldenSample.value) return selectedDisplayedPositions.value
  const goldenSet = new Set(goldenDisplayedPositions.value)
  return selectedDisplayedPositions.value.filter((pos) => goldenSet.has(pos))
})
const missingGoldenPositions = computed(() => {
  if (!goldenSample.value) return []
  const goldenSet = new Set(goldenDisplayedPositions.value)
  return selectedDisplayedPositions.value.filter((pos) => !goldenSet.has(pos))
})
const missingSelectedPositions = computed(() => {
  if (!goldenSample.value) return []
  const selectedSet = new Set(selectedDisplayedPositions.value)
  return goldenDisplayedPositions.value.filter((pos) => !selectedSet.has(pos))
})
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
  return getDisplayedPosData(gridDisplaySample.value, pos)
}

function sortPositionKeys(a: string, b: string): number {
  return comparePositions(a, b)
}

function isMissingGoldenPos(pos: string): boolean {
  return missingGoldenPositions.value.includes(pos)
}

function isMissingSelectedPos(pos: string): boolean {
  return missingSelectedPositions.value.includes(pos) || !getDisplayedPosData(selectedSample.value, pos)
}

function positionStatusClass(pos: string): Record<string, boolean> {
  return {
    'pos-missing-golden': isMissingGoldenPos(pos),
    'pos-missing-selected': !isMissingGoldenPos(pos) && isMissingSelectedPos(pos),
  }
}

function gridPositionStatusClass(pos: string): Record<string, boolean> {
  return {
    'pos-missing-selected': !getGridPosData(pos),
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
  const dataMaximum = sharedGrainValues.value.length ? Math.max(...sharedGrainValues.value) : 10
  const max = roundUpToNearestTen(dataMaximum)
  const range = Math.max(max - min, 0.1)
  const rawWidth = grainSettings.value.histBinWidthMode === 'ratio'
    ? range * Math.max(grainSettings.value.histBinWidthRatio, 0.5) / 100
    : Math.max(grainSettings.value.histBinWidthAbsolute, 0.1)
  const binWidth = Math.max(rawWidth, 0.1)
  const binCount = Math.max(1, Math.min(80, Math.ceil(range / binWidth)))
  return { min, max, binCount, binWidth }
})

function grainHistogramSeries(values: number[]): number[] {
  const { min, binCount, binWidth } = sharedGrainBins.value
  const bins = Array.from({ length: binCount }, () => 0)
  let total = 0
  for (const raw of values) {
    if (!Number.isFinite(raw)) continue
    const index = Math.min(binCount - 1, Math.max(0, Math.floor((raw - min) / Math.max(binWidth, 1e-6))))
    const weight = grainChartMode.value === 'areaHist' ? Math.PI * (raw / 2) ** 2 : 1
    bins[index] += weight
    total += weight
  }
  if ((grainChartMode.value === 'areaHist' || histogramYMode.value === 'ratio') && total > 0) {
    return bins.map((value) => value / total * 100)
  }
  return bins
}

const sharedGrainYAxisMax = computed(() => {
  if (grainChartMode.value === 'cdf') return 100
  let maximum = 0
  for (const pos of reportPositionMatrix.value.positions) {
    for (const values of [getCompareSampleGrains(pos), getCompareGoldenGrains(pos)]) {
      maximum = Math.max(maximum, ...grainHistogramSeries(values))
    }
  }
  const rounded = roundUpToNearestTen(maximum)
  return grainChartMode.value === 'areaHist' || histogramYMode.value === 'ratio'
    ? Math.min(100, rounded)
    : rounded
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

function getOrientLineValues(sample: string, rowKey: number, dev: OrientDev, orientationIndex: number): number[] {
  const key = `orientation_ratio(${dev})` as keyof CppFeatures
  return reportPositionMatrix.value.columns.map((column) => {
    const arr = getDisplayedPosData(sample, positionKey(column, rowKey))?.[key] as number[] | undefined
    return Number(((arr?.[orientationIndex] ?? 0) * 100).toFixed(1))
  })
}

function orientLineX(index: number, width: number, pointCount: number): number {
  const plotWidth = width - ORIENT_LINE_LAYOUT.left - ORIENT_LINE_LAYOUT.right
  if (pointCount <= 1) return ORIENT_LINE_LAYOUT.left + plotWidth / 2
  const segments = pointCount - 1
  return ORIENT_LINE_LAYOUT.left + (plotWidth * index) / segments
}

function orientationLineScaleMax(dev: OrientDev): number {
  const manual = Number(orientationLineYMax.value)
  if (Number.isFinite(manual) && manual > 0) return manual
  const values = [selectedSample.value, goldenSample.value]
    .filter(Boolean)
    .flatMap((sample) => reportPositionMatrix.value.positions.flatMap((pos) => {
      const key = `orientation_ratio(${dev})` as keyof CppFeatures
      const ratios = getDisplayedPosData(sample, pos)?.[key] as number[] | undefined
      return (ratios ?? []).map((value) => value * 100)
    }))
    .filter((value) => Number.isFinite(value))
  const maximum = Math.max(0, ...values)
  return roundUpToNearestTen(maximum)
}

function orientationTriangleScaleMax(dev: OrientDev): number {
  const values = [selectedSample.value, goldenSample.value]
    .filter(Boolean)
    .flatMap((sample) => reportPositionMatrix.value.positions.flatMap((pos) => {
      const key = `orientation_ratio(${dev})` as keyof CppFeatures
      const ratios = getDisplayedPosData(sample, pos)?.[key] as number[] | undefined
      return (ratios ?? []).map((value) => value * 100)
    }))
    .filter((value) => Number.isFinite(value))
  const maximum = Math.max(0, ...values)
  return roundUpToNearestTen(maximum, 50)
}

function orientLineY(value: number, maxValue: number): number {
  const plotHeight = ORIENT_LINE_LAYOUT.height - ORIENT_LINE_LAYOUT.top - ORIENT_LINE_LAYOUT.bottom
  const clampedValue = Math.min(maxValue, Math.max(0, value))
  return ORIENT_LINE_LAYOUT.top + plotHeight * (1 - clampedValue / maxValue)
}

function buildOrientLineChart(rowKey: number, dev: OrientDev, orientationIndex: number) {
  const columns = reportPositionMatrix.value.columns
  const maxValue = orientationLineScaleMax(dev)
  const width = Math.max(320, columns.length * 76 + ORIENT_LINE_LAYOUT.left + ORIENT_LINE_LAYOUT.right)
  const plot = {
    left: ORIENT_LINE_LAYOUT.left,
    right: width - ORIENT_LINE_LAYOUT.right,
    top: ORIENT_LINE_LAYOUT.top,
    bottom: ORIENT_LINE_LAYOUT.height - ORIENT_LINE_LAYOUT.bottom,
  }
  return {
    width,
    viewBox: `0 0 ${width} ${ORIENT_LINE_LAYOUT.height}`,
    plot,
    xTicks: columns.map((column, index) => ({
      label: column,
      x: orientLineX(index, width, columns.length),
    })),
    yTicks: Array.from({ length: 6 }, (_, index) => maxValue * index / 5).map((value) => ({
      label: Number.isInteger(value) ? String(value) : value.toFixed(1),
      y: orientLineY(value, maxValue),
    })),
    series: [
      { source: 'analysis' as const, sample: selectedSample.value, color: '#409eff', dashed: false, labelOffset: -8 },
      { source: 'golden' as const, sample: goldenSample.value, color: '#e6a23c', dashed: true, labelOffset: 10 },
    ].filter((spec) => spec.sample).map((spec) => {
      const points = getOrientLineValues(spec.sample, rowKey, dev, orientationIndex).map((value, index) => {
        const y = orientLineY(value, maxValue)
        return {
          x: orientLineX(index, width, columns.length),
          y,
          labelY: Math.min(ORIENT_LINE_LAYOUT.height - 6, Math.max(ORIENT_LINE_LAYOUT.top + 11, y + spec.labelOffset)),
          label: `${value.toFixed(1)}%`,
        }
      })
      return {
        key: spec.source,
        source: spec.source,
        color: spec.color,
        dashed: spec.dashed,
        points,
        polyline: points.map((point) => `${point.x},${point.y}`).join(' '),
      }
    }),
  }
}

const orientLineCharts20 = computed(() =>
  orientLineRows.value.flatMap((row) =>
    orientLineSeriesInfo.value.map((info) => ({
      ...row,
      ...info,
      key: `${row.rowKey}-${info.orientationKey}`,
      title: `${row.title} · ${info.label}`,
      chart: buildOrientLineChart(row.rowKey, '20%', info.index),
    })),
  ),
)

const orientLineCharts15 = computed(() =>
  orientLineRows.value.flatMap((row) =>
    orientLineSeriesInfo.value.map((info) => ({
      ...row,
      ...info,
      key: `${row.rowKey}-${info.orientationKey}`,
      title: `${row.title} · ${info.label}`,
      chart: buildOrientLineChart(row.rowKey, '15%', info.index),
    })),
  ),
)

function isGridPosRetestedNext(pos: string): boolean {
  if (gridDisplayTarget.value !== 'analysis') return false
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

function stopPptElapsedTimer() {
  if (pptElapsedTimer === null) return
  clearInterval(pptElapsedTimer)
  pptElapsedTimer = null
}

function startPptElapsedTimer() {
  stopPptElapsedTimer()
  pptElapsedSeconds.value = 0
  const startedAt = Date.now()
  pptElapsedTimer = setInterval(() => {
    pptElapsedSeconds.value = Math.floor((Date.now() - startedAt) / 1000)
  }, 250)
}

onBeforeUnmount(() => {
  pptPollingCancelled = true
  stopPptElapsedTimer()
})

function waitForPptPoll(): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, 1000))
}

async function waitForPptJob(jobId: string): Promise<PptJob | null> {
  while (!pptPollingCancelled) {
    const job = await $fetch<PptJob>(`/api/reports/pptx/${jobId}`, { credentials: 'include' })
    if (job.status === 'completed') return job
    if (job.status === 'failed') throw new Error(job.error || 'PPT 輸出失敗')
    await waitForPptPoll()
  }
  return null
}

async function exportReportPpt() {
  if (!reportData.value || exportingPpt.value) return
  pptError.value = ''
  exportingPpt.value = true
  pptPollingCancelled = false
  startPptElapsedTimer()

  try {
    const job = await $fetch<PptJob>('/api/reports/pptx', {
      method: 'POST',
      credentials: 'include',
      body: {
        sample: selectedSample.value,
        golden: goldenSample.value,
        version_key: currentSelectedVersionOption.value?.key,
        min_grain_size: grainSettings.value.minGrainSize,
        ipf_reference_vector: ipfReferenceVector.value,
        ipf_denoindex_threshold: toBackendDenoindexThreshold(ipfDenoindexThreshold.value),
        ipf_denoindex_misorientation_threshold: ipfDenoindexMisorientationThreshold.value,
        hist_bin_start_mode: grainSettings.value.histBinStartMode,
        hist_bin_width_mode: grainSettings.value.histBinWidthMode,
        hist_bin_width_ratio: grainSettings.value.histBinWidthRatio,
        hist_bin_width_absolute: grainSettings.value.histBinWidthAbsolute,
        grain_chart_mode: grainChartMode.value,
        histogram_y_mode: histogramYMode.value,
        orientation_line_y_max: orientationLineYMax.value ?? null,
        show_microstructure_page: SHOW_MICROSTRUCTURE_PAGE,
        show_triangle_labels: showTriangleLabels.value,
        show_orientation_line_labels: showOrientLineLabels.value,
        ipf_display_target: ipfDisplayTarget.value,
        grid_display_target: gridDisplayTarget.value,
        selected_features: selectedFeatures.value,
        color_thresholds: colorThresholds.value,
      },
    })
    const completedJob = await waitForPptJob(job.id)
    if (!completedJob) return
    const blob = await $fetch<Blob>(completedJob.download_url || `/api/reports/pptx/${job.id}/download`, {
      responseType: 'blob',
      credentials: 'include',
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
    stopPptElapsedTimer()
    exportingPpt.value = false
  }
}

function analysisPositionMatrix(sampleData: Record<string, number>): PositionMatrix {
  return buildPositionMatrix(Object.keys(sampleData))
}

function formatAnalysisValue(value: number | undefined): string {
  if (!Number.isFinite(value)) return '—'
  return `${(Number(value) * 100).toFixed(1)}%`
}

function analysisCellStyle(value: number | undefined): Record<string, string> {
  if (!Number.isFinite(value)) return {}
  const valuePercent = Number(value) * 100
  return {
    backgroundColor: getColorForValue(valuePercent),
    color: Math.abs(valuePercent) > colorThresholds.value[1] ? '#ffffff' : '#111827',
  }
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

function buildOrientSeries(pos: string, dev: '20%' | '15%') {
  const ratioKey = `orientation_ratio(${dev})` as keyof CppFeatures
  const series: Array<{ name: string; color: string; dashed: boolean; values: [number, number, number] }> = []

  const sampleArr = getDisplayedPosData(selectedSample.value, pos)?.[ratioKey] as number[] | undefined
  if (sampleArr) {
    series.push({
      name: `分析 · ${currentSelectedVersionLabel.value || selectedSample.value}`,
      color: '#2563EB',
      dashed: false,
      values: [(sampleArr[0] ?? 0) * 100, (sampleArr[1] ?? 0) * 100, (sampleArr[2] ?? 0) * 100],
    })
  }

  const goldenArr = getDisplayedPosData(goldenSample.value, pos)?.[ratioKey] as number[] | undefined
  if (goldenArr) {
    series.push({
      name: `Golden · ${currentGoldenVersionLabel.value || goldenSample.value}`,
      color: '#F59E0B',
      dashed: true,
      values: [(goldenArr[0] ?? 0) * 100, (goldenArr[1] ?? 0) * 100, (goldenArr[2] ?? 0) * 100],
    })
  }
  return series
}

function dynamicCardGridStyle(columnCount: number): Record<string, string> {
  return {
    gridTemplateColumns: `repeat(${Math.max(1, columnCount)}, minmax(250px, 1fr))`,
  }
}

function dynamicTableGridStyle(columnCount: number): Record<string, string> {
  return {
    gridTemplateColumns: `64px repeat(${Math.max(1, columnCount)}, minmax(260px, 1fr))`,
  }
}

function dynamicAnalysisGridStyle(columnCount: number): Record<string, string> {
  return {
    gridTemplateColumns: `64px repeat(${Math.max(1, columnCount)}, minmax(120px, 1fr))`,
  }
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

#report-setup,
#grain-distribution,
#ipf-map,
#nine-grid-data,
#orientation-triangle-20,
#orientation-triangle-15,
#orientation-line-20,
#orientation-line-15,
#feature-analysis {
  scroll-margin-top: 80px;
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
  border: 1px solid #fecaca;
  border-radius: 8px;
  background: #fef2f2;
  display: grid;
  gap: .18rem;
  padding: .75rem;
}

.selected-card.golden {
  border-color: #bfdbfe;
  background: #eff6ff;
}

.selected-card span {
  font-size: .76rem;
  font-weight: 800;
}

.selected-card:not(.golden) span {
  color: #dc2626;
}

.selected-card.golden span {
  color: #2563eb;
}

.selected-card strong {
  overflow-wrap: anywhere;
}

.selected-card small {
  color: #64748b;
  font-weight: 700;
}

.selected-material {
  color: #475569;
  font-size: .78rem;
  font-style: normal;
  font-weight: 850;
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

.library-material-section {
  border: 1px solid var(--material-border, #9ca3af);
  border-radius: 10px;
  background: #f8fafc;
  overflow: hidden;
}

.library-material-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: .75rem;
  background: var(--material-bg, #e5e7eb);
  border-bottom: 1px solid var(--material-border, #9ca3af);
  color: var(--material-text, #374151);
  cursor: pointer;
  list-style: none;
  padding: .55rem .7rem;
}

.library-material-head::-webkit-details-marker {
  display: none;
}

.library-material-head::before {
  content: '+';
  flex: 0 0 auto;
  font-weight: 900;
  width: .8rem;
}

.library-material-section[open] > .library-material-head::before {
  content: '-';
}

.library-material-head strong {
  flex: 1;
  font-size: .9rem;
  overflow-wrap: anywhere;
}

.library-material-head small {
  color: var(--material-text, #374151);
  font-weight: 800;
  white-space: nowrap;
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
  margin: .55rem;
}

.library-target-card:hover {
  border-color: #cbd5e1;
  background: #f8fafc;
}

.library-target-card.selected {
  border-color: #fecaca;
  background: #fef2f2;
}

.library-target-card.golden {
  border-color: #bfdbfe;
  background: #eff6ff;
}

.library-target-card.incompatible {
  opacity: .45;
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
  border: 1px solid #fecaca;
  background: #fee2e2;
  color: #dc2626;
}

.role-pill.golden {
  border: 1px solid #bfdbfe;
  background: #dbeafe;
  color: #2563eb;
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
.export-btn {
  padding: .52rem 1.25rem;
  background: #ecf5ff;
  color: #409eff;
  border: 1px solid #b3d8ff;
  border-radius: 8px;
  font-size: .9rem;
  font-weight: 700;
  cursor: pointer;
  transition: background .15s;
}
.export-btn:hover:not(:disabled) {
  background: #d9ecff;
  border-color: #79bbff;
}
.export-btn:disabled {
  background: #ecf5ff;
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
  border: 1px solid #b3d8ff;
  border-radius: 999px;
  background: #ecf5ff;
  color: #409eff;
  cursor: pointer;
  font-size: .82rem;
  font-weight: 800;
  padding: .42rem .9rem;
  white-space: nowrap;
}
.grain-settings-btn:hover {
  border-color: #79bbff;
  background: #d9ecff;
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
  border: 1px solid #b3d8ff;
  background: #ecf5ff;
  color: #409eff;
}
.grain-dialog-cancel:hover {
  background: #f8fafc;
}
.grain-dialog-apply:hover {
  background: #d9ecff;
  border-color: #79bbff;
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
.ipf-section-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 1rem;
  margin-bottom: .75rem;
}
.ipf-section-head .section-title {
  margin-bottom: .2rem;
}
.ipf-settings-summary {
  color: #64748b;
  font-size: .78rem;
  font-weight: 600;
  line-height: 1.45;
}
.ipf-head-actions,
.grid-section-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: .75rem;
  flex-wrap: wrap;
}

.ipf-legend-popover {
  position: relative;
}

.ipf-legend-trigger {
  display: inline-flex;
  align-items: center;
  border: 1px solid #b3e19d;
  border-radius: 999px;
  background: #f0f9eb;
  color: #67c23a;
  cursor: pointer;
  font-size: .82rem;
  font-weight: 850;
  padding: .42rem .9rem;
  white-space: nowrap;
}

.ipf-legend-trigger:hover,
.ipf-legend-popover:focus-within .ipf-legend-trigger {
  border-color: #95d475;
  background: #e1f3d8;
}

.ipf-legend-card {
  position: absolute;
  right: 0;
  top: calc(100% + 8px);
  z-index: 20;
  width: 188px;
  border: 1px solid #dcdfe6;
  border-radius: 12px;
  background: #fff;
  box-shadow: 0 12px 28px rgba(15, 23, 42, 0.14);
  opacity: 0;
  padding: .75rem .7rem .7rem;
  pointer-events: none;
  transform: translateY(-4px);
  transition: opacity .15s, transform .15s;
}

.ipf-legend-popover:hover .ipf-legend-card,
.ipf-legend-popover:focus-within .ipf-legend-card {
  opacity: 1;
  pointer-events: auto;
  transform: translateY(0);
}

.ipf-legend-title {
  color: #374151;
  font-size: .84rem;
  font-weight: 850;
  letter-spacing: .01em;
  margin-bottom: .45rem;
  text-align: center;
}

.ipf-legend-figure {
  position: relative;
  width: 132px;
  height: 132px;
  margin: 0 auto .5rem;
}

.ipf-legend-img {
  display: block;
  width: 100%;
  height: 100%;
  object-fit: contain;
}

.ipf-legend-label {
  position: absolute;
  color: #111827;
  font-size: .66rem;
  font-weight: 900;
  letter-spacing: -.02em;
  line-height: 1;
  text-shadow: 0 1px 2px #fff, 0 -1px 2px #fff, 1px 0 2px #fff, -1px 0 2px #fff;
}

.label-111 {
  left: 50%;
  top: 5px;
  transform: translateX(-50%);
}

.label-001 {
  bottom: 8px;
  left: 2px;
}

.label-101 {
  bottom: 8px;
  right: 2px;
}

.ipf-legend-card small {
  color: #64748b;
  display: block;
  font-size: .72rem;
  font-weight: 650;
  letter-spacing: .01em;
  line-height: 1.45;
  text-align: center;
}

.grid-section-head {
  margin-bottom: 1rem;
}

.grid-section-head .section-title {
  margin-bottom: 0;
}

.sample-display-toggle {
  display: inline-flex;
  align-items: center;
  border: 1px solid #cbd5e1;
  border-radius: 999px;
  background: #f8fafc;
  padding: .18rem;
}

.sample-display-toggle button {
  border: 0;
  border-radius: 999px;
  background: transparent;
  color: #475569;
  cursor: pointer;
  font-size: .78rem;
  font-weight: 850;
  padding: .32rem .72rem;
  white-space: nowrap;
}

.sample-display-toggle button.active {
  border: 1px solid #b3d8ff;
  background: #ecf5ff;
  color: #409eff;
}

.sample-display-toggle button:nth-child(2).active {
  border: 1px solid #f3d19e;
  background: #fdf6ec;
  color: #e6a23c;
}
.ipf-settings-btn {
  display: inline-flex;
  align-items: center;
  border: 1px solid #b3d8ff;
  border-radius: 999px;
  background: #ecf5ff;
  color: #409eff;
  cursor: pointer;
  font-size: .82rem;
  font-weight: 800;
  padding: .42rem .9rem;
  white-space: nowrap;
}
.ipf-settings-btn:hover {
  border-color: #79bbff;
  background: #d9ecff;
}
.ipf-settings-panel {
  display: grid;
  gap: .9rem;
}
.ipf-setting-block {
  border: 1px solid #e5e7eb;
  border-radius: 14px;
  background: #f8fafc;
  padding: .95rem;
}
.ipf-setting-title {
  color: #0f172a;
  font-size: .92rem;
  font-weight: 850;
  margin-bottom: .55rem;
}
.ipf-setting-desc {
  color: #64748b;
  font-size: .8rem;
  line-height: 1.5;
  margin-top: .65rem;
}
.ipf-preset-row {
  display: flex;
  flex-wrap: wrap;
}
.ipf-vector-inputs {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: .75rem;
}
.ipf-threshold-input {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: .75rem;
}
.ipf-vector-inputs label,
.ipf-threshold-input label {
  display: grid;
  gap: .35rem;
}
.ipf-vector-inputs span,
.ipf-threshold-input span {
  color: #334155;
  font-size: .78rem;
  font-weight: 800;
}
.ipf-vector-inputs :deep(.el-input-number),
.ipf-threshold-input :deep(.el-input-number) {
  width: 100%;
}
.ipf-settings-footer {
  display: flex;
  justify-content: flex-end;
  gap: .6rem;
}
.ipf-dialog-cancel,
.ipf-dialog-apply {
  border-radius: 999px;
  cursor: pointer;
  font-size: .84rem;
  font-weight: 800;
  padding: .48rem 1rem;
}
.ipf-dialog-cancel {
  border: 1px solid #d1d5db;
  background: #fff;
  color: #475569;
}
.ipf-dialog-apply {
  border: 1px solid #b3d8ff;
  background: #ecf5ff;
  color: #409eff;
}
.ipf-dialog-cancel:hover {
  background: #f8fafc;
}
.ipf-dialog-apply:hover {
  background: #d9ecff;
  border-color: #79bbff;
}
.ipf-settings-dialog :deep(.el-dialog) {
  border-radius: 18px;
}
.ipf-settings-dialog :deep(.el-dialog__header) {
  border-bottom: 1px solid #e5e7eb;
  margin-right: 0;
  padding-bottom: .85rem;
}
.ipf-settings-dialog :deep(.el-dialog__title) {
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
  border-color: #b3d8ff;
  background: #ecf5ff;
  color: #409eff;
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
  border: 1px solid #b3d8ff;
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.94);
  box-shadow: 0 14px 30px rgba(15, 23, 42, 0.12);
  backdrop-filter: blur(10px);
  max-height: calc(100vh - 36px);
  overflow-x: hidden;
  overflow-y: auto;
}
.grain-mode-divider {
  align-self: stretch;
  border-left: 1px solid #d1d5db;
  margin: 0 .2rem;
}
.report-anchor-dock {
  position: fixed;
  right: 18px;
  bottom: 18px;
  z-index: 39;
  width: 184px;
  max-height: calc(100vh - 36px);
  overflow-x: hidden;
  overflow-y: auto;
  padding: .85rem .8rem .7rem;
  border: 1px solid #dcdfe6;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.94);
  box-shadow: 0 14px 30px rgba(15, 23, 42, 0.12);
  backdrop-filter: blur(10px);
}
.report-anchor-dock__title {
  margin: 0 .65rem .45rem;
  color: #409eff;
  font-size: .78rem;
  font-weight: 800;
  letter-spacing: .04em;
}
.report-anchor-dock :deep(.el-anchor) {
  background: transparent;
}
.report-anchor-dock :deep(.el-anchor__link) {
  overflow: hidden;
  color: #606266;
  font-size: .8rem;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.report-anchor-dock :deep(.el-anchor__link.is-active) {
  color: #409eff;
  font-weight: 700;
}
.report-version-dock__collapse {
  position: absolute;
  top: 10px;
  right: 10px;
  height: 26px;
  padding: 0 .65rem;
  border: 1px solid #93c5fd;
  border-radius: 999px;
  background: #409eff;
  color: #fff;
  font-size: .74rem;
  font-weight: 800;
  line-height: 24px;
  cursor: pointer;
  box-shadow: 0 8px 18px rgba(37, 99, 235, 0.18);
}
.report-version-dock__collapse:hover {
  background: #337ecc;
}
.report-version-dock-dot {
  position: fixed;
  left: 18px;
  bottom: 18px;
  z-index: 40;
  width: 18px;
  height: 18px;
  border: 1px solid #93c5fd;
  border-radius: 999px;
  background: #409eff;
  box-shadow: 0 8px 18px rgba(37, 99, 235, 0.22);
  cursor: pointer;
}
.report-version-dock * {
  box-sizing: border-box;
}
.report-version-dock__title {
  margin-bottom: .4rem;
  font-size: .82rem;
  font-weight: 800;
  letter-spacing: .04em;
  color: #409eff;
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
  background: #ecf5ff;
  margin: 10px 0 4px;
  height: 6px;
}

.report-version-dock :deep(.el-slider__bar) {
  background: #409eff;
}

.report-version-dock :deep(.el-slider__button) {
  border-color: #409eff;
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
.grain-dock-control-row {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  align-items: end;
  gap: .6rem;
  margin-top: .62rem;
}

.grain-dock-field {
  display: grid;
  gap: .35rem;
  margin-top: 0;
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
  border: 1px solid #b3d8ff;
  border-radius: 999px;
  background: #ecf5ff;
  color: #409eff;
  cursor: pointer;
  font-size: .82rem;
  font-weight: 850;
  min-height: 30px;
  padding: .34rem .85rem;
  white-space: nowrap;
}
.grain-dock-apply:hover {
  background: #d9ecff;
  border-color: #79bbff;
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
  gap: 12px;
  overflow-x: auto;
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
  gap: 8px;
  margin-bottom: 4px;
}
.ng-corner { min-width: 0; }
.ng-col-header {
  border: 1px solid #b3e19d;
  background: #f0f9eb;
  color: #67c23a;
  border-radius: 6px;
  padding: 8px 10px;
  text-align: center;
  font-weight: 700;
  font-size: .85rem;
  line-height: 1.4;
}
.ng-data-row {
  display: grid;
  gap: 8px;
  margin-bottom: 8px;
}
.ng-row-header {
  display: flex;
  align-items: center;
  justify-content: center;
  border: 1px solid #b3e19d;
  background: #f0f9eb;
  color: #67c23a;
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
  border-left: 5px solid #67c23a;
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
.ng-position-label {
  color: #1f2937;
  font-size: .78rem;
  font-weight: 850;
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
  background: #ecf5ff;
  border-color: #b3d8ff;
  color: #409eff;
}
.orient-y-max-control {
  display: flex;
  align-items: center;
  gap: .4rem;
  color: #374151;
  font-size: .8rem;
  font-weight: 700;
}
.orient-y-max-control input {
  width: 88px;
  border: 1px solid #d1d5db;
  border-radius: 7px;
  padding: .3rem .45rem;
  font: inherit;
}
.orient-y-max-control button {
  border: 1px solid #b3d8ff;
  border-radius: 999px;
  background: #ecf5ff;
  color: #409eff;
  cursor: pointer;
  padding: .28rem .65rem;
  font: inherit;
}
.triangle-row {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 12px;
  overflow-x: auto;
}
.triangle-panel {
  background: #f9fafb;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  padding: 12px 8px 8px;
}
.triangle-panel.pos-missing-golden {
  background: #fff7ed;
  border-color: #fdba74;
}
.triangle-panel.pos-missing-selected {
  background: #f3f4f6;
  border-color: #d1d5db;
  opacity: .82;
}
.orient-line-controls {
  margin-bottom: .85rem;
}
.orient-line-controls .orient-legend-row {
  align-items: flex-start;
  gap: .75rem;
}
.orient-line-legend-group {
  display: inline-flex;
  align-items: center;
  flex-wrap: wrap;
  gap: .42rem;
  border: 1px solid #e5e7eb;
  border-radius: 999px;
  background: #f8fafc;
  padding: .28rem .45rem;
}
.orient-line-legend-group.sample-style {
  border-color: #dbeafe;
  background: #fff;
}
.orient-line-legend-title {
  color: #64748b;
  font-size: .74rem;
  font-weight: 850;
  padding: 0 .2rem;
}
.orient-line-legend-item,
.orient-line-style-chip {
  display: inline-flex;
  align-items: center;
  gap: .25rem;
  color: #374151;
  font-size: .8rem;
  font-weight: 700;
  white-space: nowrap;
}
.orient-line-legend-item .legend-lbl {
  margin-right: 0;
}
.orient-line-style-chip {
  border-radius: 999px;
  padding: .18rem .52rem .18rem .28rem;
}
.orient-line-style-chip.analysis {
  border: 1px solid #b3d8ff;
  background: #ecf5ff;
  color: #409eff;
}
.orient-line-style-chip.golden {
  border: 1px solid #f3d19e;
  background: #fdf6ec;
  color: #e6a23c;
}
.orient-line-version {
  display: inline-flex;
  align-items: center;
  padding: .24rem .72rem;
  border-radius: 999px;
  border: 1px solid #b3d8ff;
  background: #ecf5ff;
  color: #409eff;
  font-size: .76rem;
  font-weight: 700;
}
.orient-line-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 12px;
}
.orient-line-panel {
  background: #f9fafb;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  overflow-x: auto;
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
.feature-selector-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: .75rem;
  margin-bottom: .7rem;
}
.feat-label { font-size: .88rem; font-weight: 600; color: #374151; }
.feature-family-chip {
  display: inline-flex;
  align-items: center;
  min-height: 28px;
  padding: .18rem .68rem;
  border-radius: 999px;
  border: 1px solid #c7d2fe;
  background: #eef2ff;
  color: #3730a3;
  font-size: .78rem;
  font-weight: 700;
}
.feature-group-list {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  gap: .8rem;
  margin-bottom: 1rem;
}
.feature-group {
  padding: .8rem .9rem .65rem;
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  background: #fbfdff;
}
.feature-group-title {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: .6rem;
  margin-bottom: .45rem;
  color: #1f2937;
  font-size: .86rem;
  font-weight: 700;
}
.feature-group-title span:last-child {
  color: #6b7280;
  font-size: .75rem;
}
.feat-checkbox-group {
  display: flex;
  flex-direction: column;
  gap: .45rem;
  min-width: 0;
}
.feat-checkbox-group :deep(.el-checkbox) {
  align-items: flex-start;
  height: auto;
  margin-right: 0;
  min-height: 28px;
  min-width: 0;
  max-width: 100%;
  white-space: normal;
}
.feat-checkbox-group :deep(.el-checkbox__label) {
  color: #374151;
  font-size: .95rem;
  line-height: 1.35;
  min-width: 0;
  overflow-wrap: anywhere;
  white-space: normal;
  word-break: break-word;
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
.analysis-sample-result {
  margin-bottom: 2rem;
}
.analysis-table-wrap {
  overflow-x: auto;
}
.analysis-table-row {
  display: grid;
  gap: 8px;
  margin-bottom: 8px;
  min-width: max-content;
}
.analysis-axis-cell {
  align-items: center;
  background: #f0f9eb;
  border: 1px solid #b3e19d;
  border-radius: 6px;
  color: #4f9f32;
  display: flex;
  font-size: .8rem;
  font-weight: 800;
  justify-content: center;
  min-height: 42px;
  min-width: 64px;
}
.analysis-value-cell {
  align-items: center;
  border: 1px solid rgba(15, 23, 42, .12);
  border-radius: 8px;
  display: grid;
  gap: .25rem;
  justify-items: center;
  min-height: 76px;
  min-width: 120px;
  padding: .65rem .75rem;
}
.analysis-value-cell strong {
  font-size: .76rem;
}
.analysis-value-cell span {
  font-size: 1rem;
  font-variant-numeric: tabular-nums;
  font-weight: 850;
}
.analysis-value-cell.empty {
  background: #f3f4f6;
  border-style: dashed;
  color: #9ca3af;
}

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
  .report-version-dock-dot {
    left: 12px;
    bottom: 12px;
  }
  .report-anchor-dock {
    right: 12px;
    bottom: 12px;
    width: 164px;
    max-height: min(42vh, 340px);
  }
  .report-anchor-dock--raised {
    bottom: calc(13rem + 12px);
  }
  .grain-card-head {
    align-items: stretch;
    flex-direction: column;
  }
  .grain-settings-btn {
    justify-content: center;
    width: 100%;
  }
  .ipf-section-head {
    align-items: stretch;
    flex-direction: column;
  }
  .ipf-settings-btn {
    justify-content: center;
    width: 100%;
  }
  .ipf-vector-inputs,
  .ipf-threshold-input {
    grid-template-columns: 1fr;
  }
  .ipf-settings-footer {
    align-items: stretch;
    flex-direction: column;
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
  .triangle-row { grid-template-columns: 1fr; }
  .orient-line-grid { grid-template-columns: 1fr; }
}

</style>
