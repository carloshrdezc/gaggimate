import preact from 'eslint-config-preact';
import globals from 'globals';

// ESLint 9+ uses the flat config format. `eslint-config-preact@2` ships a flat
// config array (see its README), replacing the legacy `{ extends: "preact" }`
// block that lived in package.json under eslint-config-preact@1 / ESLint 8.
export default [
  {
    ignores: ['dist/**', 'node_modules/**', '.vercel/**', 'public/**'],
  },
  ...preact,
  {
    // Vitest test globals. eslint-config-preact@1 pulled in a Jest/Mocha env
    // that defined these; the v2 flat config only provides `expect`, so the
    // test-runner globals are supplied here via `globals.vitest`, which covers
    // describe/it/test/expect/beforeAll/afterAll/beforeEach/afterEach/vi/assert
    // and the rest of the Vitest surface (PRO-379).
    files: ['**/*.test.{js,jsx}', '**/*.spec.{js,jsx}'],
    languageOptions: {
      globals: {
        ...globals.vitest,
      },
    },
  },
];
