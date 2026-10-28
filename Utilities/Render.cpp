#include "Engine.h"
#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

int main (int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "Usage: FractureRender input.wav output-directory start-seconds\n";
        return 1;
    }

    juce::WavAudioFormat wav;
    const juce::File input (argv[1]);
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (new juce::FileInputStream (input), true));
    if (reader == nullptr || reader->numChannels < 1 || reader->numChannels > 2)
    {
        std::cerr << "Input must be a mono or stereo WAV.\n";
        return 1;
    }

    constexpr int seconds = 8;
    constexpr int blockSize = 512;
    const auto start = (juce::int64) (std::atof (argv[3]) * reader->sampleRate);
    const auto samples = (int) (seconds * reader->sampleRate);
    if (start < 0 || start + samples > reader->lengthInSamples)
    {
        std::cerr << "Requested section is outside the input.\n";
        return 1;
    }

    const auto channels = (int) reader->numChannels;
    juce::AudioBuffer<float> source (channels, samples);
    if (! reader->read (&source, 0, samples, start, true, channels > 1))
        return 1;
    const auto peak = source.getMagnitude (0, samples);
    if (peak > 0.0f)
        source.applyGain (0.5f / peak);

    const juce::File outputDir (argv[2]);
    if (! outputDir.createDirectory().wasOk())
        return 1;

    for (const auto protection : { -1.0f, 0.0f, 0.7f })
    {
        GlitchEngine engine;
        engine.prepare (reader->sampleRate, channels);
        engine.setRandomSeed (1);
        for (const auto [step, lane] : { std::pair { 4, GlitchEngine::reverse }, { 6, GlitchEngine::gate },
                                         { 8, GlitchEngine::repeat }, { 10, GlitchEngine::tapeStop },
                                         { 12, GlitchEngine::shuffle } })
        {
            engine.setStep (0, step, lane);
            engine.setStep (0, step + 1, GlitchEngine::Step { lane, true });
        }

        auto result = juce::AudioBuffer<float> (channels, samples);
        juce::AudioBuffer<float> block (channels, blockSize);
        GlitchEngine::Timing timing;
        timing.running = true;
        timing.bpm = 120.0;
        timing.stepBeats = 0.25;
        GlitchEngine::Settings settings;

        for (int offset = 0; offset < samples; offset += blockSize)
        {
            const auto count = juce::jmin (blockSize, samples - offset);
            for (int ch = 0; ch < channels; ++ch)
                block.copyFrom (ch, 0, source, ch, offset, count);
            timing.ppq = (double) offset * timing.bpm / (60.0 * reader->sampleRate);
            if (protection >= 0.0f)
                engine.process (block, timing, settings, 1.0f, protection);
            for (int ch = 0; ch < channels; ++ch)
                result.copyFrom (ch, offset, block, ch, 0, count);
        }

        const auto name = protection < 0.0f ? "dry.wav" : protection == 0.0f ? "fracture.wav" : "fracture-protect.wav";
        auto stream = std::unique_ptr<juce::OutputStream> (outputDir.getChildFile (name).createOutputStream());
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                  .withSampleRate (reader->sampleRate)
                                                  .withNumChannels (channels)
                                                  .withBitsPerSample (24));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (result, 0, samples))
            return 1;
    }

    return 0;
}
