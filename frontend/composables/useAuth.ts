type AuthUser = {
  id: string
  username: string
}

type CheckResponse = {
  user: AuthUser
}

export function useAuth() {
  const user = useState<AuthUser | null>('auth-user', () => null)
  const checked = useState<boolean>('auth-checked', () => false)

  async function refreshUser(force = false) {
    if (checked.value && !force) return user.value
    try {
      const res = await $fetch<CheckResponse>('/goapi/user/check', {
        credentials: 'include'
      })
      user.value = res.user
    } catch {
      user.value = null
    } finally {
      checked.value = true
    }
    return user.value
  }

  async function logout() {
    try {
      await $fetch('/goapi/user/logout', {
        method: 'POST',
        credentials: 'include'
      })
    } finally {
      user.value = null
      checked.value = true
    }
  }

  return {
    user,
    checked,
    isLoggedIn: computed(() => !!user.value),
    refreshUser,
    logout
  }
}
