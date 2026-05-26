import { parseProfileImportText } from './profileTransfer.js';

export function parseProfile(input) {
  return parseProfileImportText(input).profiles;
}
