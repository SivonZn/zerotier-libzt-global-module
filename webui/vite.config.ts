import { defineConfig } from "vite";
import solid from "vite-plugin-solid";

export default defineConfig({
  base: "./",
  build: {
    // Keep generated frontend output in the module-local build tree. The
    // packaging step stages it as `webroot/` without leaving generated files
    // beside the source tree.
    outDir: "../webroot",
    emptyOutDir: true,
    target: "esnext"
  },
  plugins: [solid()]
});
