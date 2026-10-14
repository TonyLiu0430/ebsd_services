<script setup lang="ts">
import { renderGrainChartSvg } from '~/utils/grainChartRenderer.js'

const props = withDefaults(defineProps<{
  sample: number[]
  golden: number[]
  sampleLabel?: string
  goldenLabel?: string
  mode?: 'cdf' | 'hist' | 'areaHist'
  histYMode?: 'count' | 'ratio'
  fixedXMin?: number | null
  fixedXMax?: number | null
  binCount?: number
  binWidth?: number | null
  fixedYMax?: number | null
  autoBinCount?: boolean
  maxXTicks?: number
  canvasAspectRatio?: number | null
  fontScale?: number
  showXAxis?: boolean
  showYAxis?: boolean
  xAxisLabel?: string
  sampleColor?: string
  goldenColor?: string
  sampleDash?: string
  showAverage?: boolean
  averageLabel?: string
  averageSuffix?: string
}>(), {
  sample: () => [],
  golden: () => [],
  sampleLabel: 'Sample',
  goldenLabel: 'Golden',
  mode: 'cdf',
  histYMode: 'count',
  fixedXMin: null,
  fixedXMax: null,
  binCount: 20,
  binWidth: null,
  fixedYMax: null,
  autoBinCount: false,
  maxXTicks: 7,
  canvasAspectRatio: null,
  fontScale: 1,
  showXAxis: true,
  showYAxis: true,
  xAxisLabel: '粒徑 (µm)',
  sampleColor: '#DC2626',
  goldenColor: '#2563EB',
  sampleDash: '2.2,1.4',
  showAverage: false,
  averageLabel: 'Avg',
  averageSuffix: '',
})

const svgMarkup = computed(() => renderGrainChartSvg({
  sample: props.sample,
  golden: props.golden,
  mode: props.mode,
  histYMode: props.histYMode,
  fixedXMin: props.fixedXMin,
  fixedXMax: props.fixedXMax,
  binCount: props.binCount,
  binWidth: props.binWidth,
  fixedYMax: props.fixedYMax,
  autoBinCount: props.autoBinCount,
  maxXTicks: props.maxXTicks,
  canvasAspectRatio: props.canvasAspectRatio,
  fontScale: props.fontScale,
  showXAxis: props.showXAxis,
  showYAxis: props.showYAxis,
  xAxisLabel: props.xAxisLabel,
  sampleColor: props.sampleColor,
  goldenColor: props.goldenColor,
  sampleDash: props.sampleDash,
  showAverage: props.showAverage,
  averageLabel: props.averageLabel,
  averageSuffix: props.averageSuffix,
}))
</script>

<template>
  <div class="cdf-svg" v-html="svgMarkup"></div>
</template>

<style scoped>
.cdf-svg {
  width: 100%;
}

.cdf-svg :deep(svg) {
  width: 100%;
  height: auto;
  display: block;
}
</style>
