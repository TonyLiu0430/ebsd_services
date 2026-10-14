import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import test from 'node:test'

const componentUrl = new URL('../components/IpfMapGrid.vue', import.meta.url)

test('IPF map opens with the anchor default left-click action', async () => {
  const source = await readFile(componentUrl, 'utf8')

  assert.match(source, /title="左鍵點擊開啟完整 IPF map，新分頁可縮放"/)
  assert.doesNotMatch(source, /@click\.prevent/)
  assert.match(source, /target="_blank"/)
})
