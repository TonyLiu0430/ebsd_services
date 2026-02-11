export default defineNuxtConfig({
  devtools: { enabled: false },
  ssr: false,
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
