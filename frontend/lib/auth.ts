export const AUTH_TOKEN_KEY = 'ebsd.jwt'

export function getAuthToken(): string | null {
  if (!import.meta.client) return null
  return window.localStorage.getItem(AUTH_TOKEN_KEY)
}

export function setAuthToken(token: string) {
  if (!import.meta.client) return
  window.localStorage.setItem(AUTH_TOKEN_KEY, token)
}

export function clearAuthToken() {
  if (!import.meta.client) return
  window.localStorage.removeItem(AUTH_TOKEN_KEY)
}

export function hasAuthToken(): boolean {
  return !!getAuthToken()
}

export function isPublicPath(path: string): boolean {
  return path === '/login'
}

export function isProtectedApiUrl(rawUrl: string): boolean {
  if (!import.meta.client) return false

  try {
    const url = new URL(rawUrl, window.location.origin)
    return (
      url.origin === window.location.origin &&
      (
        (url.pathname.startsWith('/api/') && url.pathname !== '/api/login') ||
        url.pathname.startsWith('/cppapi/')
      )
    )
  } catch {
    return false
  }
}
