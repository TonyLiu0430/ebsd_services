<template>
  <main class="flex min-h-screen bg-stone-50 px-4 py-10">
    <el-card class="m-auto w-full max-w-md" shadow="never">
      <template #header>
        <el-page-header title="回首頁" :content="mode === 'login' ? '登入' : '註冊'" @back="navigateTo('/')" />
      </template>

      <div v-if="ready && auth.user.value" class="flex flex-col items-center gap-3 py-3 text-center">
        <el-result icon="success" title="目前已登入" :sub-title="auth.user.value.username">
          <template #extra>
            <el-button type="primary" @click="navigateTo('/')">回分析頁</el-button>
          </template>
        </el-result>
      </div>

      <div v-else class="flex flex-col items-center py-3">
        <el-input
          v-model="username"
          style="width: 240px"
          maxlength="30"
          autocomplete="username"
          placeholder="使用者名稱"
          @blur="touched.username = true"
        />
        <el-text v-if="usernameError" class="mt-2 w-[240px] text-left" type="danger">
          {{ usernameError }}
        </el-text>
        <el-input
          v-model="password"
          style="width: 240px"
          class="mt-4"
          type="password"
          maxlength="100"
          :autocomplete="mode === 'login' ? 'current-password' : 'new-password'"
          placeholder="密碼"
          show-password
          @blur="touched.password = true"
        />
        <el-text v-if="passwordError" class="mt-2 w-[240px] text-left" type="danger">
          {{ passwordError }}
        </el-text>
        <el-text v-if="error" class="mt-3" type="danger">
          {{ error }}
        </el-text>
      </div>

      <template v-if="!auth.user.value" #footer>
        <div class="flex items-center justify-center gap-3">
          <el-button @click="mode = mode === 'login' ? 'register' : 'login'">
            {{ mode === 'login' ? '註冊' : '返回登入' }}
          </el-button>
          <el-button type="primary" :loading="pending" @click="submit">
            {{ mode === 'login' ? '登入' : '建立帳號' }}
          </el-button>
        </div>
      </template>
    </el-card>
  </main>
</template>

<script setup lang="ts">
type AuthMode = 'login' | 'register'
type CheckResponse = { user: { id: string, username: string } }

const auth = useAuth()
const mode = ref<AuthMode>('login')
const username = ref('')
const password = ref('')
const pending = ref(false)
const ready = ref(false)
const error = ref('')
const touched = reactive({
  username: false,
  password: false
})

watch(mode, () => {
  error.value = ''
  touched.username = false
  touched.password = false
})

async function loadSession() {
  await auth.refreshUser(true)
  ready.value = true
}

async function submit() {
  pending.value = true
  error.value = ''

  touched.username = true
  touched.password = true
  if (usernameError.value || passwordError.value) {
    pending.value = false
    return
  }

  try {
    const path = mode.value === 'login' ? '/goapi/user/login' : '/goapi/user/register'
    const res = await $fetch<CheckResponse>(path, {
      method: 'POST',
      credentials: 'include',
      body: {
        username: username.value,
        password: password.value
      }
    })

    auth.user.value = res.user
    auth.checked.value = true
    password.value = ''
    await navigateTo('/')
  } catch (err) {
    const apiError = err as { data?: { error?: string } }
    error.value = apiError.data?.error || '帳號或密碼錯誤'
  } finally {
    pending.value = false
  }
}

const usernameError = computed(() => {
  if (!touched.username) return ''
  if (!username.value) return '請輸入使用者名稱'
  if (/\s/.test(username.value)) return '帳號不能包含空白'
  if (username.value.length < 3) return '帳號至少需要 3 個字元'
  if (username.value.length > 30) return '帳號不能超過 30 個字元'
  if (!/^[a-zA-Z0-9_]+$/.test(username.value)) return '帳號只能使用英文、數字或底線'
  return ''
})

const passwordError = computed(() => {
  if (!touched.password) return ''
  if (!password.value) return '請輸入密碼'
  if (/\s/.test(password.value)) return '密碼不能包含空白'
  if (password.value.length < 8) return '密碼至少需要 8 個字元'
  if (password.value.length > 100) return '密碼不能超過 100 個字元'
  return ''
})

onMounted(loadSession)
</script>
