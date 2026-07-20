#include "audio/bgm_data.hpp"

#include "pixel_twins/sound_waves.hpp"

namespace wizward::audio {

using pixel_twins::Envelope;
using pixel_twins::SequenceInstrument;
using pixel_twins::Timbre;

const SequenceInstrument kBgmInstruments[12]{
    {Timbre{&pixel_twins::kStandardWaves.triangle, Envelope{0.004F, 0.05F, 0.68F, 0.045F}, 0.22F, 0.0F}},
    {Timbre{&pixel_twins::kStandardWaves.bell, Envelope{0.012F, 0.08F, 0.50F, 0.09F}, 0.13F, -0.22F}},
    {Timbre{&pixel_twins::kStandardWaves.thinPulse, Envelope{0.004F, 0.045F, 0.66F, 0.055F}, 0.16F, 0.18F}},
    {Timbre{&pixel_twins::kStandardWaves.thinPulse, Envelope{0.004F, 0.045F, 0.66F, 0.055F}, 0.08F, -0.28F}, 1.00405F},
    {Timbre{&pixel_twins::kStandardWaves.bell, Envelope{0.002F, 0.11F, 0.32F, 0.16F}, 0.14F, -0.1F}},
    {Timbre{&pixel_twins::kStandardWaves.sine, Envelope{0.001F, 0.075F, 0.08F, 0.05F}, 0.7981F}, 1.0F, 72.0F, 0.53F, 0.09F},
    {Timbre{&pixel_twins::kStandardWaves.noise, Envelope{0.001F, 0.055F, 0.12F, 0.055F}, 0.26F}, 1.0F, 180.0F, 1.0F, 0.0F, 3, 0.17F, 180.0F, 105.0F, 0.05F, 0.065F},
    {Timbre{&pixel_twins::kStandardWaves.noise, Envelope{0.001F, 0.022F, 0.04F, 0.03F}, 0.10F}, 1.0F, 2200.0F, 1.0F, 0.0F, 1},
    {Timbre{&pixel_twins::kStandardWaves.noise, Envelope{0.001F, 0.022F, 0.04F, 0.03F}, 0.10F}, 1.0F, 3200.0F, 1.0F, 0.0F, 1},
    {Timbre{&pixel_twins::kStandardWaves.noise, Envelope{0.001F, 0.035F, 0.08F, 0.04F}, 0.2594F}, 1.0F, 140.0F, 1.0F, 0.0F, 2},
    {Timbre{&pixel_twins::kStandardWaves.noise, Envelope{0.001F, 0.035F, 0.08F, 0.04F}, 0.2594F}, 1.0F, 680.0F, 1.0F, 0.0F, 2},
    {Timbre{&pixel_twins::kStandardWaves.noise, Envelope{0.001F, 0.035F, 0.08F, 0.04F}, 0.2594F}, 1.0F, 240.0F, 1.0F, 0.0F, 2},
};

} // namespace wizward::audio
