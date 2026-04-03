import { clearAuthToken, getAuthToken, isProtectedApiUrl } from '~/lib/auth'

declare global {
  interface Window {
    __ebsdAuthFetchPatched?: boolean
  }
}

export default defineNuxtPlugin(() => {
  if (!import.meta.client || window.__ebsdAuthFetchPatched) return

  const originalFetch = window.fetch.bind(window)

  window.fetch = async (input: RequestInfo | URL, init?: RequestInit) => {
    const rawUrl =
      typeof input === 'string'
        ? input
        : input instanceof URL
          ? input.toString()
          : input.url

    const protectedApi = isProtectedApiUrl(rawUrl)
    let nextInput = input
    let nextInit = init

    if (protectedApi) {
      const token = getAuthToken()
      if (token) {
        const headers = new Headers(input instanceof Request ? input.headers : init?.headers)
        headers.set('Authorization', `Bearer ${token}`)

        if (input instanceof Request) {
          nextInput = new Request(input, { headers })
        } else {
          nextInit = { ...init, headers }
        }
      }
    }

    const response = await originalFetch(nextInput, nextInit)

    if (protectedApi && response.status === 401 && window.location.pathname !== '/login') {
      clearAuthToken()
      const redirect = `${window.location.pathname}${window.location.search}`
      window.location.assign(`/login?redirect=${encodeURIComponent(redirect)}`)
    }

    return response
  }

  window.__ebsdAuthFetchPatched = true
})
