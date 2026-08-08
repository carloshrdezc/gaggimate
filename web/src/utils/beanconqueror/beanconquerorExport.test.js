import { describe, test, expect } from 'vitest';

import template from './beanconquerorTemplate.json';
import {
  BEANCONQUEROR_CHUNK_SIZE,
  buildBeanconquerorZip,
  toBeanconquerorBackup,
  validateBeanconquerorBackup,
} from './beanconquerorExport.js';

const BEAN = {
  id: 'bean-house',
  name: 'Pink Bourbon',
  roaster: 'Onyx',
  roastLevel: 'Medium',
  roastDate: '2026-07-01',
  origin: 'Colombia',
  process: 'Washed',
  notes: 'stone fruit',
  quantity: 250,
  archived: false,
  createdAt: 1_754_000_000,
  updatedAt: 1_754_000_100,
};

const SHOT = {
  id: '42',
  source: 'gaggimate',
  profile: 'Espresso',
  profileId: 'profile-1',
  timestamp: 1_754_000_500,
  duration: 28_400, // milliseconds
  volume: 36.4,
  samples: [{ tt: 93.5 }],
  notes: {
    beanId: 'bean-house',
    doseIn: '18.2',
    doseOut: '36.4',
    grinder: 'Niche Zero',
    grindSetting: '18',
    notes: 'balanced',
    rating: 8.5,
  },
};

function backupOf(overrides = {}) {
  return toBeanconquerorBackup({ beans: [BEAN], shots: [SHOT], ...overrides });
}

describe('toBeanconquerorBackup — structure', () => {
  test('emits exactly the six top-level Beanconqueror storage keys as arrays', () => {
    const backup = backupOf();

    expect(Object.keys(backup).sort()).toEqual([
      'BEANS',
      'BREWS',
      'MILL',
      'PREPARATION',
      'SETTINGS',
      'VERSION',
    ]);
    for (const key of Object.keys(backup)) {
      expect(Array.isArray(backup[key])).toBe(true);
    }
  });

  test('every record carries a config uuid and a Unix SECONDS timestamp', () => {
    const backup = backupOf();
    const nowSeconds = Math.floor(Date.now() / 1000);

    for (const records of Object.values(backup)) {
      for (const record of records) {
        expect(record.config.uuid).toMatch(
          /^[0-9a-f]{8}-[0-9a-f]{4}-5[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/,
        );
        expect(Number.isInteger(record.config.unix_timestamp)).toBe(true);
        // Seconds, not milliseconds: a ms value would be ~1000x larger.
        expect(record.config.unix_timestamp).toBeLessThanOrEqual(nowSeconds + 1);
      }
    }
  });

  test('bean records keep the full pinned upstream field set', () => {
    const [bean] = backupOf().BEANS;
    expect(Object.keys(bean).sort()).toEqual(Object.keys(template.beanTemplate).sort());
  });

  test('brew records keep the full pinned upstream field set', () => {
    const [brew] = backupOf().BREWS;
    expect(Object.keys(brew).sort()).toEqual(Object.keys(template.brewTemplate).sort());
  });

  test('SETTINGS and VERSION come from the pinned upstream template', () => {
    const backup = backupOf();
    expect(backup.SETTINGS).toEqual([template.settings]);
    expect(backup.VERSION).toEqual([template.version]);
  });

  test('never emits attachment or flow/graph profile paths', () => {
    const backup = backupOf();

    for (const bean of backup.BEANS) expect(bean.attachments).toEqual([]);
    for (const mill of backup.MILL) expect(mill.attachments).toEqual([]);
    for (const prep of backup.PREPARATION) expect(prep.attachments).toEqual([]);
    for (const brew of backup.BREWS) {
      expect(brew.attachments).toEqual([]);
      expect(brew.flow_profile).toBe('');
      expect(brew.reference_flow_profile).toEqual({ type: 'NONE', uuid: '' });
    }
  });
});

describe('toBeanconquerorBackup — bean mapping', () => {
  test('maps the fields GaggiMate actually stores', () => {
    const [bean] = backupOf().BEANS;

    expect(bean.name).toBe('Pink Bourbon');
    expect(bean.roaster).toBe('Onyx');
    expect(bean.roastingDate).toBe('2026-07-01');
    expect(bean.note).toBe('stone fruit');
    // `weight` is the BAG TOTAL upstream, not the remaining quantity: this fixture
    // carries one 18.2 g shot, so 250 g remaining + 18.2 g accounted consumption.
    // Available weight is asserted directly in the PRO-632 inventory suite.
    expect(bean.weight).toBe(268.2);
    expect(bean.finished).toBe(false);
    expect(bean.bean_information).toEqual([
      expect.objectContaining({ country: 'Colombia', processing: 'Washed' }),
    ]);
  });

  test('leaves fields with no GaggiMate source at upstream defaults', () => {
    const [bean] = backupOf().BEANS;

    // No purchase date, cost, decaf flag or bean rating exists in our model.
    expect(bean.buyDate).toBe('');
    expect(bean.cost).toBe(0);
    expect(bean.decaffeinated).toBe(false);
    expect(bean.rating).toBe(0);
  });

  test('archived beans map to Beanconqueror finished', () => {
    const [bean] = toBeanconquerorBackup({
      beans: [{ ...BEAN, archived: true }],
      shots: [],
    }).BEANS;
    expect(bean.finished).toBe(true);
  });

  test('a recognised roast level maps onto the upstream roast enum', () => {
    const [bean] = toBeanconquerorBackup({
      beans: [{ ...BEAN, roastLevel: 'Full City Roast' }],
      shots: [],
    }).BEANS;
    expect(bean.roast).toBe('FULL_CITY_ROAST');
    expect(bean.roast_custom).toBe('');
  });

  test('an unrecognised roast level becomes CUSTOM_ROAST plus roast_custom', () => {
    const [bean] = toBeanconquerorBackup({
      beans: [{ ...BEAN, roastLevel: 'Scandinavian filter roast' }],
      shots: [],
    }).BEANS;
    expect(bean.roast).toBe('CUSTOM_ROAST');
    expect(bean.roast_custom).toBe('Scandinavian filter roast');
  });

  test('a blank roast level stays UNKNOWN', () => {
    const [bean] = toBeanconquerorBackup({ beans: [{ ...BEAN, roastLevel: '' }], shots: [] }).BEANS;
    expect(bean.roast).toBe('UNKNOWN');
    expect(bean.roast_custom).toBe('');
  });
});

describe('toBeanconquerorBackup — bean inventory (PRO-632 regression)', () => {
  // Beanconqueror stores ONLY a bag total (`bean.weight`) — there is no
  // `weight_used` field anywhere upstream (`grep -rn weight_used src/` at the
  // pinned commit b713c3e returns nothing). Consumption is DERIVED from the
  // brews that reference the bean, and available weight is the difference:
  //
  //   consumed  = SUM(brew.bean_weight_in > 0 ? brew.bean_weight_in : brew.grind_weight)
  //   available = bean.weight - consumed
  //
  // Sources at b713c3e: `getUsedWeightCount()` in
  // src/components/bean-information/bean-information.component.ts:228-241
  // (duplicated in dashboard.page.ts:251-263, bean-sort-filter-helper.service.ts:391-402,
  // uiBrewHelper.ts:183-197), rendered by bean-information.component.html:237 as
  // `{{gramUsed}}g of {{gramTotal}}g ({{leftOver}}g)` with
  // `leftOver: bean.weight - uiUsedWeightCount`.
  //
  // This helper is a literal transcription of that upstream derivation, so these
  // tests assert what the app will actually display.
  function upstreamConsumed(backup, beanRecord) {
    return backup.BREWS.filter(brew => brew.bean === beanRecord.config.uuid).reduce(
      (sum, brew) => sum + (brew.bean_weight_in > 0 ? brew.bean_weight_in : brew.grind_weight),
      0,
    );
  }

  function upstreamAvailable(backup, beanRecord) {
    return beanRecord.weight - upstreamConsumed(backup, beanRecord);
  }

  test("a bean's available weight equals GaggiMate's remaining quantity", () => {
    // GaggiMate's `quantity` is REMAINING grams (beanManager.syncBeanUsageFromNotes
    // decrements it by each shot's doseIn), so it must land on the AVAILABLE side
    // of the upstream relation, not in the bag-total slot.
    const backup = toBeanconquerorBackup({
      beans: [BEAN],
      shots: [SHOT, { ...SHOT, id: '43' }],
    });
    const [bean] = backup.BEANS;

    expect(upstreamConsumed(backup, bean)).toBeCloseTo(36.4, 2); // 2 shots x 18.2 g
    expect(upstreamAvailable(backup, bean)).toBeCloseTo(BEAN.quantity, 2);
  });

  test('a beans-only export reports the remaining quantity as available and zero consumed', () => {
    const backup = toBeanconquerorBackup({ beans: [BEAN], shots: [] });
    const [bean] = backup.BEANS;

    expect(upstreamConsumed(backup, bean)).toBe(0);
    expect(upstreamAvailable(backup, bean)).toBe(250);
    expect(bean.weight).toBe(250);
  });

  test('no bean in the archive ever reports negative available weight', () => {
    const backup = toBeanconquerorBackup({
      beans: [{ ...BEAN, quantity: 0 }],
      shots: [SHOT, { ...SHOT, id: '43' }, { ...SHOT, id: '44' }],
    });

    for (const bean of backup.BEANS) {
      expect(upstreamAvailable(backup, bean)).toBeGreaterThanOrEqual(0);
    }
  });

  test('the placeholder bean absorbs its brews instead of going negative', () => {
    // The shared placeholder carries no GaggiMate quantity at all, yet its brews
    // still carry real doses — the second independent negative-inventory site.
    const backup = toBeanconquerorBackup({
      beans: [],
      shots: [{ ...SHOT, notes: { ...SHOT.notes, beanId: 'ghost' } }],
    });
    const [placeholder] = backup.BEANS;

    expect(placeholder.name).toBe('GaggiMate (unknown bean)');
    expect(upstreamConsumed(backup, placeholder)).toBeCloseTo(18.2, 2);
    expect(upstreamAvailable(backup, placeholder)).toBe(0);
  });

  test('consumption is attributed per bean, not pooled across the library', () => {
    const other = { ...BEAN, id: 'bean-other', name: 'Other', quantity: 100 };
    const backup = toBeanconquerorBackup({
      beans: [BEAN, other],
      shots: [
        SHOT,
        { ...SHOT, id: '43', notes: { ...SHOT.notes, beanId: 'bean-other', doseIn: '20' } },
      ],
    });

    const byName = Object.fromEntries(backup.BEANS.map(bean => [bean.name, bean]));
    expect(upstreamConsumed(backup, byName['Pink Bourbon'])).toBeCloseTo(18.2, 2);
    expect(upstreamConsumed(backup, byName.Other)).toBeCloseTo(20, 2);
    expect(upstreamAvailable(backup, byName['Pink Bourbon'])).toBeCloseTo(250, 2);
    expect(upstreamAvailable(backup, byName.Other)).toBeCloseTo(100, 2);
  });

  test('an archived bean with nothing left reports zero available, not negative', () => {
    const backup = toBeanconquerorBackup({
      beans: [{ ...BEAN, quantity: 0, archived: true }],
      shots: [SHOT],
    });
    const [bean] = backup.BEANS;

    expect(bean.finished).toBe(true);
    expect(upstreamAvailable(backup, bean)).toBe(0);
  });

  test('float dust in the dose sum cannot push available weight below zero', () => {
    // 1 + 1.03 sums to 2.0300000000000002 in binary float but rounds to 2.03, so
    // a naively-rounded bag total would sit BELOW the consumption Beanconqueror
    // recomputes, yielding a tiny negative leftOver.
    const backup = toBeanconquerorBackup({
      beans: [{ ...BEAN, quantity: 0 }],
      shots: [
        { ...SHOT, id: 'd1', notes: { ...SHOT.notes, doseIn: '1' } },
        { ...SHOT, id: 'd2', notes: { ...SHOT.notes, doseIn: '1.03' } },
      ],
    });
    const [bean] = backup.BEANS;

    expect(upstreamAvailable(backup, bean)).toBeGreaterThanOrEqual(0);
  });

  // `numberOr` accepts any finite non-negative dose and GaggiMate's dose field is
  // free text, so a 3-decimal dose (1.005 g — plausible on a 0.001 g scale) is a
  // valid input. Quantizing `quantity + consumed` to 2dp desynchronises the two
  // halves of the upstream relation, because Beanconqueror recomputes `consumed`
  // from the UNROUNDED brew doses: a bag total rounded UP makes the difference read
  // HIGHER than GaggiMate's remaining quantity (phantom coffee), and one rounded
  // DOWN makes it read negative. The bag total must keep the precision it was
  // built from so `weight - consumed` cancels back to the remaining quantity.

  test('a 3-decimal dose keeps available weight at a zero remaining quantity', () => {
    const backup = toBeanconquerorBackup({
      beans: [{ ...BEAN, quantity: 0 }],
      shots: [{ ...SHOT, id: 'p1', notes: { ...SHOT.notes, doseIn: '1.005' } }],
    });
    const [bean] = backup.BEANS;

    expect(upstreamConsumed(backup, bean)).toBe(1.005);
    // A 2dp bag total would be 1, i.e. BELOW the 1.005 the app recomputes.
    expect(bean.weight).toBe(1.005);
    expect(upstreamAvailable(backup, bean)).toBe(0);
  });

  test('a 3-decimal dose keeps available weight at a nonzero remaining quantity', () => {
    const backup = toBeanconquerorBackup({
      beans: [{ ...BEAN, quantity: 10 }],
      shots: [{ ...SHOT, id: 'p2', notes: { ...SHOT.notes, doseIn: '1.005' } }],
    });
    const [bean] = backup.BEANS;

    expect(upstreamConsumed(backup, bean)).toBe(1.005);
    // 2dp rounding turns 11.004999… into 11.01, and 11.01 - 1.005 reads
    // 10.004999999999999: 5 mg of coffee the bag does not hold.
    expect(bean.weight).not.toBe(11.01);
    expect(upstreamAvailable(backup, bean)).toBe(10);
  });

  test('a 3-decimal dose never inflates available weight across remaining quantities', () => {
    // Generalisation of the two cases above: float addition is correctly rounded,
    // so `(quantity + consumed) - consumed` stays within a hair of the remaining
    // quantity and never below zero, for every quantity scale.
    for (const quantity of [0, 0.5, 10, 18.2, 250]) {
      const backup = toBeanconquerorBackup({
        beans: [{ ...BEAN, quantity }],
        shots: [
          { ...SHOT, id: 'q1', notes: { ...SHOT.notes, doseIn: '1.005' } },
          { ...SHOT, id: 'q2', notes: { ...SHOT.notes, doseIn: '18.234' } },
        ],
      });
      const [bean] = backup.BEANS;
      const available = upstreamAvailable(backup, bean);

      expect(available).toBeGreaterThanOrEqual(0);
      // Deliberately tighter than 2dp: a 2dp-rounded total is off by up to 5e-3.
      expect(available).toBeCloseTo(quantity, 9);
    }
  });
});

describe('non-finite derived numbers (PRO-632 regression)', () => {
  // `numberOr` clears every value it touches as finite and non-negative, but the
  // bag total is DERIVED after that gate (`quantity + consumed`), so two
  // individually-valid operands can still sum past Number.MAX_VALUE. Infinity is
  // not a Beanconqueror numeric field: JSON.stringify writes it as `null`, and
  // the importer would read a bean with no weight at all.
  //
  // No finite bag total satisfies `available = weight - consumed` for such
  // operands (MAX_VALUE - 1e308 is not 1e308), so clamping would silently invent
  // or destroy coffee and break the documented available=quantity invariant.
  // The archive is therefore unrepresentable and must be refused.
  const HUGE = 1e308;

  function overflowingBackup() {
    return toBeanconquerorBackup({
      beans: [{ ...BEAN, quantity: HUGE }],
      shots: [{ ...SHOT, id: 'overflow', notes: { ...SHOT.notes, doseIn: String(HUGE) } }],
    });
  }

  test('validation rejects a bean whose bag total overflowed to Infinity', () => {
    const result = validateBeanconquerorBackup(overflowingBackup());

    expect(result.valid).toBe(false);
    expect(result.errors.join('\n')).toMatch(/BEANS\[0\]\.weight.*finite/i);
  });

  test('the export path refuses instead of writing a JSON null bean weight', async () => {
    await expect(buildBeanconquerorZip(overflowingBackup())).rejects.toThrow(/finite/i);
  });

  test('a representable huge quantity still exports a finite bean weight', async () => {
    // Guard against over-rejecting: 1e308 + an 18.2 g dose is still finite, so
    // this archive must keep exporting.
    const backup = toBeanconquerorBackup({ beans: [{ ...BEAN, quantity: HUGE }], shots: [SHOT] });
    expect(validateBeanconquerorBackup(backup)).toEqual({ valid: true, errors: [] });

    const { entries } = await buildBeanconquerorZip(backup);
    const [bean] = JSON.parse(entries['Beanconqueror.json']).BEANS;

    expect(bean.weight).toBe(HUGE);
    expect(Number.isFinite(bean.weight)).toBe(true);
  });

  test('no exported record may carry a non-finite number in any numeric field', () => {
    // Class-level guard: any derived numeric field, not just the bag total.
    const backup = backupOf();
    backup.BREWS[0].grind_weight = Number.NaN;

    const result = validateBeanconquerorBackup(backup);
    expect(result.valid).toBe(false);
    expect(result.errors.join('\n')).toMatch(/BREWS\[0\]\.grind_weight.*finite/i);
  });

  test('a normal export serializes no non-finite number and no null weight', async () => {
    const { entries } = await buildBeanconquerorZip(backupOf());

    for (const body of Object.values(entries)) {
      expect(body).not.toMatch(/\bInfinity\b|\bNaN\b/);
    }
    const [bean] = JSON.parse(entries['Beanconqueror.json']).BEANS;
    expect(bean.weight).not.toBeNull();
    expect(Number.isFinite(bean.weight)).toBe(true);
  });
});

describe('toBeanconquerorBackup — shot mapping', () => {
  test('maps espresso output to brew_beverage_quantity with type GR', () => {
    const [brew] = backupOf().BREWS;

    expect(brew.brew_beverage_quantity).toBe(36.4);
    expect(brew.brew_beverage_quantity_type).toBe('GR');
    // brew_quantity is a distinct concept upstream; we have nothing for it.
    expect(brew.brew_quantity).toBe(0);
  });

  test('maps dose to grind_weight and grinder setting to grind_size', () => {
    const [brew] = backupOf().BREWS;

    expect(brew.grind_weight).toBe(18.2);
    expect(brew.grind_size).toBe('18');
  });

  test('splits the millisecond duration into brew_time seconds plus remainder', () => {
    const [brew] = backupOf().BREWS;

    expect(brew.brew_time).toBe(28);
    expect(brew.brew_time_milliseconds).toBe(400);
  });

  test('derives brew_temperature from the shot log start target', () => {
    const [brew] = backupOf().BREWS;
    expect(brew.brew_temperature).toBe(93.5);
  });

  test('leaves brew_temperature at 0 when the shot carries no usable samples', () => {
    const [brew] = toBeanconquerorBackup({
      beans: [BEAN],
      shots: [{ ...SHOT, samples: [] }],
    }).BREWS;
    expect(brew.brew_temperature).toBe(0);
  });

  test('maps note, rating and tds', () => {
    const [brew] = toBeanconquerorBackup({
      beans: [BEAN],
      shots: [{ ...SHOT, notes: { ...SHOT.notes, tds: 9.2 } }],
    }).BREWS;

    expect(brew.note).toBe('balanced');
    expect(brew.rating).toBe(8.5);
    expect(brew.tds).toBe(9.2);
  });

  test('tds defaults to 0 when the shot has none', () => {
    const [brew] = backupOf().BREWS;
    expect(brew.tds).toBe(0);
  });

  test('falls back to the shot volume when no doseOut note was entered', () => {
    const [brew] = toBeanconquerorBackup({
      beans: [BEAN],
      shots: [{ ...SHOT, notes: { ...SHOT.notes, doseOut: '' } }],
    }).BREWS;
    expect(brew.brew_beverage_quantity).toBe(36.4);
  });

  test('emits an espresso PREPARATION and references it from every brew', () => {
    const backup = backupOf();
    const [prep] = backup.PREPARATION;

    expect(backup.PREPARATION).toHaveLength(1);
    expect(prep.type).toBe('PORTAFILTER');
    expect(prep.style_type).toBe('ESPRESSO');
    expect(backup.BREWS.every(brew => brew.method_of_preparation === prep.config.uuid)).toBe(true);
  });

  test('creates one MILL per distinct grinder name', () => {
    const backup = toBeanconquerorBackup({
      beans: [BEAN],
      shots: [
        SHOT,
        { ...SHOT, id: '43', notes: { ...SHOT.notes, grinder: 'Niche Zero' } },
        { ...SHOT, id: '44', notes: { ...SHOT.notes, grinder: 'DF64' } },
      ],
    });

    expect(backup.MILL.map(mill => mill.name).sort()).toEqual(['DF64', 'Niche Zero']);
  });
});

describe('toBeanconquerorBackup — grinder (MILL) resolution precedence', () => {
  // Mirrors the shot-note contract in utils/grinderManager.js (inferGrinderForShot):
  // notes.grinderName -> shot.grinderName -> notes.grinder -> shot.grinder,
  // then the placeholder. The device stamps the authoritative grinder under
  // `grinderName` (PRO-428), so it must outrank the user-entered `grinder`.
  function millNamesFor(shot) {
    return toBeanconquerorBackup({ beans: [BEAN], shots: [shot] }).MILL.map(mill => mill.name);
  }

  test('uses the device-recorded notes.grinderName when no other grinder field exists', () => {
    expect(
      millNamesFor({ ...SHOT, notes: { beanId: 'bean-house', grinderName: 'Niche Zero' } }),
    ).toEqual(['Niche Zero']);
  });

  test('uses a top-level hoisted shot.grinderName', () => {
    expect(millNamesFor({ ...SHOT, notes: { beanId: 'bean-house' }, grinderName: 'DF64' })).toEqual(
      ['DF64'],
    );
  });

  test('device-recorded notes.grinderName outranks user-entered notes.grinder', () => {
    expect(
      millNamesFor({
        ...SHOT,
        notes: { ...SHOT.notes, grinder: 'Old Guess', grinderName: 'Niche Zero' },
      }),
    ).toEqual(['Niche Zero']);
  });

  test('hoisted shot.grinderName outranks user-entered notes.grinder', () => {
    expect(
      millNamesFor({
        ...SHOT,
        notes: { ...SHOT.notes, grinder: 'Old Guess' },
        grinderName: 'DF64',
      }),
    ).toEqual(['DF64']);
  });

  test('still falls back to the user-entered notes.grinder', () => {
    expect(millNamesFor({ ...SHOT, notes: { ...SHOT.notes, grinder: 'Niche Zero' } })).toEqual([
      'Niche Zero',
    ]);
  });

  test('still falls back to a top-level shot.grinder', () => {
    expect(millNamesFor({ ...SHOT, notes: { beanId: 'bean-house' }, grinder: 'DF64' })).toEqual([
      'DF64',
    ]);
  });

  test('falls back to the placeholder mill when no grinder is recorded anywhere', () => {
    expect(millNamesFor({ ...SHOT, notes: { beanId: 'bean-house' } })).toEqual([
      'GaggiMate (unknown grinder)',
    ]);
  });

  test('a blank device-recorded grinderName falls through to the next source', () => {
    expect(
      millNamesFor({
        ...SHOT,
        notes: { ...SHOT.notes, grinderName: '   ', grinder: 'Niche Zero' },
      }),
    ).toEqual(['Niche Zero']);
  });
});

describe('toBeanconquerorBackup — deterministic ids', () => {
  test('two runs over the same input produce byte-identical output', () => {
    expect(JSON.stringify(backupOf())).toBe(JSON.stringify(backupOf()));
  });

  test('bean uuids are keyed on the bean id, not on array position', () => {
    const first = toBeanconquerorBackup({ beans: [BEAN, { ...BEAN, id: 'b2', name: 'Other' }] });
    const second = toBeanconquerorBackup({ beans: [{ ...BEAN, id: 'b2', name: 'Other' }, BEAN] });

    const uuidByName = backup =>
      Object.fromEntries(backup.BEANS.map(bean => [bean.name, bean.config.uuid]));
    expect(uuidByName(first)).toEqual(uuidByName(second));
  });

  test('grinder uuids are stable across exports and independent of shot order', () => {
    const a = toBeanconquerorBackup({ beans: [BEAN], shots: [SHOT] });
    const b = toBeanconquerorBackup({
      beans: [BEAN],
      shots: [{ ...SHOT, id: '99' }, SHOT],
    });

    const niche = records => records.find(mill => mill.name === 'Niche Zero').config.uuid;
    expect(niche(a.MILL)).toBe(niche(b.MILL));
  });

  test('editing unrelated bean fields does not change its uuid', () => {
    const before = toBeanconquerorBackup({ beans: [BEAN] }).BEANS[0].config.uuid;
    const after = toBeanconquerorBackup({ beans: [{ ...BEAN, notes: 'changed' }] }).BEANS[0].config
      .uuid;
    expect(after).toBe(before);
  });

  test('distinct beans, grinders and shots never collide on a uuid', () => {
    const backup = toBeanconquerorBackup({
      beans: [BEAN, { ...BEAN, id: 'b2', name: 'Second' }],
      shots: [SHOT, { ...SHOT, id: '43', notes: { ...SHOT.notes, grinder: 'DF64' } }],
    });

    const uuids = Object.values(backup)
      .flat()
      .map(record => record.config.uuid);
    expect(new Set(uuids).size).toBe(uuids.length);
  });
});

describe('toBeanconquerorBackup — referential integrity', () => {
  test('every brew reference resolves inside the same archive', () => {
    const backup = toBeanconquerorBackup({
      beans: [BEAN],
      shots: [
        SHOT,
        { ...SHOT, id: '43', notes: { ...SHOT.notes, grinder: 'DF64' } },
        { ...SHOT, id: '44', notes: {} },
      ],
    });

    expect(validateBeanconquerorBackup(backup)).toEqual({ valid: true, errors: [] });
  });

  test('a shot whose bean is unknown still resolves via a placeholder bean', () => {
    const backup = toBeanconquerorBackup({
      beans: [],
      shots: [{ ...SHOT, notes: { ...SHOT.notes, beanId: 'ghost' } }],
    });

    const beanUuids = new Set(backup.BEANS.map(bean => bean.config.uuid));
    expect(backup.BREWS).toHaveLength(1);
    expect(beanUuids.has(backup.BREWS[0].bean)).toBe(true);
    expect(validateBeanconquerorBackup(backup).valid).toBe(true);
  });

  test('a shot with no grinder still resolves via a placeholder mill', () => {
    const backup = toBeanconquerorBackup({
      beans: [BEAN],
      shots: [{ ...SHOT, notes: { ...SHOT.notes, grinder: '' } }],
    });

    const millUuids = new Set(backup.MILL.map(mill => mill.config.uuid));
    expect(millUuids.has(backup.BREWS[0].mill)).toBe(true);
    expect(validateBeanconquerorBackup(backup).valid).toBe(true);
  });

  test('resolves the bean by name when the shot carries no bean id', () => {
    const backup = toBeanconquerorBackup({
      beans: [BEAN],
      shots: [{ ...SHOT, notes: { ...SHOT.notes, beanId: '', beanType: 'Pink Bourbon' } }],
    });

    const named = backup.BEANS.find(bean => bean.name === 'Pink Bourbon');
    expect(backup.BEANS).toHaveLength(1);
    expect(backup.BREWS[0].bean).toBe(named.config.uuid);
  });

  test('an export with no beans and no shots is still a valid archive', () => {
    const backup = toBeanconquerorBackup({ beans: [], shots: [] });

    expect(backup.BREWS).toEqual([]);
    expect(backup.SETTINGS).toHaveLength(1);
    expect(backup.VERSION).toHaveLength(1);
    expect(validateBeanconquerorBackup(backup).valid).toBe(true);
  });
});

describe('validateBeanconquerorBackup', () => {
  test('reports a missing top-level key', () => {
    const backup = backupOf();
    delete backup.MILL;

    const result = validateBeanconquerorBackup(backup);
    expect(result.valid).toBe(false);
    expect(result.errors.join('\n')).toMatch(/MILL/);
  });

  test('reports a dangling bean reference', () => {
    const backup = backupOf();
    backup.BREWS[0].bean = 'not-a-real-uuid';

    const result = validateBeanconquerorBackup(backup);
    expect(result.valid).toBe(false);
    expect(result.errors.join('\n')).toMatch(/bean/);
  });

  test('reports a dangling mill reference', () => {
    const backup = backupOf();
    backup.BREWS[0].mill = 'nope';

    expect(validateBeanconquerorBackup(backup).valid).toBe(false);
  });

  test('reports a dangling preparation reference', () => {
    const backup = backupOf();
    backup.BREWS[0].method_of_preparation = 'nope';

    expect(validateBeanconquerorBackup(backup).valid).toBe(false);
  });

  test('reports a record missing its config uuid', () => {
    const backup = backupOf();
    backup.BEANS[0].config = { uuid: '', unix_timestamp: 0 };

    expect(validateBeanconquerorBackup(backup).valid).toBe(false);
  });

  test('reports an attachment path, which would reference a missing file', () => {
    const backup = backupOf();
    backup.BREWS[0].attachments = ['/some/photo.jpg'];

    const result = validateBeanconquerorBackup(backup);
    expect(result.valid).toBe(false);
    expect(result.errors.join('\n')).toMatch(/attachment/i);
  });

  test('reports a flow profile path, which would reference a missing file', () => {
    const backup = backupOf();
    backup.BREWS[0].flow_profile = '/graphs/42.json';

    expect(validateBeanconquerorBackup(backup).valid).toBe(false);
  });

  test('rejects a non-object payload', () => {
    expect(validateBeanconquerorBackup(null).valid).toBe(false);
    expect(validateBeanconquerorBackup([]).valid).toBe(false);
  });
});

describe('buildBeanconquerorZip — chunking', () => {
  function manyShots(count) {
    return Array.from({ length: count }, (_, index) => ({
      ...SHOT,
      id: String(1000 + index),
    }));
  }

  function manyBeans(count) {
    return Array.from({ length: count }, (_, index) => ({
      ...BEAN,
      id: `bulk-${index}`,
      name: `Bulk ${index}`,
    }));
  }

  test('the chunk size matches the pinned upstream value', () => {
    expect(BEANCONQUEROR_CHUNK_SIZE).toBe(500);
  });

  test('a small export is a single Beanconqueror.json entry', async () => {
    const { entries } = await buildBeanconquerorZip(backupOf());

    expect(Object.keys(entries)).toEqual(['Beanconqueror.json']);
    const main = JSON.parse(entries['Beanconqueror.json']);
    expect(main.BREWS).toHaveLength(1);
    expect(main.BEANS).toHaveLength(1);
  });

  test('exactly 500 brews still fit in the main file', async () => {
    const backup = toBeanconquerorBackup({ beans: [BEAN], shots: manyShots(500) });
    const { entries } = await buildBeanconquerorZip(backup);

    expect(Object.keys(entries)).toEqual(['Beanconqueror.json']);
    expect(JSON.parse(entries['Beanconqueror.json']).BREWS).toHaveLength(500);
  });

  test('501 brews spill into Beanconqueror_Brews_1.json', async () => {
    const backup = toBeanconquerorBackup({ beans: [BEAN], shots: manyShots(501) });
    const { entries } = await buildBeanconquerorZip(backup);

    expect(Object.keys(entries).sort()).toEqual([
      'Beanconqueror.json',
      'Beanconqueror_Brews_1.json',
    ]);
    expect(JSON.parse(entries['Beanconqueror.json']).BREWS).toHaveLength(500);
    expect(JSON.parse(entries['Beanconqueror_Brews_1.json'])).toHaveLength(1);
  });

  test('1001 brews produce two numbered chunk files', async () => {
    const backup = toBeanconquerorBackup({ beans: [BEAN], shots: manyShots(1001) });
    const { entries } = await buildBeanconquerorZip(backup);

    expect(Object.keys(entries).sort()).toEqual([
      'Beanconqueror.json',
      'Beanconqueror_Brews_1.json',
      'Beanconqueror_Brews_2.json',
    ]);
    expect(JSON.parse(entries['Beanconqueror_Brews_1.json'])).toHaveLength(500);
    expect(JSON.parse(entries['Beanconqueror_Brews_2.json'])).toHaveLength(1);
  });

  test('beans chunk under the Beans name', async () => {
    const backup = toBeanconquerorBackup({ beans: manyBeans(501), shots: [] });
    const { entries } = await buildBeanconquerorZip(backup);

    expect(Object.keys(entries).sort()).toEqual([
      'Beanconqueror.json',
      'Beanconqueror_Beans_1.json',
    ]);
    expect(JSON.parse(entries['Beanconqueror_Beans_1.json'])).toHaveLength(1);
  });

  test('reassembling main plus chunks restores the whole backup', async () => {
    const backup = toBeanconquerorBackup({ beans: manyBeans(600), shots: manyShots(1200) });
    const { entries } = await buildBeanconquerorZip(backup);

    const restored = JSON.parse(entries['Beanconqueror.json']);
    for (const [name, body] of Object.entries(entries)) {
      const match = /^Beanconqueror_(Brews|Beans)_\d+\.json$/.exec(name);
      if (!match) continue;
      restored[match[1] === 'Brews' ? 'BREWS' : 'BEANS'].push(...JSON.parse(body));
    }

    expect(restored.BREWS).toHaveLength(backup.BREWS.length);
    expect(restored.BEANS).toHaveLength(backup.BEANS.length);
    expect(restored).toEqual(backup);
    expect(validateBeanconquerorBackup(restored).valid).toBe(true);
  });

  test('the blob is a real zip named Beanconqueror.zip whose entries round-trip', async () => {
    const { blob, filename } = await buildBeanconquerorZip(backupOf());

    expect(filename).toBe('Beanconqueror.zip');
    expect(blob.type).toBe('application/zip');

    const bytes = new Uint8Array(await blob.arrayBuffer());
    // Local file header signature "PK\x03\x04".
    expect([...bytes.slice(0, 4)]).toEqual([0x50, 0x4b, 0x03, 0x04]);
    // End-of-central-directory signature "PK\x05\x06".
    expect(bytes.length).toBeGreaterThan(22);
    expect(new TextDecoder().decode(bytes)).toContain('Beanconqueror.json');
  });

  test('refuses to build a zip from a backup that fails validation', async () => {
    const backup = backupOf();
    backup.BREWS[0].bean = 'dangling';

    await expect(buildBeanconquerorZip(backup)).rejects.toThrow(/valid/i);
  });
});
