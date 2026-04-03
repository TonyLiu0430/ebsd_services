<template>
  <div class="flex min-h-[100dvh] items-center justify-center bg-[#f5f7fa] px-4">
    <div
      class="w-full max-w-[560px] overflow-hidden rounded-[4px] border border-[#e4e7ed] bg-white shadow-[0_2px_12px_0_rgba(0,0,0,0.1)]"
    >
      <!-- header -->
      <div class="border-b border-[#ebeef5] px-6 py-5">
        <span class="text-[28px] font-bold text-[#303133]">登入</span>
      </div>

      <!-- body -->
      <div class="flex flex-col items-center px-6 py-10">
        <el-input
          v-model="username"
          style="width: 240px"
          placeholder="使用者名稱"
          @keyup.enter="login"
        />

        <el-input
          v-model="password"
          style="width: 240px"
          type="password"
          placeholder="密碼"
          show-password
          class="mt-4"
          @keyup.enter="login"
        />

        <el-text v-if="error" class="mt-3" type="danger">
          {{ error }}
        </el-text>
      </div>

      <!-- footer -->
      <div class="border-t border-[#ebeef5] px-6 py-4">
        <div class="flex items-center justify-center gap-3">
          <el-button @click="register">註冊</el-button>
          <el-button type="primary" @click="login">登入</el-button>
        </div>
      </div>
    </div>
  </div>
</template>

<script lang="ts" setup>
import { ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { ofetch } from 'ofetch'
import { ElMessage } from 'element-plus'
import { setAuthToken } from '~/lib/auth'

type LoginResponse = {
  token: string
  token_type: string
  expires_in: number
}

const router = useRouter()
const route = useRoute()

const username = ref('')
const password = ref('')
const error = ref('')

const login = async () => {
  error.value = ''

  try {
    const res = await ofetch<LoginResponse>('/api/login', {
      method: 'POST',
      body: {
        username: username.value,
        password: password.value,
      },
    })

    setAuthToken(res.token)

    ElMessage({
      message: '登入成功',
      type: 'success',
      plain: true,
    })

    await router.push(
      typeof route.query.redirect === 'string'
        ? route.query.redirect
        : '/',
    )
  } catch (e: unknown) {
    const err = e as {
      data?: { detail?: string; message?: string }
      message?: string
    }

    error.value =
      err.data?.detail ||
      err.data?.message ||
      err.message ||
      '帳號或密碼錯誤'
  }
}

const register = async () => {
  await router.push('/register')
}
</script>

<style scoped>
.flex {
  display: flex;
}

.min-h-\[100dvh\] {
  min-height: 100dvh;
}

.items-center {
  align-items: center;
}

.justify-center {
  justify-content: center;
}

.bg-\[\#f5f7fa\] {
  background: #f5f7fa;
}

.px-4 {
  padding-left: 1rem;
  padding-right: 1rem;
}

.w-full {
  width: 100%;
}

.max-w-\[560px\] {
  max-width: 560px;
}

.overflow-hidden {
  overflow: hidden;
}

.rounded-\[4px\] {
  border-radius: 4px;
}

.border {
  border-width: 1px;
  border-style: solid;
}

.border-\[\#e4e7ed\] {
  border-color: #e4e7ed;
}

.border-b {
  border-bottom-width: 1px;
  border-bottom-style: solid;
}

.border-t {
  border-top-width: 1px;
  border-top-style: solid;
}

.border-\[\#ebeef5\] {
  border-color: #ebeef5;
}

.bg-white {
  background: #fff;
}

.shadow-\[0_2px_12px_0_rgba\(0\,0\,0\,0\.1\)\] {
  box-shadow: 0 2px 12px 0 rgba(0, 0, 0, 0.1);
}

.px-6 {
  padding-left: 1.5rem;
  padding-right: 1.5rem;
}

.py-5 {
  padding-top: 1.25rem;
  padding-bottom: 1.25rem;
}

.py-10 {
  padding-top: 2.5rem;
  padding-bottom: 2.5rem;
}

.py-4 {
  padding-top: 1rem;
  padding-bottom: 1rem;
}

.flex-col {
  flex-direction: column;
}

.text-\[28px\] {
  font-size: 28px;
  line-height: 1.2;
}

.font-bold {
  font-weight: 700;
}

.text-\[\#303133\] {
  color: #303133;
}

.mt-4 {
  margin-top: 1rem;
}

.mt-3 {
  margin-top: 0.75rem;
}

.gap-3 {
  gap: 0.75rem;
}

:deep(.el-input__inner::selection) {
  background: #d9ecff;
  color: #303133;
}

:deep(input:-webkit-autofill),
:deep(input:-webkit-autofill:hover),
:deep(input:-webkit-autofill:focus),
:deep(input:-webkit-autofill:active) {
  -webkit-text-fill-color: #606266;
  -webkit-box-shadow: 0 0 0 1000px #fff inset;
  box-shadow: 0 0 0 1000px #fff inset;
  transition: background-color 9999s ease-out 0s;
}
</style>
