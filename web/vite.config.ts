import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'path'

// Адрес устройства при разработке с живым железом.
// Задаётся через VITE_DEVICE=192.168.x.x npm run dev
const DEVICE = process.env.VITE_DEVICE ?? '192.168.4.1'

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: { '@': path.resolve(__dirname, 'src') },
  },
  server: {
    proxy: {
      // Все /api/* уходят на устройство (REST конфиги)
      '/api': { target: `http://${DEVICE}`, changeOrigin: true },
      // /ws — WebSocket телеметрии
      '/ws': { target: `ws://${DEVICE}`, ws: true, changeOrigin: true },
    },
  },
  build: {
    outDir: '../data/www',
    emptyOutDir: true,
    rollupOptions: {
      output: {
        // Разделяем vendor-бандл чтобы кэш браузера не инвалидировался при
        // каждой правке редактора.
        manualChunks: { vendor: ['react', 'react-dom', 'zustand'] },
      },
    },
  },
})
