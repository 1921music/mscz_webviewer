const { defineConfig } = require('@playwright/test')
module.exports = defineConfig({
  testDir: './test/browser', timeout: 90000, workers: 1,
  use: { headless: true, launchOptions: { args: ['--autoplay-policy=no-user-gesture-required'] } },
  webServer: { command: 'mise exec -- node scripts/playback-test-server.cjs', url: 'http://127.0.0.1:8766', reuseExistingServer: false },
})
