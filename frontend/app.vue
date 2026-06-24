<template>
  <div class="app-shell">
    <header class="app-header">
      <el-menu
        :default-active="activeIndex"
        class="el-menu-demo"
        mode="horizontal"
        :ellipsis="false"
        @select="handleSelect"
      >
        <el-menu-item index="brand" class="brand-item" title="回到檔案庫">
          <span class="brand-link">EBSD</span>
        </el-menu-item>

        <el-menu-item v-if="auth.isLoggedIn.value" index="/">檔案庫</el-menu-item>
        <el-menu-item v-if="auth.isLoggedIn.value" index="/report">自動化報表</el-menu-item>
        <el-menu-item v-if="!auth.isLoggedIn.value" index="/login">登入</el-menu-item>
        <el-menu-item v-if="auth.isLoggedIn.value" index="/logout">登出</el-menu-item>
      </el-menu>
    </header>

    <main class="app-main">
      <NuxtPage />
    </main>
  </div>
</template>

<script setup lang="ts">
const route = useRoute()
const router = useRouter()
const auth = useAuth()

const activeIndex = computed(() => {
  if (route.path === '/login') return '/login'
  if (route.path === '/report') return '/report'
  return '/'
})

onMounted(() => {
  auth.refreshUser()
})

async function handleSelect(key: string) {
  if (key === 'brand') {
    await router.push(auth.isLoggedIn.value ? '/' : '/login')
    return
  }

  if (key === '/logout') {
    await auth.logout()
    await router.push('/login')
    return
  }

  await router.push(key)
}
</script>

<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body,
button,
input,
select,
textarea {
  font-family: "Noto Sans TC", "Segoe UI", system-ui, sans-serif;
}

body { background: #fff; color: #222; min-height: 100vh; }

.app-shell {
  min-height: 100vh;
}

.app-header {
  border-bottom: 1px solid #e5e7eb;
  background: #fff;
}

.app-main {
  min-height: calc(100vh - 61px);
}

.el-menu--horizontal > .el-menu-item:nth-child(1) {
  margin-right: auto;
}

.brand-link {
  font-weight: 700;
  letter-spacing: 0.04em;
}
</style>
