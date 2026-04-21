# Gaggimate Web UI Redesign Specification

**Status:** In Progress  
**Direction:** "Dark Precision" — minimal, futuristic, fluid  
**Stack:** Preact + Tailwind CSS + Chart.js + FontAwesome  

---

## Design Language

### Color Palette

| Token | Hex | Usage |
|-------|-----|-------|
| `--bg-base` | `#09090b` | Page background |
| `--bg-elevated` | `#0f0f12` | Cards, panels |
| `--bg-glass` | `rgba(15,15,18,0.7)` | Frosted overlays |
| `--border` | `rgba(255,255,255,0.08)` | Dividers, borders |
| `--border-active` | `rgba(255,255,255,0.15)` | Focused borders |
| `--accent` | `#f59e0b` | Amber — primary action, highlights |
| `--accent-glow` | `rgba(245,158,11,0.15)` | Glow effects |
| `--text-primary` | `#fafafa` | Main text |
| `--text-secondary` | `rgba(255,255,255,0.5)` | Labels, hints |
| `--text-muted` | `rgba(255,255,255,0.25)` | Disabled, placeholders |
| `--success` | `#22c55e` | Confirmation |
| `--warning` | `#eab308` | Caution |
| `--error` | `#ef4444` | Danger |

### Typography

- **UI Font:** Inter (Google Fonts) — weights 400, 500, 600, 700
- **Data Font:** JetBrains Mono (Google Fonts) — weights 400, 500
  - Used for: temperature readings, pressure values, flow rates, timers, weights
  - Size: slightly smaller than UI text, creates visual hierarchy
- **Base size:** 14px (desktop), 16px (mobile)
- **Scale:** 12, 14, 16, 20, 24, 32, 48

### Spatial System

- **Base unit:** 4px
- **Spacing scale:** 4, 8, 12, 16, 20, 24, 32, 48, 64
- **Border radius:** 6px (small), 10px (medium), 16px (large)
- **Content max-width:** 1400px

### Motion Philosophy

- **Duration:** 150ms (micro), 250ms (standard), 400ms (emphasis)
- **Easing:** `cubic-bezier(0.16, 1, 0.3, 1)` — spring-out feel
- **Rules:**
  - Every hover has feedback (150ms)
  - Page transitions: cross-fade 250ms
  - Modal/panel appear: scale(0.96→1) + fade, 250ms
  - Loading states: skeleton shimmer, never spinners
  - Stagger: 50ms between items in lists

### Visual Effects

- **Glass panels:** `backdrop-filter: blur(20px)` + semi-transparent bg
- **Borders:** 1px solid `--border`, no box-shadows for structure
- **Accent glow:** `box-shadow: 0 0 20px var(--accent-glow)` on active elements
- **Scrollbars:** 4px wide, `--border` color track, `--text-muted` thumb

---

## Global Layout

### Structure

```
┌─ App Shell ─────────────────────────────────────────────┐
│ ┌─── Top Bar (48px) ─────────────────────────────────┐ │
│ │  [Logo]  [Mode pill]        [Status]  [⚙️] [👤]   │ │
│ └────────────────────────────────────────────────────┘ │
│                                                         │
│ ┌─── Content Area ───────────────────────────────────┐ │
│ │                                                   │ │
│ │   Full-width, max-width: 1400px, centered          │ │
│ │   No sidebar on desktop by default                │ │
│ │   Collapsible nav appears on hover (future)       │ │
│ │                                                   │ │
│ └────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

### Navigation Model

- **Top bar only** — no persistent sidebar
- Navigation items in top bar as icon buttons with tooltips
- Active page: icon fills with `--accent`
- Hover: `--border-active` border appears
- Mobile: bottom tab bar with 5 core icons

### Page Structure

Every page has:
1. **Page header** — title + primary action (right-aligned)
2. **Content area** — page-specific
3. **No card borders around everything** — use whitespace to separate sections

---

## Component Inventory

### 1. TopBar

- Fixed, 48px height, `--bg-base` with bottom border
- Left: Logo (text "G" in accent color, or custom icon)
- Center: Mode indicator pill (Standby/Brew/Steam/Water/Grind)
- Right: Status indicators, settings icon, user avatar

**States:**
- Default: static display
- Brewing: mode pill pulses with `--accent-glow`

### 2. ModePill

- Rounded pill showing current mode
- Background: `--bg-elevated`, border: `--border`
- Active state: `--accent` border + text
- Icon left of text (flame, drop, etc.)

### 3. DataValue

- Monospace number + unit
- Large: 32px font, used for hero stats
- Medium: 20px font, used in cards
- Small: 14px font, used in labels

**States:**
- Default: `--text-primary`
- Loading: skeleton shimmer
- Stale: `--text-muted`

### 4. ActionButton

- Icon + label, `--bg-elevated` background, `--border` border
- Hover: `--border-active`, slight scale(1.02)
- Active/Press: scale(0.98), background shifts
- Disabled: 40% opacity, no pointer events

**Variants:**
- Primary: `--accent` background, dark text
- Ghost: transparent bg, `--border` border
- Danger: `--error` background, white text

### 5. ProcessChart

- Full-width area chart (pressure/flow over time)
- Dark background (`--bg-base`), subtle grid lines
- Line: `--accent` with glow
- Current point: pulsing dot
- Phase markers: vertical dashed lines, labeled

**States:**
- Idle: flat line at baseline
- Active: live updating
- Finished: full chart with phase highlights

### 6. ProfileCard

- Horizontal layout: name, origin, roast date, actions
- No border — whitespace separation
- Hover: `--border-active` all edges, `--bg-elevated` background
- Actions reveal on hover (edit, export, delete)

### 7. BeanCard

- Similar to ProfileCard
- Shows: name, roaster, roast date, process method
- Tasting notes as small tags

### 8. ShotRow

- Table row with: timestamp, profile, weight, yield, ratio, score
- Monospace for all numeric values
- Hover: row background `--bg-elevated`
- Click: expand to show shot details inline

### 9. StatCard

- Compact display: label + large value + trend arrow
- No borders — just text on `--bg-elevated` rounded panel
- Sparkline in background (very subtle)

### 10. GlassPanel

- Reusable floating panel
- `backdrop-filter: blur(20px)`, `--bg-glass`
- `border: 1px solid --border`
- `border-radius: 16px`
- Appears with scale animation

### 11. Toast

- Bottom-right position, stacked
- `GlassPanel` style with icon left
- Auto-dismiss: 3s (info), 5s (warning), manual (error)
- Slide in from right, fade out

### 12. EmptyState

- Centered illustration (simple SVG icon)
- Heading + subtext
- Primary action button below

### 13. SkeletonLoader

- Rounded rectangles matching content layout
- Shimmer animation: left-to-right gradient sweep
- Used everywhere instead of spinners

### 14. TabBar

- Horizontal tabs with underline indicator
- Active tab: `--accent` underline, bold text
- Hover: `--text-secondary` → `--text-primary`
- Transition: underline slides, 200ms

### 15. InputField

- Single line, `--bg-base` background, `--border` border
- Focus: `--accent` border, subtle glow
- Label above, error message below
- No outer card — inline styling

### 16. Toggle

- Pill-shaped, 40px wide × 20px tall
- Off: `--bg-elevated` track
- On: `--accent` track with white dot right-aligned
- Transition: 200ms spring

### 17. Slider

- Horizontal track, `--bg-elevated`
- Fill: `--accent` gradient
- Thumb: white circle, 16px, `--accent` border
- Labels at each end (min/max)

### 18. ProgressBar

- Thin (4px) horizontal bar
- Track: `--bg-elevated`
- Fill: `--accent`, optional glow at leading edge
- Label above or beside

---

## Page Specifications

### Home / Dashboard

**Purpose:** At-a-glance machine status + quick actions

**Layout:**
```
┌─ TopBar ──────────────────────────────────────────────┐
└────────────────────────────────────────────────────────┘
┌─ Status Strip (compact) ──────────────────────────────┐
│ [●] Brewing  ·  93.2°C  ·  18.4g / 36.1g  ·  28s     │
└────────────────────────────────────────────────────────┘
┌─ ProcessDisplay (hero, 300px tall) ───────────────────┐
│                                                       │
│   [Profile name]  ·  phase 2/4                        │
│   ████████████████░░░░░░░░░░  28s elapsed             │
│   [Pressure curve chart - live]                        │
│                                                       │
└────────────────────────────────────────────────────────┘
┌─ QuickActions ────────────────────────────────────────┐
│  [▶ Start]   [⏸ Pause]   [⏹ Stop]   [Flush]         │
└────────────────────────────────────────────────────────┘
┌─ ProfileSelector ─────────────────────────────────────┐
│  [Selected profile name ▼]        [+ New] [⚙ Edit]   │
└────────────────────────────────────────────────────────┘
┌─ RecentShots (3 items) ───────────────────────────────┐
│  espresso · 18.4g → 36.1g · 28s · score: 87          │
│  espresso · 18.2g → 35.8g · 30s · score: 85          │
│  espresso · 18.5g → 37.0g · 27s · score: 89          │
└────────────────────────────────────────────────────────┘
```

**Components:**
- ModePill (top bar center)
- StatusStrip (new) — one-line summary of all key values
- ProcessChart (hero area)
- ActionButton group
- ProfileSelector dropdown
- RecentShots list (compact rows)

### Profiles Page

**Purpose:** Browse, search, import/export profiles

**Layout:**
```
┌─ PageHeader ──────────────────────────────────────────┐
│  Profiles                              [+ New] [Import] [Export] │
└────────────────────────────────────────────────────────┘
┌─ SearchBar ───────────────────────────────────────────┐
│  [🔍 Search profiles...]                              │
└────────────────────────────────────────────────────────┘
┌─ ProfileList ─────────────────────────────────────────┐
│  ProfileCard row                                       │
│  ProfileCard row                                       │
│  ProfileCard row                                       │
└────────────────────────────────────────────────────────┘
```

**Components:**
- SearchBar (icon left, placeholder text)
- ProfileCard (horizontal, hover reveals actions)
- EmptyState when no profiles

### Beans Page

**Purpose:** Manage coffee beans inventory

**Layout:** Same as Profiles — list with search + CRUD actions

**BeanCard extras:**
- Tags for process method (Washed, Natural, Honey)
- Roast level indicator (light → dark gradient bar)
- Quantity remaining (if tracked)

### Shot History Page

**Purpose:** Browse past shots in reverse chronological order

**Layout:**
```
┌─ PageHeader ──────────────────────────────────────────┐
│  Shot History                                          │
└────────────────────────────────────────────────────────┘
┌─ Filters ─────────────────────────────────────────────┐
│  [All] [Espresso] [Pour Over]   Sort: [Recent ▼]     │
└────────────────────────────────────────────────────────┘
┌─ ShotTable ────────────────────────────────────────────┐
│  Time       Profile      In    Out   Ratio  Score [→] │
│  2:30 PM    Morning      18.4  36.1  1:2.0   87  →   │
│  1:15 PM    Evening      18.2  35.8  1:2.0   85  →   │
└────────────────────────────────────────────────────────┘
```

**Components:**
- TabBar (filter by type)
- ShotRow (expandable — click to see full details)
- Sort dropdown

### Shot Analyzer Page

**Purpose:** Deep dive into a single shot's data

**Layout:**
```
┌─ PageHeader ──────────────────────────────────────────┐
│  ← Back   Shot Analyzer   [Share] [Export]          │
└────────────────────────────────────────────────────────┘
┌─ ShotSummary ─────────────────────────────────────────┐
│  espresso · 18.4g → 36.1g · 28s · ratio 1:2.0         │
│  Score: 87  ·  Apr 19, 2026 2:30 PM                  │
└────────────────────────────────────────────────────────┘
┌─ PressureFlowChart (full width, 250px tall) ─────────┐
│  [Live chart with phases labeled]                     │
└────────────────────────────────────────────────────────┘
┌─ PhaseBreakdown ──────────────────────────────────────┐
│  Preinfusion: 0-4s  0-3 bar                          │
│  Ramp:        4-10s  3-9 bar                          │
│  ...                                                  │
└────────────────────────────────────────────────────────┘
```

### Statistics Page

**Purpose:** Aggregate data visualization

**Layout:**
```
┌─ PageHeader ──────────────────────────────────────────┐
│  Statistics                         [Export]          │
└────────────────────────────────────────────────────────┘
┌─ StatCardGrid ────────────────────────────────────────┐
│  [Total Shots] [Avg Score] [Avg Extraction] [Time]  │
└────────────────────────────────────────────────────────┘
┌─ Charts Area ─────────────────────────────────────────┐
│  [Score over time - line chart]                       │
│  [Grind distribution - bar chart]                     │
│  [Extraction yield - scatter]                         │
└────────────────────────────────────────────────────────┘
```

### Settings Page

**Purpose:** Machine configuration

**Layout:**
```
┌─ PageHeader ──────────────────────────────────────────┐
│  Settings                                              │
└────────────────────────────────────────────────────────┘
┌─ SettingsSections (accordion-style) ──────────────────┐
│  ▼ Machine                                            │
│    Boiler Temp: [slider] 93°C                        │
│    Preinfusion time: [slider] 4s                      │
│  ▼ WiFi                                              │
│    Network: [dropdown]                                │
│    [Connect] [Forget]                                 │
│  ▼ Display                                           │
│    Theme: [toggle: Dark/Light]                        │
└────────────────────────────────────────────────────────┘
```

---

## Migration Notes

### Files to Change

| File | Change |
|------|--------|
| `web/src/index.jsx` | TopBar only layout, remove sidebar grid |
| `web/src/style.css` | CSS variables, font imports, base styles |
| `web/src/components/` | New: TopBar, ModePill, DataValue, GlassPanel, etc. |
| `web/src/pages/Home/index.jsx` | Complete rewrite to new layout |
| `web/src/pages/ProfileList/index.jsx` | Restyle to use ProfileCard |
| `web/src/pages/Settings/index.jsx` | Convert to accordion sections |
| `web/src/pages/ShotHistory/index.jsx` | Restyle table to ShotRow |
| Other pages | Incremental restyle |

### New Components to Create

1. `TopBar` — navigation bar
2. `ModePill` — mode indicator
3. `DataValue` — monospace data display
4. `StatusStrip` — summary line
5. `ActionButton` — primary button component
6. `GlassPanel` — reusable overlay/panel
7. `Toast` — notification system
8. `SkeletonLoader` — loading state
9. `EmptyState` — empty list placeholder
10. `TabBar` — tab navigation
11. Restyled versions of: ProfileCard, BeanCard, ShotRow, StatCard, InputField, Toggle, Slider, ProgressBar

### What Gets Removed

- Card borders everywhere (use whitespace)
- Section labels like "Control", "Review" (use top bar)
- Bootstrap/Tailwind default component styling
- Spinners (replaced with skeleton)
- Modal dialogs for simple actions (use inline edit or Toast)

---

## Implementation Priority

1. **Global styles + TopBar** — foundation
2. **Home page** — the hero, biggest visual impact
3. **ProfileList + ProfileCard** — profiles are core workflow
4. **Beans page + BeanCard** — similar pattern
5. **ShotHistory + ShotRow** — table styling
6. **ShotAnalyzer** — chart integration
7. **Statistics** — chart pages
8. **Settings** — accordion form

---

## Animation Checklist

Every component must have:
- [ ] Hover state (150ms)
- [ ] Active/press state (scale down)
- [ ] Loading state (skeleton, not spinner)
- [ ] Empty/zero state (friendly message)

Every page transition:
- [ ] Fade in content (250ms)
- [ ] Stagger list items (50ms delay each)