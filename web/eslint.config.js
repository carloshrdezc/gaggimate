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
    // test-runner globals must be declared explicitly here (PRO-379).
    files: ['**/*.test.{js,jsx}', '**/*.spec.{js,jsx}'],
    languageOptions: {
      globals: {
        ...globals.vitest,
        describe: 'readonly',
        it: 'readonly',
        test: 'readonly',
        expect: 'readonly',
        beforeAll: 'readonly',
        afterAll: 'readonly',
        beforeEach: 'readonly',
        afterEach: 'readonly',
        vi: 'readonly',
        assert: 'readonly',
      },
    },
  },
];
