import { defineConfig } from "vite";
import basicSsl from "@vitejs/plugin-basic-ssl";

// HTTPS is enabled so the page is a "secure context" on every device, which
// Web Bluetooth requires. Plain http://<lan-ip> pages cannot use Bluetooth.
// The cert is self-signed, so browsers show a one-time "not private" warning
// that you click through (Advanced -> Proceed).
export default defineConfig({
  // Relative base so the built app works when served from a GitHub Pages
  // sub-path (https://<user>.github.io/<repo>/) as well as from root.
  base: "./",
  plugins: [basicSsl()],
  server: {
    port: 5173,
    host: true,
  },
  preview: {
    port: 4173,
    host: true,
  },
});
