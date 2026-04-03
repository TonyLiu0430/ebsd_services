export default defineNuxtConfig({
  compatibilityDate: '2026-03-03',
  telemetry: false,
  devtools: { enabled: false },
  ssr: false,
  modules: ['@element-plus/nuxt'],
  app: {
    head: {
      title: 'EBSD Frontend'
    }
  },
  nitro: {
    routeRules: {
      '/api/**': { proxy: 'http://127.0.0.1:8000/**' },
      '/cppapi/**': { proxy: 'http://127.0.0.1:8080/**' }
    }
  }
})
