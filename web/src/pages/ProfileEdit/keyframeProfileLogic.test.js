import { test, expect } from 'vitest';

import {
  addKeyframeAtTime,
  keyframesToProfile,
  moveKeyframeTime,
  normalizeKeyframes,
  profileToKeyframes,
  removeKeyframeAtIndex,
  updateKeyframeSegment,
  updateKeyframeValue,
} from './keyframeProfileLogic.js';

const baseProfile = {
  label: 'Keyframe Espresso',
  type: 'pro',
  temperature: 93,
  phases: [
    {
      name: 'Start',
      phase: 'preinfusion',
      valve: 1,
      pump: { target: 'pressure', pressure: 9, flow: 4 },
      duration: 0,
      transition: { type: 'instant', duration: 0, adaptive: true },
      targets: [],
      temperature: 90,
    },
    {
      name: 'Ramp Down',
      phase: 'brew',
      valve: 1,
      pump: { target: 'pressure', pressure: 6, flow: 4 },
      duration: 10,
      transition: { type: 'linear', duration: 10, adaptive: true },
      targets: [],
      temperature: 90,
    },
  ],
};

test('converts profile phases to persisted keyframe markers', () => {
  const markers = profileToKeyframes(baseProfile);

  expect(markers.map(marker => marker.time)).toEqual([0, 10]);
  expect(markers[0].temperature).toBe(90);
  expect(markers[0].pressure).toBe(9);
  expect(markers[0].flow).toBe(4);
  expect(markers[1].pressure).toBe(6);
  expect(markers[1].rampType).toBe('linear');
});

test('converts legacy profiles without a setup phase by synthesizing a start marker', () => {
  const legacy = {
    ...baseProfile,
    phases: baseProfile.phases.slice(1),
  };
  const markers = profileToKeyframes(legacy);

  expect(markers.map(marker => marker.time)).toEqual([0, 10]);
  expect(markers[0].pressure).toBe(6);
  expect(markers[1].pressure).toBe(6);
});

test('writes initial keyframe as zero-second setup phase', () => {
  const profile = keyframesToProfile(baseProfile, profileToKeyframes(baseProfile));

  expect(profile.phases[0].duration).toBe(0);
  expect(profile.phases[0].pump.pressure).toBe(9);
  expect(profile.phases[1].duration).toBe(10);
  expect(profile.phases[1].pump.target).toBe('pressure');
  expect(profile.phases[1].transition.type).toBe('linear');
});

test('adding a marker splits the matching segment and keeps sorted time', () => {
  const result = addKeyframeAtTime(baseProfile, 4);

  expect(result.profile.phases.map(phase => phase.duration)).toEqual([0, 4, 6]);
  expect(result.selectedSegmentIndex).toBe(1);
});

test('moving a marker changes adjacent durations without crossing neighbors', () => {
  const withMarker = addKeyframeAtTime(baseProfile, 4).profile;
  const result = moveKeyframeTime(withMarker, 1, 7);

  expect(result.profile.phases.map(phase => phase.duration)).toEqual([0, 7, 3]);
});

test('removing an interior marker merges the neighboring time span', () => {
  const withMarker = addKeyframeAtTime(baseProfile, 4).profile;
  const result = removeKeyframeAtIndex(withMarker, 1);

  expect(result.profile.phases.map(phase => phase.duration)).toEqual([0, 10]);
  expect(result.selectedSegmentIndex).toBe(0);
});

test('editing segment target mode preserves the other value as a limit', () => {
  const result = updateKeyframeSegment(baseProfile, 0, {
    targetMode: 'flow',
    pressure: 8,
    flow: 3.2,
    rampType: 'ease-out',
  });

  const phase = result.profile.phases[1];
  expect(phase.pump.target).toBe('flow');
  expect(phase.pump.pressure).toBe(8);
  expect(phase.pump.flow).toBe(3.2);
  expect(phase.transition.type).toBe('ease-out');
  expect(phase.transition.duration).toBe(10);
});

test('adding a marker duplicates matching segment metadata without shifting later phases', () => {
  const profile = {
    ...baseProfile,
    phases: [
      {
        ...baseProfile.phases[0],
        valve: 1,
        targets: [{ curve: 'setup' }],
        transition: { type: 'instant', duration: 0, adaptive: true },
      },
      {
        ...baseProfile.phases[1],
        name: 'First Runnable',
        valve: 2,
        duration: 8,
        targets: [{ curve: 'first' }],
        transition: { type: 'linear', duration: 8, adaptive: false },
      },
      {
        ...baseProfile.phases[1],
        name: 'Second Runnable',
        valve: 3,
        duration: 12,
        targets: [{ curve: 'second' }],
        transition: { type: 'ease-in', duration: 12, adaptive: true },
      },
    ],
  };

  const result = addKeyframeAtTime(profile, 3);

  expect(result.profile.phases.map(phase => phase.duration)).toEqual([0, 3, 5, 12]);
  expect(result.profile.phases.map(phase => phase.valve)).toEqual([1, 2, 2, 3]);
  expect(result.profile.phases.map(phase => phase.targets[0]?.curve)).toEqual([
    'setup',
    'first',
    'first',
    'second',
  ]);
  expect(result.profile.phases.map(phase => phase.transition.adaptive)).toEqual([
    true,
    false,
    false,
    true,
  ]);
});

test('normalizing duplicate marker times accumulates minimum spacing', () => {
  const markers = normalizeKeyframes([{ time: 0 }, { time: 0 }, { time: 0 }]);

  expect(markers.map(marker => marker.time)).toEqual([0, 0.1, 0.2]);
});

test('numeric pump phases migrate to explicit pressure flow keyframes after edit', () => {
  const profile = {
    ...baseProfile,
    phases: [
      { ...baseProfile.phases[0], pump: 7 },
      { ...baseProfile.phases[1], pump: 6 },
    ],
  };

  const markers = profileToKeyframes(profile);
  const result = updateKeyframeSegment(profile, 0, { pressure: 8, flow: 3.5 });

  expect(markers[0].pressure).toBe(9);
  expect(markers[0].flow).toBe(4);
  expect(markers[1].pressure).toBe(9);
  expect(markers[1].flow).toBe(4);
  expect(result.profile.phases.map(phase => phase.pump)).toEqual([
    { target: 'pressure', pressure: 9, flow: 4 },
    { target: 'pressure', pressure: 8, flow: 3.5 },
  ]);
});

test('keyframesToProfile clones metadata target arrays', () => {
  const markers = profileToKeyframes(baseProfile);
  const metadataTargets = [{ curve: 'shared' }];
  const profile = keyframesToProfile(baseProfile, markers, [
    baseProfile.phases[0],
    { ...baseProfile.phases[1], targets: metadataTargets },
  ]);

  expect(profile.phases[1].targets).toEqual(metadataTargets);
  expect(profile.phases[1].targets).not.toBe(metadataTargets);
});

test('removing a marker preserves metadata for surviving later segments', () => {
  const profile = {
    ...baseProfile,
    phases: [
      {
        ...baseProfile.phases[0],
        valve: 1,
        targets: [{ curve: 'setup' }],
        transition: { type: 'instant', duration: 0, adaptive: true },
      },
      {
        ...baseProfile.phases[1],
        name: 'A',
        valve: 2,
        duration: 4,
        targets: [{ curve: 'a' }],
        transition: { type: 'linear', duration: 4, adaptive: false },
      },
      {
        ...baseProfile.phases[1],
        name: 'B',
        valve: 3,
        duration: 6,
        targets: [{ curve: 'b' }],
        transition: { type: 'ease-out', duration: 6, adaptive: true },
      },
    ],
  };

  const result = removeKeyframeAtIndex(profile, 1);

  expect(result.profile.phases.map(phase => phase.duration)).toEqual([0, 10]);
  expect(result.profile.phases[1].name).toBe('B');
  expect(result.profile.phases[1].valve).toBe(3);
  expect(result.profile.phases[1].targets).toEqual([{ curve: 'b' }]);
  expect(result.profile.phases[1].transition.adaptive).toBe(true);
});

test('standalone keyframe round trip preserves adaptive false transitions', () => {
  const profile = {
    ...baseProfile,
    phases: [
      baseProfile.phases[0],
      {
        ...baseProfile.phases[1],
        transition: { type: 'linear', duration: 10, adaptive: false },
      },
    ],
  };

  const roundTrip = keyframesToProfile(profile, profileToKeyframes(profile));

  expect(roundTrip.phases[1].transition.type).toBe('linear');
  expect(roundTrip.phases[1].transition.adaptive).toBe(false);
});

test('adding a marker to a legacy profile keeps synthesized metadata aligned', () => {
  const legacy = {
    ...baseProfile,
    phases: [
      {
        ...baseProfile.phases[1],
        name: 'Legacy A',
        valve: 2,
        duration: 8,
        targets: [{ curve: 'legacy-a' }],
        transition: { type: 'linear', duration: 8, adaptive: false },
      },
      {
        ...baseProfile.phases[1],
        name: 'Legacy B',
        valve: 3,
        duration: 12,
        targets: [{ curve: 'legacy-b' }],
        transition: { type: 'ease-in', duration: 12, adaptive: true },
      },
    ],
  };

  const result = addKeyframeAtTime(legacy, 3);

  expect(result.profile.phases.map(phase => phase.duration)).toEqual([0, 3, 5, 12]);
  expect(result.profile.phases.map(phase => phase.valve)).toEqual([2, 2, 2, 3]);
  expect(result.profile.phases.map(phase => phase.targets[0]?.curve)).toEqual([
    'legacy-a',
    'legacy-a',
    'legacy-a',
    'legacy-b',
  ]);
  expect(result.profile.phases.map(phase => phase.transition.adaptive)).toEqual([
    false,
    false,
    false,
    true,
  ]);
});

test('updating a segment applies metadata field patches to the runnable phase', () => {
  const result = updateKeyframeSegment(baseProfile, 0, {
    phase: 'water',
    valve: 0,
    targets: [{ curve: 'edited' }],
  });

  expect(result.profile.phases[1].phase).toBe('water');
  expect(result.profile.phases[1].valve).toBe(0);
  expect(result.profile.phases[1].targets).toEqual([{ curve: 'edited' }]);
});

test('updates selected segment values through next marker semantics', () => {
  const result = updateKeyframeSegment(baseProfile, 0, {
    temperature: 91,
    pressure: 5.5,
    flow: 3,
    targetMode: 'pressure',
    rampType: 'ease-in-out',
  });

  expect(result.profile.phases[1].temperature).toBe(91);
  expect(result.profile.phases[1].pump.pressure).toBe(5.5);
  expect(result.profile.phases[1].pump.flow).toBe(3);
  expect(result.profile.phases[1].transition.type).toBe('ease-in-out');
});

test('duration patch on setup-phase profile moves the correct marker', () => {
  const profile = {
    ...baseProfile,
    phases: [
      { ...baseProfile.phases[0], duration: 0 },
      { ...baseProfile.phases[1], duration: 5 },
      { ...baseProfile.phases[1], name: 'Phase 2', duration: 20 },
    ],
  };

  const result = updateKeyframeSegment(profile, 0, { duration: 7 });

  // Moving marker 1 from t=5 to t=7 sets phase 1 to 7s; phase 2 shrinks to 25-7=18s
  expect(result.profile.phases.map(p => p.duration)).toEqual([0, 7, 18]);
});

test('duration patch on non-setup profile moves the correct marker via segmentIndex=index', () => {
  // Simulates what onPhaseChange does for a no-setup profile at form index 1:
  // hasInitialSetupPhase is false, so segmentIndex = index = 1
  const noSetupProfile = {
    ...baseProfile,
    phases: [
      { ...baseProfile.phases[1], name: 'Phase A', duration: 3 },
      { ...baseProfile.phases[1], name: 'Phase B', duration: 5 },
      { ...baseProfile.phases[1], name: 'Phase C', duration: 20 },
    ],
  };

  // segmentIndex = 1 (form index 1 for no-setup profile)
  const result = updateKeyframeSegment(noSetupProfile, 1, { duration: 7 });

  // Marker 2 moves from t=8 to t=10; Phase B becomes 7s, Phase C shrinks to 28-10=18s
  expect(result.profile.phases.map(p => p.duration)).toEqual([0, 3, 7, 18]);
});

test('updateKeyframeValue patches pressure on marker 1', () => {
  const result = updateKeyframeValue(baseProfile, 1, { pressure: 9.5 });
  expect(result.profile.phases[1].pump.pressure).toBe(9.5);
  expect(result.selectedSegmentIndex).toBe(0);
});

test('updateKeyframeValue patches flow on marker 1', () => {
  const result = updateKeyframeValue(baseProfile, 1, { flow: 7 });
  expect(result.profile.phases[1].pump.flow).toBe(7);
});

test('updateKeyframeValue patches temperature on marker 1', () => {
  const result = updateKeyframeValue(baseProfile, 1, { temperature: 96 });
  expect(result.profile.phases[1].temperature).toBe(96);
});

test('updateKeyframeValue preserves other marker values when patching only pressure', () => {
  const result = updateKeyframeValue(baseProfile, 1, { pressure: 9.5 });
  expect(result.profile.phases[1].pump.flow).toBe(4);
  expect(result.profile.phases[1].temperature).toBe(90);
});

test('updateKeyframeValue returns original profile for out-of-range markerIndex', () => {
  const result = updateKeyframeValue(baseProfile, 99, { pressure: 9.5 });
  expect(result.profile).toBe(baseProfile);
  expect(result.selectedSegmentIndex).toBe(0);
});

test('updateKeyframeValue on marker 0 updates start phase and returns selectedSegmentIndex 0', () => {
  const result = updateKeyframeValue(baseProfile, 0, { pressure: 5 });
  expect(result.profile.phases[0].pump.pressure).toBe(5);
  expect(result.selectedSegmentIndex).toBe(0);
});
