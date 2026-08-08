# Beanconqueror backup export (PRO-632)

GaggiMate can export its bean library and shot history as a
`Beanconqueror.zip` archive that Beanconqueror's own **Import backup** flow
restores.

## Where the export lives in the UI

| Page | Control | Scope |
|---|---|---|
| **Shot History** | zip icon, `Export Beanconqueror Backup` | whole bean library **+ every filter-visible shot** (the same scope the CSV export uses — search, filters and sort applied, pagination ignored) |
| **Bean Library** | zip icon, `Export Beanconqueror Backup (beans only)` | whole bean library, **no brews** (the page has no access to shot history) |

Both controls share one module (`web/src/utils/beanconqueror/`), so the two
archives are byte-identical for the same bean input.

## ⚠️ Import replaces the target library

Beanconqueror's importer writes every top-level key of `Beanconqueror.json`
straight into its storage (upstream `uiStorage.ts`) — it **replaces**, it does
not merge. Importing this archive over an existing Beanconqueror library
destroys that library's beans and brews. Both export buttons therefore
require an explicit confirmation naming that limitation; only import into a
fresh Beanconqueror installation or profile.

## Module layout

| File | Responsibility |
|---|---|
| `beanconquerorExport.js` | **pure** serializer + validator (`toBeanconquerorBackup`, `validateBeanconquerorBackup`, `buildBeanconquerorZip`). No DOM, no I/O, no randomness. |
| `beanconquerorDownload.js` | the only DOM seam — hands the Blob to `utils/download.js`. |
| `beanconquerorTemplate.json` | sanitized record fixture pinned to upstream `graphefruit/Beanconqueror` commit `b713c3e`. |
| `storeZip.js` | ~110-line STORE (uncompressed) ZIP writer, so no zip dependency enters the ESP32-hosted bundle. |
| `uuidV5.js` | synchronous SHA-1 / RFC 4122 v5 UUIDs. |

## Format contract

The archive holds `Beanconqueror.json` with all six top-level storage keys:
`BEANS`, `BREWS`, `MILL`, `PREPARATION`, `SETTINGS`, `VERSION`. Brews carry
foreign-key UUIDs for their bean, mill and preparation, so those records must
travel in the same archive — a beans-and-shots-only payload is not importable.

The upstream representation is an **internal, unversioned storage dump**: there
is no published import schema. Record shapes therefore come from the pinned
fixture rather than being hand-written, and the tests assert against that
fixture's field set. When bumping the pin, regenerate
`beanconquerorTemplate.json` from a real (sanitized) Beanconqueror backup and
update the commit hash in every reference.

### Chunking

Upstream `EXPORT_CHUNKING_CONFIG` chunks `BREWS` and `BEANS` at **500** entries.
Chunk 0 stays in `Beanconqueror.json`; overflow lands in
`Beanconqueror_Brews_<n>.json` / `Beanconqueror_Beans_<n>.json` starting at
`n = 1`, and those files appear only once the threshold is exceeded.

### Deterministic UUIDs

Every id is a v5 UUID minted from the fixed namespace
`6f9f4a4e-5d0f-4a3e-9b1f-4a5a4f6b7c8d`, keyed on stable input
(`bean:<id>`, `mill:<lowercased grinder name>`, `preparation:espresso`,
`brew:<source>:<shot id>`). **Never change the namespace** — a different
namespace makes a re-export of the same library look like a whole new set of
records to Beanconqueror. Editing unrelated bean fields does not change a
bean's UUID.

### Field mapping

| GaggiMate | Beanconqueror | Note |
|---|---|---|
| bean `name` / `roaster` / `roastDate` / `notes` | `name` / `roaster` / `roastingDate` / `note` | |
| bean `roastLevel` | `roast` (+ `roast_custom`) | matched against upstream `ROASTS_ENUM`; unrecognised text becomes `CUSTOM_ROAST` |
| bean `quantity` (remaining g) | `weight` (bag total) = `quantity` + this archive's accounted consumption | see **Bean inventory** below |
| bean `archived` | `finished` | |
| bean `origin` / `process` | `bean_information[0].country` / `.processing` | |
| shot note `doseIn` | `grind_weight` | |
| shot note `doseOut` (else `shot.volume`) | `brew_beverage_quantity` + `brew_beverage_quantity_type: 'GR'` | espresso yield; `brew_quantity` has no GaggiMate source and stays at its upstream default |
| shot note `grindSetting` | `grind_size` | |
| shot `duration` (ms) | `brew_time` (s) + `brew_time_milliseconds` | |
| shot log first plausible `tt` sample | `brew_temperature` | via `deriveStartTargetTemperature` (PRO-631); shots exported without their sample trace carry `0` |
| shot note `notes` / `rating` / `tds` | `note` / `rating` / `tds` | |

Shots whose bean or grinder cannot be resolved get **one shared deterministic
placeholder record** each, because a brew with a dangling UUID reference is not
importable.

### Bean inventory (PRO-632)

Beanconqueror has **no stored "consumed" field** — there is no `weight_used`
anywhere in upstream at the pinned commit (`grep -rn weight_used src/` → 0 hits;
`src/classes/bean/bean.ts` declares only `weight` and `finished`). A bean carries
just `weight`, the **bag total**, and the app derives the rest from the brews
that reference it:

```
consumed  = SUM(brew.bean_weight_in > 0 ? brew.bean_weight_in : brew.grind_weight)
available = bean.weight - consumed
```

That derivation is `getUsedWeightCount()`, duplicated across
`bean-information.component.ts`, `dashboard.page.ts`,
`bean-sort-filter-helper.service.ts`, `bean-popover-freeze.component.ts` and
`uiBrewHelper.ts`; it is rendered by `bean-information.component.html` as
`{{gramUsed}}g of {{gramTotal}}g ({{leftOver}}g)` with
`leftOver: bean.weight - uiUsedWeightCount`.

GaggiMate's `bean.quantity` is **remaining** grams (`beanManager.js`
`syncBeanUsageFromNotes` decrements it by each shot's `doseIn`). Mapping it
straight onto `weight` put remaining into the bag-total slot, so Beanconqueror
subtracted the exported doses from it a *second* time — displaying consumption as
availability and going **negative** once the exported doses exceeded what was
left in the bag.

Because GaggiMate keeps no purchase mass and no consumption history, the only
self-consistent representation is to solve the upstream relation for the one
quantity we know:

```
weight = quantity + consumed      (consumed = doses of the brews IN THIS ARCHIVE)
```

Nothing is invented — the consumption term is summed from the doses those brews
already carry, using upstream's own `bean_weight_in > 0 ? bean_weight_in :
grind_weight` precedence. A **beans-only** export has no brews, so the sum is `0`
and it degenerates to `weight = quantity`, consumed `0`. Available weight
therefore always equals GaggiMate's remaining quantity and can never be
negative — including for the shared placeholder bean, which has no GaggiMate
quantity but still owns real brew doses.

The one input the bag total cannot represent is a sum that **overflows** to
`Infinity` (each operand finite, `quantity + consumed` past `Number.MAX_VALUE`).
`JSON.stringify` writes `Infinity` and `NaN` as `null`, so such an archive would
hand Beanconqueror a bean with no weight at all. Clamping is not an option — no
finite bag total satisfies `available = weight - consumed` there, so it would
break the invariant above. The validator instead **rejects** any record carrying
a non-finite number (reported as `BEANS[0].weight is not a finite number.`) and
`buildBeanconquerorZip` refuses to build, surfacing as the usual "Beanconqueror
export failed" alert rather than a silently corrupt file.

### Never emitted

Attachment paths, `flow_profile` and `reference_flow_profile` are deliberately
left empty and the validator **rejects** a backup that carries any of them —
they would reference files this exporter does not ship inside the archive. The
validator likewise rejects any non-finite number anywhere in a record, so the
archive can never contain a JSON `null` in place of a number.

## Verification status

Structural, referential-integrity, determinism and chunking behaviour are
covered by host unit tests (`npm test` in `web/`). A round-trip **import smoke
test into a fresh Beanconqueror installation has not been performed** — it needs
the Beanconqueror app on a real device/emulator, which is outside this
repository's automated test environment. Treat Beanconqueror compatibility as
*unverified against the live app* until that smoke test is done.
