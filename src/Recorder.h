#pragma once
// ============================================================
//  Recorder
//  Captures audio output to a 16-bit stereo WAV file.
//
//  Audio thread:  calls push(sample) every output sample.
//  Main thread:   calls start()/stop(), polls drain() to
//                 flush captured samples to disk.
//
//  Ring buffer size = 2 seconds @ 44100 = 88200 floats.
//  drain() is cheap — just memcpy + file write per call.
//  Call drain() from update() (main thread, 60fps).
// ============================================================

#include <vector>
#include <atomic>
#include <string>
#include <fstream>
#include <cstring>
#include <cmath>

namespace ChaosOsc {

class Recorder {
public:
    static constexpr int RING_SIZE = 88200 * 2;  // 2s stereo floats

    void init() {
        ring.resize(RING_SIZE, 0.f);
        writePos.store(0);
        readPos = 0;
        recording.store(false);
        totalSamples = 0;
    }

    // Start recording — opens file, writes placeholder header
    bool start(const std::string& filePath, double sampleRate, int numChannels = 2) {
        if (recording.load()) return false;
        path = filePath;
        sr   = sampleRate;
        nCh  = numChannels;
        totalSamples = 0;
        readPos = writePos.load();  // start capturing from now

        outFile.open(path, std::ios::binary);
        if (!outFile.is_open()) return false;

        writeWavHeader(0);  // placeholder — will rewrite on stop
        recording.store(true);
        return true;
    }

    // Stop recording — finalises WAV header
    void stop() {
        if (!recording.load()) return;
        recording.store(false);
        drain();  // flush remaining
        // Rewrite header with final sample count
        outFile.seekp(0);
        writeWavHeader(totalSamples);
        outFile.close();
    }

    // Called from AUDIO THREAD — push one interleaved stereo pair
    void push(float left, float right) {
        if (!recording.load()) return;
        int wp = writePos.load(std::memory_order_relaxed);
        ring[wp % RING_SIZE] = left;
        ring[(wp + 1) % RING_SIZE] = right;
        writePos.fetch_add(2, std::memory_order_release);
    }

    // Called from MAIN THREAD (update) — drain buffered samples to file
    void drain() {
        if (!outFile.is_open()) return;
        int wp = writePos.load(std::memory_order_acquire);
        while (readPos != wp) {
            float s = ring[readPos % RING_SIZE];
            // Convert to 16-bit
            int16_t s16 = (int16_t)std::max(-32768.f, std::min(32767.f, s * 32767.f));
            outFile.write(reinterpret_cast<char*>(&s16), 2);
            totalSamples++;
            readPos++;
        }
    }

    bool isRecording() const { return recording.load(); }
    int  getSamples()  const { return totalSamples; }
    double getDurationSec() const { return (nCh > 0 && sr > 0) ? totalSamples / (double)(nCh * sr) : 0.0; }

private:
    void writeWavHeader(int numSamples) {
        int byteRate    = (int)sr * nCh * 2;
        int blockAlign  = nCh * 2;
        int dataSize    = numSamples * 2;  // 16-bit
        int chunkSize   = 36 + dataSize;

        auto w4 = [&](const char* s)         { outFile.write(s, 4); };
        auto wu = [&](uint32_t v)             { outFile.write(reinterpret_cast<char*>(&v), 4); };
        auto ws = [&](uint16_t v)             { outFile.write(reinterpret_cast<char*>(&v), 2); };

        w4("RIFF");  wu(chunkSize);  w4("WAVE");
        w4("fmt ");  wu(16);  ws(1);  ws((uint16_t)nCh);
        wu((uint32_t)sr);  wu((uint32_t)byteRate);  ws((uint16_t)blockAlign);  ws(16);
        w4("data");  wu((uint32_t)dataSize);
    }

    std::vector<float>  ring;
    std::atomic<int>    writePos {0};
    int                 readPos  = 0;
    std::atomic<bool>   recording {false};
    std::ofstream       outFile;
    std::string         path;
    double              sr  = 44100.0;
    int                 nCh = 2;
    int                 totalSamples = 0;
};

} // namespace ChaosOsc
