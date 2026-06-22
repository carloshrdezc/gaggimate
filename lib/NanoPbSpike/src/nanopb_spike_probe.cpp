// PRO-239 THROWAWAY footprint probe. Forces the nanopb runtime + the two hot
// generated messages (SensorData, SimpleOutput) to be linked into the display
// image so `pio run -e display-nanopb-spike` reports a realistic flash/RAM delta
// vs the baseline `display` build. NOT production code.
//
// We encode+decode both hot messages into static buffers and stash a result in
// a volatile sink so the optimizer cannot dead-strip the calls. This pulls in
// pb_encode, pb_decode, pb_common and the two message descriptors — the same
// code a real NanoPbComm send/receive path would reference.

#include "comms.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <cstdint>

volatile uint32_t g_nanopb_spike_sink = 0;

extern "C" void nanopb_spike_probe(float t, float p, float pf, float mf, float pr, bool valve, float pump, float boiler) {
    uint8_t buf[64];

    gaggimate_spike_SensorData s = gaggimate_spike_SensorData_init_zero;
    s.temperature = t;
    s.pressure = p;
    s.puck_flow = pf;
    s.pump_flow = mf;
    s.puck_resistance = pr;
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    if (pb_encode(&os, gaggimate_spike_SensorData_fields, &s)) {
        gaggimate_spike_SensorData d = gaggimate_spike_SensorData_init_zero;
        pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
        if (pb_decode(&is, gaggimate_spike_SensorData_fields, &d)) {
            g_nanopb_spike_sink += (uint32_t)d.temperature + os.bytes_written;
        }
    }

    gaggimate_spike_SimpleOutput o = gaggimate_spike_SimpleOutput_init_zero;
    o.valve = valve;
    o.pump_setpoint = pump;
    o.boiler_setpoint = boiler;
    pb_ostream_t os2 = pb_ostream_from_buffer(buf, sizeof(buf));
    if (pb_encode(&os2, gaggimate_spike_SimpleOutput_fields, &o)) {
        gaggimate_spike_SimpleOutput d2 = gaggimate_spike_SimpleOutput_init_zero;
        pb_istream_t is2 = pb_istream_from_buffer(buf, os2.bytes_written);
        if (pb_decode(&is2, gaggimate_spike_SimpleOutput_fields, &d2)) {
            g_nanopb_spike_sink += (uint32_t)d2.boiler_setpoint + os2.bytes_written;
        }
    }
}

// Constructor attribute guarantees the linker keeps the probe (and therefore
// the whole nanopb runtime) — equivalent to it being reachable from a real
// comms call path.
__attribute__((constructor)) static void nanopb_spike_force_link() {
    nanopb_spike_probe(93.4f, 9.1f, 2.7f, 3.1f, 0.0007f, true, 87.6f, 93.3f);
}
