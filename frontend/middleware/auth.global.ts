import { hasAuthToken, isPublicPath } from '~/lib/auth'

export default defineNuxtRouteMiddleware((to) => {
  if (!import.meta.client) return

  if (isPublicPath(to.path)) {
    if (to.path === '/login' && hasAuthToken()) {
      return navigateTo('/')
    }
    return
  }

  if (!hasAuthToken()) {
    return navigateTo({
      path: '/login',
      query: {
        redirect: `${to.path}${to.fullPath.includes('?') ? to.fullPath.slice(to.path.length) : ''}`,
      },
    })
  }
})
