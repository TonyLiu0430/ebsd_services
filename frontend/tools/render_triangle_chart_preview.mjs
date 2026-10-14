#!/usr/bin/env node

import { readFile, writeFile, unlink } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { parse, compileScript } from '@vue/compiler-sfc'
import { transform } from 'esbuild'
import { createSSRApp, h } from 'vue'
import { renderToString } from '@vue/server-renderer'

const here = dirname(fileURLToPath(import.meta.url))
const componentPath = resolve(here, '../components/TriangleChart.vue')
const generatedPath = resolve(here, '.triangle-chart-preview.generated.mjs')
const outputPath = resolve(process.argv[2] ?? '../../.temp/triangle-chart-ui.svg')

const original = await readFile(componentPath, 'utf8')
const source = original.replace(
  '<script setup lang="ts">',
  '<script setup lang="ts">\nimport { computed } from \'vue\'',
)
const { descriptor, errors } = parse(source, { filename: componentPath })
if (errors.length) throw errors[0]

const compiled = compileScript(descriptor, {
  id: 'triangle-chart-preview',
  inlineTemplate: true,
})
const javascript = await transform(compiled.content, {
  loader: 'ts',
  format: 'esm',
  target: 'node20',
})

await writeFile(generatedPath, javascript.code, 'utf8')
try {
  const { default: TriangleChart } = await import(`${pathToFileURL(generatedPath).href}?t=${Date.now()}`)
  const app = createSSRApp({
    render: () => h(TriangleChart, {
      title: 'B-1',
      maxValue: 60,
      axisLabels: ['[001]', '[011]', '[111]'],
      showValueLabels: true,
      series: [
        { name: '分析・DATA10-2', color: '#2563eb', dashed: false, values: [8.5, 42.2, 18.1] },
        { name: 'Golden・DATA09-2', color: '#f59e0b', dashed: true, values: [15.1, 29.5, 30.5] },
      ],
    }),
  })
  const html = await renderToString(app)
  const match = html.match(/<svg\b[\s\S]*?<\/svg>/)
  if (!match) throw new Error('TriangleChart did not render an SVG')
  const svg = match[0].replace('<svg ', '<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="1080" ')
  await writeFile(outputPath, svg, 'utf8')
  process.stdout.write(`${outputPath}\n`)
} finally {
  await unlink(generatedPath).catch(() => {})
}
