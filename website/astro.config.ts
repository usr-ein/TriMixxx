import { readdir, readFile, unlink } from "node:fs/promises";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

import mdx from "@astrojs/mdx";
import sitemap from "@astrojs/sitemap";
import type { AstroIntegration } from "astro";
import { defineConfig } from "astro/config";

// Astro copies the original of every content-collection picture into the
// build, even when pages only use its resized versions; a photo's 2560 px
// original is dead weight. After the build, delete every original image in
// _astro/ that no page, feed or stylesheet names.
const pruneOriginals: AstroIntegration = {
  name: "prune-unused-originals",
  hooks: {
    "astro:build:done": async ({ dir, logger }) => {
      const root = fileURLToPath(dir);
      const assets = join(root, "_astro");
      const files = await readdir(root, { recursive: true });
      const texts = await Promise.all(
        files
          .filter((file) => /\.(html|xml|css|js|json|txt|webmanifest)$/.test(file))
          .map((file) => readFile(join(root, file), "utf8")),
      );
      const corpus = texts.join("\n");
      const originals = (await readdir(assets)).filter((file) => /\.(jpe?g|png)$/i.test(file));
      const unused = originals.filter((file) => !corpus.includes(file));
      await Promise.all(unused.map((file) => unlink(join(assets, file))));
      logger.info(`removed ${String(unused.length)} unused original images`);
    },
  },
};

// https://docs.astro.build/en/reference/configuration-reference/
export default defineConfig({
  site: "https://trimixxx.e1n.sh",
  trailingSlash: "ignore",
  // Its bar would sit over the deck's frame.
  devToolbar: { enabled: false },
  integrations: [mdx(), sitemap(), pruneOriginals],
  // The first site's pages, sent to where their content went.
  redirects: {
    "/features": "/what-can-it-do/",
    "/gallery": "/pictures/",
    "/about": "/why/",
    "/hardware": "/pictures/boards/",
    "/architecture": "/blog/",
  },
  image: {
    layout: "constrained",
    responsiveStyles: true,
    breakpoints: [480, 800, 1200, 1800],
  },
  markdown: {
    shikiConfig: {
      theme: "vitesse-dark",
    },
  },
  vite: {
    // three.js's add-ons are imported only once someone opens a model; known
    // up front, the dev server doesn't stop to re-bundle them mid-visit.
    optimizeDeps: {
      include: [
        "three",
        "three/addons/controls/OrbitControls.js",
        "three/addons/environments/RoomEnvironment.js",
        "three/addons/libs/meshopt_decoder.module.js",
        "three/addons/loaders/GLTFLoader.js",
        "three/addons/utils/BufferGeometryUtils.js",
      ],
    },
  },
  // The client router swaps only what changes between two screens, so the
  // music, the knob and the frame carry on across them.
  prefetch: {
    prefetchAll: true,
    defaultStrategy: "hover",
  },
});
