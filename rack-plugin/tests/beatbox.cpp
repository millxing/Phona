// Headless tests of the real module, including its worker and patch serialization.
#include "../src/Phona.cpp"
#include <cassert>
#include <iostream>
Plugin* pluginInstance = nullptr;
Model* modelPhonaLab = nullptr;
int main(int argc, char** argv) {
    Context context;
    context.engine = new engine::Engine;
    contextSet(&context);
    Phona m;
    float rate = argc > 1 ? std::stof(argv[1]) : 48000.f;
    Module::ProcessArgs args{rate, 1.f/rate, 0};
    auto tick = [&](int n) { for (int i=0;i<n;i++) m.process(args); };
    auto ready = [&]() {
        for (int i=0;i<2000;i++) {
            tick(16);
            if (m.active && m.activeLyr && m.activeLyr->version == m.lyricsVersion.load()
                && m.activeLyr->body == m.active->body && m.active->body == (int)m.params[Phona::BODY_PARAM].getValue() && m.active->rate == rate) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(false && "worker timeout");
    };
    m.inputs[Phona::GATE_INPUT].channels = 1;
    m.inputs[Phona::RESET_INPUT].channels = 1;
    m.setBeatMode(true);
    m.setLyrics("B Pf t . bogus");
    ready();
    auto hit = [&](const char* expected) {
        m.inputs[Phona::GATE_INPUT].setVoltage(10.f); tick(1);
        m.inputs[Phona::GATE_INPUT].setVoltage(0.f); tick(100);
        if (m.shownSyllable() != expected) std::cerr << "expected " << expected << " got " << m.shownSyllable() << " next " << m.syl << "\n";
        assert(m.shownSyllable() == expected);
    };
    for (const char* s : {"B","Pf","t",".","?","B"}) hit(s);
    // A reset arriving inside the existing 1ms gate delay selects B, then Pf.
    for (int delay : {0,2,30}) {
        m.inputs[Phona::GATE_INPUT].setVoltage(10.f); tick(delay);
        m.inputs[Phona::RESET_INPUT].setVoltage(10.f); tick(1);
        m.inputs[Phona::GATE_INPUT].setVoltage(0.f);
        m.inputs[Phona::RESET_INPUT].setVoltage(0.f); tick(100);
        assert(m.shownSyllable()=="B"); hit("Pf");
    }
    // Every edge advances, even several edges inside the 1ms delay.
    for (int i=0;i<3;i++) {
        m.inputs[Phona::GATE_INPUT].setVoltage(10.f); tick(1);
        m.inputs[Phona::GATE_INPUT].setVoltage(0.f); tick(1);
    }
    tick(100); assert(m.shownSyllable()=="?");
    hit("B"); hit("Pf");
    // Held gate advances only once.
    m.inputs[Phona::GATE_INPUT].setVoltage(10.f); tick(24000);
    assert(m.shownSyllable()=="t");
    m.inputs[Phona::GATE_INPUT].setVoltage(0.f); tick(100);
    // Patch recall saves shared text and mode. Legacy JSON selects lyrics.
    json_t* saved=m.dataToJson();
    m.setBeatMode(false); m.setLyrics(""); m.dataFromJson(saved); json_decref(saved);
    assert(m.beatMode && m.getLyrics()=="B Pf t . bogus");
    ready();
    m.params[Phona::BODY_PARAM].setValue(1.f); ready(); hit("B");
    m.setLyrics(""); tick(1);
    assert(m.outputs[Phona::AUDIO_OUTPUT].getVoltage()==0.f);
    ready(); hit("--"); tick(48000);
    assert(m.outputs[Phona::AUDIO_OUTPUT].getVoltage()==0.f);
    // Mode switch while high must not launch a hit. Requires low then rising edge.
    m.setBeatMode(false); tick(100);
    m.inputs[Phona::GATE_INPUT].setVoltage(10.f);
    m.setLyrics("B"); m.setBeatMode(true); ready(); tick(100);
    assert(m.shownSyllable()=="--");
    m.inputs[Phona::GATE_INPUT].setVoltage(0.f); tick(100); hit("B");
    json_t* old=json_object(); m.dataFromJson(old); json_decref(old); assert(!m.beatMode);
    m.onReset(Module::ResetEvent{}); assert(!m.beatMode && m.getLyrics().empty());
    std::cout << "beatbox module regression passed\n";
}
