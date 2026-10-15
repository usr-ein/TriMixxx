/** @type {import("stylelint").Config} */
export default {
  extends: ["stylelint-config-standard", "stylelint-config-html/astro"],
  reportDescriptionlessDisables: true,
  reportInvalidScopeDisables: true,
  reportNeedlessDisables: true,
  rules: {
    // Astro scopes component styles, so class names follow the markup's BEM-ish
    // blocks (`deck-card__label`), which kebab-case alone rejects.
    "selector-class-pattern": [
      "^[a-z][a-z0-9]*(?:-[a-z0-9]+)*(?:__[a-z0-9]+(?:-[a-z0-9]+)*)?(?:--[a-z0-9]+(?:-[a-z0-9]+)*)?$",
      { message: "Expected a kebab-case class, optionally block__element--modifier" },
    ],
    // :global() is how a scoped Astro style reaches rendered Markdown.
    "selector-pseudo-class-no-unknown": [true, { ignorePseudoClasses: ["global"] }],
  },
};
