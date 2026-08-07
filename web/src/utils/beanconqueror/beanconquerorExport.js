// Beanconqueror-compatible backup serializer (PRO-632).
//
// Beanconqueror's importer reads a ZIP holding `Beanconqueror.json` and writes
// every top-level key straight into its storage (upstream `uiStorage.ts`), so a
// beans-and-shots-only payload is not enough: each brew stores foreign-key UUIDs
// for its bean, preparation method and mill, and those records must travel in
// the same archive. There is no versioned import schema — this is an internal
// storage dump — so the record shapes below come from a sanitized fixture pinned
// to upstream commit b713c3e (`beanconquerorTemplate.json`) rather than being
// hand-written. See `docs/beanconqueror-export.md`.

import { deriveStartTargetTemperature } from '../../pages/ShotHistory/startTargetTemperature.js';
import template from './beanconquerorTemplate.json' with { type: 'json' };
import { createStoreZip } from './storeZip.js';
import { uuidV5 } from './uuidV5.js';

/** Namespace for every deterministic id this module mints. Never change it: a
 * different namespace makes a re-export of the same library look like a whole
 * new set of records to Beanconqueror. */
const GAGGIMATE_UUID_NAMESPACE = '6f9f4a4e-5d0f-4a3e-9b1f-4a5a4f6b7c8d';

export const BEANCONQUEROR_ZIP_NAME = 'Beanconqueror.zip';
export const BEANCONQUEROR_MAIN_FILE = 'Beanconqueror.json';
/** Upstream `EXPORT_CHUNKING_CONFIG` chunk size for BREWS and BEANS (b713c3e). */
export const BEANCONQUEROR_CHUNK_SIZE = 500;

const CHUNKED_KEYS = [
  { property: 'BREWS', fileName: 'Brews' },
  { property: 'BEANS', fileName: 'Beans' },
];

const TOP_LEVEL_KEYS = ['BEANS', 'BREWS', 'MILL', 'PREPARATION', 'SETTINGS', 'VERSION'];

// Upstream ROASTS_ENUM (src/enums/beans/roasts.ts at b713c3e). Beanconqueror
// stores the enum KEY in `roast`, so this maps our free-text roast level onto a
// key, matching either the key itself or its human label.
const ROAST_ENUM = {
  UNKNOWN: 'Unknown',
  CINNAMON_ROAST: 'Cinnamon Roast',
  AMERICAN_ROAST: 'American Roast',
  NEW_ENGLAND_ROAST: 'New England Roast',
  HALF_CITY_ROAST: 'Half City Roast',
  MODERATE_LIGHT_ROAST: 'Moderate-Light Roast',
  CITY_ROAST: 'City roast',
  CITY_PLUS_ROAST: 'City+ Roast',
  FULL_CITY_ROAST: 'Full City Roast',
  FULL_CITY_PLUS_ROAST: 'Full City + Roast',
  ITALIAN_ROAST: 'Italian Roast',
  VIEANNA_ROAST: 'Vienna Roast',
  FRENCH_ROAST: 'French Roast',
  CUSTOM_ROAST: 'Custom',
};

// Upstream IBeanInformation (src/interfaces/bean/iBeanInformation.ts). Emitted
// so a bean's origin/process is not silently dropped.
const EMPTY_BEAN_INFORMATION = {
  country: '',
  region: '',
  farm: '',
  farmer: '',
  elevation: '',
  harvest_time: '',
  variety: '',
  processing: '',
  certification: '',
  percentage: 0,
  purchasing_price: 0,
  fob_price: 0,
};

const PLACEHOLDER_BEAN_NAME = 'GaggiMate (unknown bean)';
const PLACEHOLDER_MILL_NAME = 'GaggiMate (unknown grinder)';
const PREPARATION_NAME = 'GaggiMate';

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function text(value) {
  return String(value ?? '').trim();
}

/** Finite non-negative number, or `fallback`. Beanconqueror's numeric fields
 * have no "absent" representation, so an unparseable value must fall back to
 * the upstream default rather than emitting NaN (which JSON.stringify turns
 * into `null` and the importer would choke on). */
function numberOr(value, fallback = 0) {
  if (value === '' || value === null || value === undefined) return fallback;
  const parsed = Number(value);
  if (!Number.isFinite(parsed) || parsed < 0) return fallback;
  return parsed;
}

function nowSeconds() {
  return Math.floor(Date.now() / 1000);
}

/** Unix SECONDS. GaggiMate beans already store seconds (beanManager's
 * normalizeBeanTimestamp) and device shots store seconds in the binary index,
 * but a 0 sentinel (pre-NTP firmware) must not become a 1970 record. */
function timestampSeconds(value, fallback) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed) || parsed <= 0) return fallback;
  return Math.floor(parsed);
}

function normalizeKey(value) {
  return text(value).toLowerCase();
}

function beanUuid(beanId) {
  return uuidV5(`bean:${beanId}`, GAGGIMATE_UUID_NAMESPACE);
}

function millUuid(grinderName) {
  return uuidV5(`mill:${normalizeKey(grinderName)}`, GAGGIMATE_UUID_NAMESPACE);
}

function preparationUuid() {
  return uuidV5('preparation:espresso', GAGGIMATE_UUID_NAMESPACE);
}

/** Stable key for a shot. `source:id` is what the ShotHistory page itself uses
 * to identify a shot (`getHistoryKey`), so a device shot and an imported browser
 * shot that happen to share an id do not collide. */
function shotKey(shot) {
  const source = text(shot?.source) || 'gaggimate';
  return `${source}:${text(shot?.id)}`;
}

function brewUuid(shot) {
  return uuidV5(`brew:${shotKey(shot)}`, GAGGIMATE_UUID_NAMESPACE);
}

function resolveRoast(roastLevel) {
  const value = text(roastLevel);
  if (!value) return { roast: 'UNKNOWN', roast_custom: '' };

  const needle = value.toLowerCase();
  const match = Object.entries(ROAST_ENUM).find(
    ([key, label]) => key.toLowerCase() === needle || label.toLowerCase() === needle,
  );
  if (match) return { roast: match[0], roast_custom: '' };

  return { roast: 'CUSTOM_ROAST', roast_custom: value };
}

function toBeanRecord(bean, fallbackTimestamp) {
  const record = clone(template.beanTemplate);
  const { roast, roast_custom: roastCustom } = resolveRoast(bean.roastLevel);

  record.config = {
    uuid: beanUuid(bean.id),
    unix_timestamp: timestampSeconds(bean.createdAt, fallbackTimestamp),
  };
  record.name = text(bean.name);
  record.roaster = text(bean.roaster);
  record.roastingDate = text(bean.roastDate);
  record.note = text(bean.notes);
  record.roast = roast;
  record.roast_custom = roastCustom;
  // GaggiMate tracks REMAINING grams; Beanconqueror's `weight` is the bag mass.
  // Closest available mapping — documented as an approximation.
  record.weight = numberOr(bean.quantity, 0);
  // Beanconqueror's archive flag is `finished`.
  record.finished = !!bean.archived;

  const origin = text(bean.origin);
  const process = text(bean.process);
  record.bean_information =
    origin || process ? [{ ...EMPTY_BEAN_INFORMATION, country: origin, processing: process }] : [];

  return record;
}

function toMillRecord(name, uuid, timestamp) {
  const record = clone(template.millTemplate);
  record.config = { uuid, unix_timestamp: timestamp };
  record.name = name;
  return record;
}

function toPreparationRecord(timestamp) {
  const record = clone(template.preparationTemplate);
  record.config = { uuid: preparationUuid(), unix_timestamp: timestamp };
  record.name = PREPARATION_NAME;
  return record;
}

function toBrewRecord(shot, { beanUuidRef, millUuidRef, preparationUuidRef }, fallbackTimestamp) {
  const record = clone(template.brewTemplate);
  const notes = shot?.notes || {};

  record.config = {
    uuid: brewUuid(shot),
    unix_timestamp: timestampSeconds(shot.timestamp, fallbackTimestamp),
  };
  record.bean = beanUuidRef;
  record.mill = millUuidRef;
  record.method_of_preparation = preparationUuidRef;

  record.grind_weight = numberOr(notes.doseIn, 0);
  record.grind_size = text(notes.grindSetting);

  // Espresso yield goes in `brew_beverage_quantity` (the beverage in the cup);
  // `brew_quantity` is a distinct pre-brew liquid quantity we have no source for
  // and is deliberately left at its upstream default.
  const output = notes.doseOut === '' || notes.doseOut == null ? shot.volume : notes.doseOut;
  record.brew_beverage_quantity = numberOr(output, 0);
  record.brew_beverage_quantity_type = 'GR';

  // `shot.duration` is MILLISECONDS in the web model (HistoryCard divides it by
  // 1000 for display; the .slog header stores ms). Beanconqueror splits elapsed
  // time into whole seconds plus a millisecond remainder.
  const durationMs = Math.round(numberOr(shot.duration, 0));
  record.brew_time = Math.floor(durationMs / 1000);
  record.brew_time_milliseconds = durationMs % 1000;

  // The shot log itself is the only temperature source (PRO-631): the first
  // plausible `tt` sample is the target requested at brew start. Shots exported
  // without their sample trace loaded therefore carry the upstream default 0.
  record.brew_temperature = numberOr(deriveStartTargetTemperature(shot), 0);

  record.note = text(notes.notes);
  record.rating = numberOr(notes.rating ?? shot.rating, 0);
  // No GaggiMate UI writes TDS today, but the field is mapped so a future
  // refractometer note flows through without touching the serializer.
  record.tds = numberOr(notes.tds ?? shot.tds, 0);

  return record;
}

/**
 * Serializes a GaggiMate bean library and shot history into a Beanconqueror
 * backup object. Pure: no I/O, no device access, no randomness — the same input
 * always yields byte-identical output.
 *
 * Shots whose bean or grinder cannot be resolved get a deterministic placeholder
 * record, because a brew whose UUID reference dangles is not importable.
 *
 * @param {{ beans?: Array<object>, shots?: Array<object> }} data GaggiMate beans
 *   (beanManager shape) and shots (ShotHistory shape, with `notes`)
 * @returns {{BEANS: object[], BREWS: object[], MILL: object[], PREPARATION: object[], SETTINGS: object[], VERSION: object[]}}
 */
export function toBeanconquerorBackup(data = {}) {
  const beans = Array.isArray(data.beans) ? data.beans : [];
  const shots = Array.isArray(data.shots) ? data.shots : [];
  const fallbackTimestamp = nowSeconds();

  const beanRecords = new Map();
  const beanUuidById = new Map();
  const beanUuidByName = new Map();

  for (const bean of beans) {
    const id = text(bean?.id);
    if (!id) continue;
    const uuid = beanUuid(id);
    if (!beanRecords.has(uuid)) {
      beanRecords.set(uuid, toBeanRecord(bean, fallbackTimestamp));
    }
    beanUuidById.set(id, uuid);
    const name = normalizeKey(bean?.name);
    if (name && !beanUuidByName.has(name)) {
      beanUuidByName.set(name, uuid);
    }
  }

  const millRecords = new Map();
  const preparationRecords = [toPreparationRecord(fallbackTimestamp)];
  const preparationUuidRef = preparationRecords[0].config.uuid;

  function resolveBeanUuid(shot) {
    const notes = shot?.notes || {};
    const byId = text(notes.beanId) || text(shot?.beanId);
    if (byId && beanUuidById.has(byId)) return beanUuidById.get(byId);

    const byName = normalizeKey(notes.beanType || shot?.beanName);
    if (byName && beanUuidByName.has(byName)) return beanUuidByName.get(byName);

    // Unresolvable: the shot names a bean the library no longer holds (or names
    // none at all). Emit ONE shared placeholder so the reference resolves.
    const uuid = beanUuid('__gaggimate_unknown__');
    if (!beanRecords.has(uuid)) {
      beanRecords.set(
        uuid,
        toBeanRecord(
          { id: '__gaggimate_unknown__', name: PLACEHOLDER_BEAN_NAME },
          fallbackTimestamp,
        ),
      );
    }
    return uuid;
  }

  function resolveMillUuid(shot) {
    const name = text(shot?.notes?.grinder || shot?.grinder) || PLACEHOLDER_MILL_NAME;
    const uuid = millUuid(name);
    if (!millRecords.has(uuid)) {
      millRecords.set(uuid, toMillRecord(name, uuid, fallbackTimestamp));
    }
    return uuid;
  }

  const brewRecords = shots.map(shot =>
    toBrewRecord(
      shot,
      {
        beanUuidRef: resolveBeanUuid(shot),
        millUuidRef: resolveMillUuid(shot),
        preparationUuidRef,
      },
      fallbackTimestamp,
    ),
  );

  return {
    BEANS: [...beanRecords.values()],
    BREWS: brewRecords,
    MILL: [...millRecords.values()],
    PREPARATION: preparationRecords,
    SETTINGS: [clone(template.settings)],
    VERSION: [clone(template.version)],
  };
}

/**
 * Checks a backup against the interoperability contract: all six top-level keys
 * present, every record identified by a UUID, every brew reference resolving
 * inside the archive, and no attachment / flow-profile path (which would point
 * at a file the archive does not carry).
 *
 * @param {object} backup result of `toBeanconquerorBackup`
 * @returns {{ valid: boolean, errors: string[] }}
 */
export function validateBeanconquerorBackup(backup) {
  const errors = [];

  if (!backup || typeof backup !== 'object' || Array.isArray(backup)) {
    return { valid: false, errors: ['Backup must be a plain object.'] };
  }

  for (const key of TOP_LEVEL_KEYS) {
    if (!Array.isArray(backup[key])) {
      errors.push(`Missing or non-array top-level key: ${key}.`);
    }
  }
  if (errors.length) return { valid: false, errors };

  if (backup.SETTINGS.length !== 1) errors.push('SETTINGS must hold exactly one record.');
  if (backup.VERSION.length !== 1) errors.push('VERSION must hold exactly one record.');
  if (backup.BREWS.length > 0 && backup.PREPARATION.length === 0) {
    errors.push('BREWS present but PREPARATION is empty.');
  }

  const uuidPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
  const seen = new Set();
  for (const key of TOP_LEVEL_KEYS) {
    backup[key].forEach((record, index) => {
      const uuid = record?.config?.uuid;
      if (!uuid || !uuidPattern.test(uuid)) {
        errors.push(`${key}[${index}] has no valid config.uuid.`);
        return;
      }
      if (seen.has(uuid)) errors.push(`${key}[${index}] reuses uuid ${uuid}.`);
      seen.add(uuid);
      if (!Number.isInteger(record?.config?.unix_timestamp)) {
        errors.push(`${key}[${index}] has a non-integer config.unix_timestamp.`);
      }
    });
  }

  const uuidsOf = records => new Set(records.map(record => record?.config?.uuid));
  const beanUuids = uuidsOf(backup.BEANS);
  const millUuids = uuidsOf(backup.MILL);
  const preparationUuids = uuidsOf(backup.PREPARATION);

  backup.BREWS.forEach((brew, index) => {
    if (!beanUuids.has(brew?.bean)) {
      errors.push(`BREWS[${index}] references unknown bean ${brew?.bean}.`);
    }
    if (!millUuids.has(brew?.mill)) {
      errors.push(`BREWS[${index}] references unknown mill ${brew?.mill}.`);
    }
    if (!preparationUuids.has(brew?.method_of_preparation)) {
      errors.push(`BREWS[${index}] references unknown preparation ${brew?.method_of_preparation}.`);
    }
    if (brew?.brew_beverage_quantity_type !== 'GR') {
      errors.push(`BREWS[${index}] must use brew_beverage_quantity_type "GR".`);
    }
  });

  // Attachment / graph paths must never be emitted unless their files ship in
  // the archive too, which this exporter does not do.
  for (const key of ['BEANS', 'BREWS', 'MILL', 'PREPARATION']) {
    backup[key].forEach((record, index) => {
      if (Array.isArray(record?.attachments) && record.attachments.length > 0) {
        errors.push(`${key}[${index}] carries an attachment path but no file is exported.`);
      }
    });
  }
  backup.BREWS.forEach((brew, index) => {
    if (text(brew?.flow_profile)) {
      errors.push(`BREWS[${index}] carries a flow_profile path but no file is exported.`);
    }
    if (text(brew?.reference_flow_profile?.uuid)) {
      errors.push(`BREWS[${index}] carries a reference_flow_profile but no file is exported.`);
    }
  });

  return { valid: errors.length === 0, errors };
}

function chunkFileName(fileName, index) {
  return `Beanconqueror_${fileName}_${index}.json`;
}

/**
 * Splits a validated backup into `Beanconqueror.json` plus any chunk files and
 * packs them into `Beanconqueror.zip`. Chunk names and the 500-entry threshold
 * mirror upstream `EXPORT_CHUNKING_CONFIG`: chunk 0 stays in the main file, so
 * chunk files start at index 1 and appear only once a threshold is exceeded.
 *
 * @param {object} backup result of `toBeanconquerorBackup`
 * @returns {Promise<{ blob: Blob, filename: string, entries: Record<string,string> }>}
 */
export async function buildBeanconquerorZip(backup) {
  const validation = validateBeanconquerorBackup(backup);
  if (!validation.valid) {
    throw new Error(`Refusing to export an invalid Beanconqueror backup: ${validation.errors[0]}`);
  }

  const main = clone(backup);
  const chunks = [];

  for (const { property, fileName } of CHUNKED_KEYS) {
    const records = backup[property];
    if (!records.length) continue;
    main[property] = records.slice(0, BEANCONQUEROR_CHUNK_SIZE);
    for (
      let start = BEANCONQUEROR_CHUNK_SIZE, index = 1;
      start < records.length;
      start += BEANCONQUEROR_CHUNK_SIZE, index++
    ) {
      chunks.push({
        name: chunkFileName(fileName, index),
        content: JSON.stringify(records.slice(start, start + BEANCONQUEROR_CHUNK_SIZE)),
      });
    }
  }

  const entries = { [BEANCONQUEROR_MAIN_FILE]: JSON.stringify(main) };
  for (const chunk of chunks) {
    entries[chunk.name] = chunk.content;
  }

  return { blob: createStoreZip(entries), filename: BEANCONQUEROR_ZIP_NAME, entries };
}
