import { fileURLToPath } from 'node:url'

export default defineNuxtConfig({
  compatibilityDate: '2026-03-03',
  telemetry: false,
  devtools: { enabled: false },
  ssr: false,
  modules: ['@element-plus/nuxt', '@nuxtjs/tailwindcss'],
  alias: {
    '@ebsd/charts': fileURLToPath(new URL('../shared/charts/src/index.js', import.meta.url))
  },
  app: {
    head: {
      title: 'EBSD Frontend'
    }
  },
  nitro: {
    routeRules: {
      '/api/**': { proxy: 'http://127.0.0.1:8000/**' }
    }
  }
})
