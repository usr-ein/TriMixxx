// ===========================================================================
//  ESLint for the Mixxx controller scripts in this directory.
//
//  The rules and globals below are lifted from Mixxx's OWN eslint.config.cjs
//  (see the mixxx/ submodule) -- these files are Mixxx controller scripts, so
//  upstream's house style is the one worth matching. Deliberately NOT copied:
//  its typescript-eslint / jsdoc / diff plugins, which exist to police a
//  200-mapping tree and are dead weight for two files.
//
//  Prettier is deliberately absent. It would reformat the aligned `=` columns
//  this codebase uses throughout (TriMixxx.NOTE_ON, PAD_BASE, PADS ...) down to
//  single spaces, and it has no option to preserve them. ESLint's rule set is
//  the same choice upstream made for controller scripts, and it leaves the
//  alignment alone -- note there is no `no-multi-spaces` rule here, on purpose.
//
//    npm run lint     report everything
//    npm run format   fix LAYOUT only (whitespace/punctuation, no code changes)
//    npm run fix      fix everything auto-fixable, incl. var -> const/let
// ===========================================================================
import js from "@eslint/js";

export default [
    {
        ignores: ["node_modules/**", "ttymidi/**", "dj-usb/**", "TriMixxx_skin/**"],
    },
    js.configs.recommended,
    {
        files: ["**/*.js"],
        languageOptions: {
            // Mixxx runs these in QJSEngine: ES7, plain scripts (no modules).
            ecmaVersion: 7,
            sourceType: "script",
            globals: {
                // QJSEngine::ConsoleExtension
                console: "readonly",
                // Mixxx custom
                engine: "readonly",
                midi: "readonly",
                script: "readonly",
                controller: "readonly",
                components: "readonly",
                ColorMapper: "readonly",
                // common-controller-scripts globals
                print: "readonly",
                printObject: "readonly",
                stringifyObject: "readonly",
                arrayContains: "readonly",
                secondstominutes: "readonly",
                msecondstominutes: "readonly",
                colorCodeToObject: "readonly",
                colorCodeFromObject: "readonly",
                bpm: "readonly",
                ButtonState: "readonly",
                LedState: "readonly",
                Controller: "readonly",
                Button: "readonly",
                Control: "readonly",
                Deck: "readonly",
            },
        },
        rules: {
            // ---- vanilla rule config, mirrored from Mixxx's eslint.config.cjs ----
            "array-bracket-spacing": "warn",
            "block-spacing": "warn",
            "brace-style": ["warn", "1tbs", {allowSingleLine: true}],
            curly: "warn",
            camelcase: "warn",
            "comma-spacing": "warn",
            "computed-property-spacing": ["warn", "never", {enforceForClassMembers: true}],
            "dot-location": ["warn", "property"],
            "dot-notation": "warn",
            eqeqeq: ["error", "always"],
            "func-call-spacing": "warn",
            "func-style": ["error", "expression", {allowArrowFunctions: true}],
            indent: ["warn", 4],
            "key-spacing": "warn",
            "keyword-spacing": "warn",
            "linebreak-style": ["warn", "unix"],
            "newline-per-chained-call": "warn",
            "no-constructor-return": "warn",
            "no-extra-bind": "warn",
            "no-sequences": "warn",
            "no-useless-call": "warn",
            "no-useless-return": "warn",
            "no-trailing-spaces": "warn",
            "no-unneeded-ternary": ["warn", {defaultAssignment: false}],
            "no-var": "warn",
            "object-curly-newline": ["warn", {consistent: true, multiline: true}],
            "object-curly-spacing": "warn",
            "prefer-const": "warn",
            "prefer-regex-literals": "warn",
            "prefer-template": "warn",
            quotes: ["warn", "double"],
            "require-atomic-updates": "error",
            semi: "warn",
            "semi-spacing": "warn",
            "space-before-blocks": ["warn", "always"],
            "space-before-function-paren": ["warn", "never"],
            "space-in-parens": "warn",
            yoda: "warn",

            // args: "none" -- Mixxx calls every handler with a fixed
            // (channel, control, value, status, group) signature and spelling all
            // five out is the house convention even when only `value` is read, so
            // trailing unused args are structural, not sloppiness. Checking them
            // just trains you to ignore the linter. Unused LOCALS still error,
            // which is the part that catches real bugs (a typo'd property, a
            // variable left behind by an edit).
            "no-unused-vars": ["error", {args: "none", varsIgnorePattern: "^_"}],
        },
    },
];
