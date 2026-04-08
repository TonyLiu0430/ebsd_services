export default defineNuxtRouteMiddleware(async () => {
  const auth = useAuth()
  await auth.refreshUser()
  if (!auth.user.value) {
    return navigateTo('/login')
  }
})
