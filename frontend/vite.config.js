import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import basicSsl from '@vitejs/plugin-basic-ssl';

export default defineConfig({
  plugins: [react(), basicSsl()],
  optimizeDeps: {
    include: ['jsqr'],
  },
  server: {
    https: true,
    host: true,   // exposes on your local network (192.168.x.x)
    proxy: {
      '/api': {
        target: 'http://localhost:8000',
        changeOrigin: true,
      },
      '/phone-cam': {
        target: 'http://192.168.1.3:8080',
        changeOrigin: true,
        rewrite: path => path.replace(/^\/phone-cam/, ''),
      },
    },
  },
});
