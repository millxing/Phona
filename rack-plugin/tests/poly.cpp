// Headless tests of polyphony in the real module: voice count, poly output, shared lyrics
// syllables (a chord sings one syllable), mono beatbox, patch recall, and CPU per voice.
#include "../src/Phona.cpp"
#include <cassert>
#include <iostream>
Plugin* pluginInstance = nullptr;
Model* modelPhonaLab = nullptr;
Model* modelPhonaView = nullptr;
Model* modelPhonaScope = nullptr;
int main(int argc, char** argv) {
    Context context;
    context.engine = new engine::Engine;
    contextSet(&context);
    Phona m;
    float rate = argc > 1 ? std::stof(argv[1]) : 48000.f;
    Module::ProcessArgs args{rate, 1.f/rate, 0};
    auto tick = [&](int n) { for (int i=0;i<n;i++) m.process(args); };
    auto ready = [&]() {
        for (int i=0;i<4000;i++) {
            tick(16);
            if (m.active && m.active->n == m.polyMax.load() && m.active->rate == rate
                && m.activeLyr && m.activeLyr->version == m.lyricsVersion.load()
                && m.activeLyr->body == m.active->body) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(false && "worker timeout");
    };
    Input& voct = m.inputs[Phona::VOCT_INPUT];
    Input& gate = m.inputs[Phona::GATE_INPUT];
    Output& out = m.outputs[Phona::AUDIO_OUTPUT];
    auto setGates = [&](int n, float v) { for (int c=0;c<n;c++) gate.setVoltage(v, c); };

    // Default: one voice. A 3-channel V/OCT cable still gives one output channel.
    // Rack ignores setChannels on an unconnected port: mark the cables connected.
    voct.channels = 3; gate.channels = 3; out.channels = 1;
    for (int c=0;c<3;c++) voct.setVoltage(c * 0.25f, c);
    ready();
    assert(m.polyMax == 1 && m.active->n == 1);
    tick(64); assert(out.getChannels() == 1);

    // Max 4: the cable sets 3 voices; each sounds.
    m.setPolyMax(4); ready();
    assert(m.active->n == 4);
    setGates(3, 10.f);
    float peak[4] = {};
    for (int i=0;i<(int)(rate*0.5f);i++) {
        tick(1);
        for (int c=0;c<4;c++) peak[c] = std::max(peak[c], std::fabs(out.getVoltage(c)));
    }
    assert(out.getChannels() == 3);
    for (int c=0;c<3;c++) { std::cout << "voice " << c+1 << " peak " << peak[c] << " V\n"; assert(peak[c] > 0.1f); }
    assert(peak[3] == 0.f);
    setGates(3, 0.f); tick((int)(rate*0.2f));

    // CPU: 3 voices with open gates, per-sample calls as Rack makes them
    setGates(3, 10.f);
    {
        int n = (int) rate; // 1 s of audio
        auto t0 = std::chrono::steady_clock::now();
        tick(n);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "3 voices: " << 100.0 * s << "% of one core (real time)\n";
    }
    setGates(3, 0.f); tick((int)(rate*0.2f));

    // Lyrics: a chord (gates opening together, or within 20 ms) sings one syllable.
    m.setLyrics("doh ray mee fah"); ready(); tick(64);
    auto chordAt = [&](int n, int staggerSamples) {
        for (int c=0;c<n;c++) { gate.setVoltage(10.f, c); tick(staggerSamples); }
        tick((int)(rate*0.05f));
    };
    chordAt(3, 0);
    assert(m.syl == 1 && m.chordMask == 0x7 && m.sylShown == 0);
    setGates(3, 0.f); tick((int)(rate*0.05f));
    chordAt(3, (int)(rate*0.005f)); // 5 ms apart: still one chord
    assert(m.syl == 2 && m.chordMask == 0x7 && m.sylShown == 1);
    setGates(3, 0.f); tick((int)(rate*0.05f));
    // One voice twice inside the window: two syllables (a voice cannot join its own chord).
    gate.setVoltage(10.f, 0); tick((int)(rate*0.003f));
    gate.setVoltage(0.f, 0); tick((int)(rate*0.003f));
    gate.setVoltage(10.f, 0); tick((int)(rate*0.003f));
    assert(m.syl == 0 && m.sylShown == 3); // syllables 2 and 3 (wraps to 0)
    gate.setVoltage(0.f, 0); tick((int)(rate*0.05f));
    // Reset: the next chord starts at the first syllable.
    m.inputs[Phona::RESET_INPUT].setVoltage(10.f); tick(1); m.inputs[Phona::RESET_INPUT].setVoltage(0.f);
    chordAt(2, 0);
    assert(m.sylShown == 0 && m.syl == 1);
    setGates(3, 0.f); tick((int)(rate*0.05f));

    // Beatbox stays mono: one output channel, voice 1 only.
    m.setBeatMode(true); m.setLyrics("B t"); ready(); tick(64);
    assert(out.getChannels() == 1);
    gate.setVoltage(10.f, 0); tick(1); gate.setVoltage(0.f, 0); tick(200);
    assert(m.shownSyllable() == "B");
    m.setBeatMode(false); m.setLyrics(""); ready();

    // Fewer channels than built: output follows the cable.
    voct.setChannels(2); gate.setChannels(2); tick(64);
    assert(out.getChannels() == 2);

    // Patch recall; old patches (no "voices") load mono.
    json_t* saved = m.dataToJson();
    m.setPolyMax(1); m.dataFromJson(saved); json_decref(saved);
    assert(m.polyMax == 4);
    json_t* old = json_object(); json_object_set_new(old, "lyrics", json_string(""));
    m.dataFromJson(old); json_decref(old);
    assert(m.polyMax == 1);
    ready(); assert(m.active->n == 1);
    std::cout << "poly module test passed\n";
    return 0;
}
