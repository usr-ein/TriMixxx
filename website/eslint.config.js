// Linting for the site, run with --max-warnings 0 (`npm run lint`).
//
// TypeScript files get typescript-eslint's strictest type-checked rules.
// Astro components get the plugin's recommended and strict accessibility
// rules, and typescript-eslint's strict rules without type information; their
// types are checked by `astro check` instead. MDX posts are parsed (a syntax
// error fails here, not at build), run through remark-lint (.remarkrc.json),
// and their code blocks are linted.
import js from "@eslint/js";
import { defineConfig, globalIgnores } from "eslint/config";
import astro from "eslint-plugin-astro";
import * as mdx from "eslint-plugin-mdx";
import globals from "globals";
import tseslint from "typescript-eslint";

/** The rules of some typescript-eslint configs, without their parser settings. */
const rulesOf = (/** @type {import("eslint").Linter.Config[]} */ configs) =>
  Object.assign({}, ...configs.map((config) => config.rules ?? {}));

export default defineConfig(
  globalIgnores(["dist/", ".astro/", "media/", "node_modules/", "public/"]),
  {
    linterOptions: {
      reportUnusedDisableDirectives: "error",
      reportUnusedInlineConfigs: "error",
    },
  },
  {
    files: ["**/*.{js,mjs,ts,astro}"],
    extends: [js.configs.recommended],
    languageOptions: {
      globals: { ...globals.browser, ...globals.node },
    },
    rules: {
      eqeqeq: "error",
      "no-console": "error",
      "prefer-const": "error",
      "object-shorthand": "error",
    },
  },
  {
    files: ["**/*.ts"],
    extends: [tseslint.configs.strictTypeChecked, tseslint.configs.stylisticTypeChecked],
    languageOptions: {
      parserOptions: {
        projectService: true,
        tsconfigRootDir: import.meta.dirname,
      },
    },
  },
  ...astro.configs.recommended,
  ...astro.configs["jsx-a11y-strict"],
  {
    files: ["**/*.astro"],
    plugins: { "@typescript-eslint": tseslint.plugin },
    rules: {
      ...rulesOf(tseslint.configs.strict),
      ...rulesOf(tseslint.configs.stylistic),
      "astro/no-set-html-directive": "error",
      "astro/no-unused-css-selector": "error",
      "astro/prefer-class-list-directive": "error",
      "astro/prefer-object-class-list": "error",
      "astro/prefer-split-class-list": "error",
      "astro/no-exports-from-components": "error",
      // A region that scrolls sideways (a wide diagram or table on a phone)
      // must take keyboard focus to be scrolled without a mouse (axe:
      // scrollable-region-focusable), so a named role="region" may.
      "astro/jsx-a11y/no-noninteractive-tabindex": ["error", { roles: ["tabpanel", "region"] }],
    },
  },
  {
    files: ["**/*.mdx"],
    ...mdx.flat,
    processor: mdx.createRemarkProcessor({ lintCodeBlocks: true }),
    rules: {
      ...mdx.flat.rules,
      "mdx/remark": "error",
    },
  },
  mdx.flatCodeBlocks,
);
