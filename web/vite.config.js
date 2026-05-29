import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import tailwindcss from '@tailwindcss/vite';

const isGhPages = process.env.GITHUB_PAGES === '1';

// SPIFFS image size (mkspiffs) constrains object names to 32 bytes including
// the leading slash and trailing null. Files end up at /w/assets/<basename>,
// and `flash.sh` then gzips them into <basename>.gz, so the device path is
// /w/assets/<basename>.gz (10 chars of prefix + basename + 3 chars `.gz`).
//
// That leaves ~18 chars for the asset basename pre-gzip. Default Vite/Rollup
// names like `chartjs-plugin-annotation.esm-BY55cuoq.js` (~41 chars) blow past
// it, mkspiffs aborts the entire `assets/` dir with `SPIFFS_write error
// (-10010): SPIFFS_ERR_NAME_TOO_LONG`, and the on-device web UI 404s on every
// chunk. See CAR-281.
//
// Use short hash-only names so every chunk fits regardless of the source
// chunk's logical name.
const shortAssetName = 'assets/[hash:8].[ext]';
const shortChunkName = 'assets/[hash:8].js';

// https://vitejs.dev/config/
export default defineConfig({
  base: isGhPages ? '/gaggimate/' : '/',
  plugins: [preact(), tailwindcss()],

  build: {
    rollupOptions: {
      output: {
        inlineDynamicImports: true,
        entryFileNames: shortChunkName,
        chunkFileNames: shortChunkName,
        assetFileNames: shortAssetName,
      },
    },
  },

  server: {
    proxy: {
      '/api': {
        target: 'http://gaggimate.local/',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://gaggimate.local',
        ws: true,
      },
    },
    watch: {
      usePolling: true,
    },
  },

  test: {
    globals: true,
    environment: 'jsdom',
  },
});
