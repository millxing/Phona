// Runs actual installed modules offline. No UI, audio device, or user state access.
#include <rack.hpp>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <thread>
#include <chrono>
#include <stdexcept>
using namespace rack;
namespace rack { namespace core { extern plugin::Model* modelNotes; } }
void require(bool yes, const char* message) { if(!yes) throw std::runtime_error(message); }
json_t* read(const std::string& path) { json_error_t e; auto j=json_load_file(path.c_str(),0,&e); require(j,e.text); return j; }
plugin::Plugin* load(const std::string& dir) {
 auto p=new plugin::Plugin; p->path=dir; auto manifest=read(dir+"/plugin.json"); p->slug=json_string_value(json_object_get(manifest,"slug")); p->version=json_string_value(json_object_get(manifest,"version"));
 p->handle=dlopen((dir+"/plugin.dylib").c_str(),RTLD_NOW|RTLD_LOCAL); if(!p->handle) throw std::runtime_error(dlerror());
 auto init=reinterpret_cast<void(*)(plugin::Plugin*)>(dlsym(p->handle,"init")); require(init,"init missing"); init(p);
 json_decref(manifest); plugin::plugins.push_back(p); return p;
}
int main(int argc,char**argv) { try {
 require(argc==4,"usage: host_smoke FUNDAMENTAL_DIR PHONA_DIR PATCH.vcv");
 Context context; context.engine=new engine::Engine; contextSet(&context);
 auto corePlugin=new plugin::Plugin; corePlugin->slug="Core"; corePlugin->version="2.6.6"; corePlugin->addModel(core::modelNotes); plugin::plugins.push_back(corePlugin);
 auto fundamental=load(argv[1]); auto phona=load(argv[2]); auto patch=read(argv[3]);
 std::map<int,std::unique_ptr<engine::Module>> modules;
 size_t i; json_t* j;
 json_array_foreach(json_object_get(patch,"modules"),i,j) {
  std::string name=json_string_value(json_object_get(j,"model"));
  auto model=name=="Notes"?core::modelNotes:(name=="Phona"?phona:fundamental)->getModel(name);
  require(model,"model missing"); auto m=model->createModule(); m->fromJson(j);
  // Engine normally sets connection state; setChannels() cannot connect a port.
  for(auto& out:m->outputs) out.channels=1;
  auto saved=m->toJson(); auto wanted=json_object_get(j,"data");
  if(wanted) require(json_equal(wanted,json_object_get(saved,"data")),"module data recall differs");
  size_t k; json_t* v; json_array_foreach(json_object_get(j,"params"),k,v) {
   int id=json_integer_value(json_object_get(v,"id")); float value=json_number_value(json_object_get(v,"value"));
   require(id>=0 && id<int(m->params.size()),"param out of range");
   require(std::abs(m->params[id].getValue()-value)<1e-6,"param recall differs");
  }
  json_decref(saved); modules[json_integer_value(json_object_get(j,"id"))].reset(m);
 }
 struct Wire{engine::Module*src;int out;engine::Module*dst;int in;}; std::vector<Wire>wires;
 json_array_foreach(json_object_get(patch,"cables"),i,j) {
  auto src=modules.at(json_integer_value(json_object_get(j,"outputModuleId"))).get();
  auto dst=modules.at(json_integer_value(json_object_get(j,"inputModuleId"))).get();
  int out=json_integer_value(json_object_get(j,"outputId")),in=json_integer_value(json_object_get(j,"inputId"));
  require(out>=0&&out<int(src->outputs.size())&&in>=0&&in<int(dst->inputs.size()),"port out of range");
  dst->inputs[in].channels=1; wires.push_back({src,out,dst,in});
 }
 auto seq=modules.at(1).get();auto voice=modules.at(2).get(); auto vca=modules.at(5).get();
 engine::Module::ProcessArgs args; args.sampleRate=48000;args.sampleTime=1.f/48000;args.frame=0;
 double energy=0,peak=0; int edges=0; bool last=false; std::vector<float> notes;
 auto tick=[&](){
  // Transfer previous sample first, as Rack cables do.
  for(auto&w:wires) w.dst->inputs[w.in].setVoltage(w.src->outputs[w.out].getVoltage());
  for(auto&entry:modules) entry.second->process(args);
  args.frame++;
  float x=voice->outputs[0].getVoltage();require(std::isfinite(x),"nonfinite voice");
  require(std::abs(x)<=10.01,"voice exceeds soft-clip bound");require(vca->outputs[0].getVoltage()==0,"muted VCA leaked");
  require(voice->lights[1].getBrightness()==0,"Phona fault light");
  return x;
 };
 // Give Phona's worker time to build; keep sequencer stopped.
 for(int b=0;b<100;b++){ for(int s=0;s<480;s++) tick(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
 require(seq->outputs[0].getVoltage()==0,"stopped sequencer emits trigger");
 require(voice->inputs[12].getVoltage()==0,"fixture starts with open gate");
 seq->params[2].setValue(1);tick();seq->params[2].setValue(0);tick();
 seq->params[1].setValue(1);tick();seq->params[1].setValue(0);
 for(int n=0;n<48000*17;n++) {
  float x=tick(); energy+=x*x; peak=std::max(peak,double(std::abs(x)));
  bool gate=voice->inputs[12].getVoltage()>0.1f;
  if(gate&&!last){edges++;notes.push_back(voice->inputs[0].getVoltage());}last=gate;
  if(n%4800==0)std::this_thread::yield();
 }
 require(energy>1,"voice stayed silent");
 bool held=std::string(argv[3]).find("held")!=std::string::npos;
 require(edges==(held?5:17),"unexpected gate count over 17 seconds");

 const float expected[]={0,2.f/12,4.f/12,5.f/12,7.f/12,9.f/12,11.f/12,1};
 if(!held) for(size_t k=0;k<notes.size();k++) require(std::abs(notes[k]-expected[(k+1)%8])<1e-5,"pitch/trigger sequence differs");
 if(held) for(size_t k=0;k<notes.size();k++) require(std::abs(notes[k]-(k%2?7.f/12:2.f/12))<1e-5,"held-gate pitch sequence differs");
 std::cout<<"PASS recall, finite output, fault light, zero VCA, gate/pitch sequence: edges="<<edges<<" peak="<<peak<<" V RMS="<<sqrt(energy/(48000*17))<<" V\n";
 // One-sample retriggers and reset offsets: audio/fault checks, not syllable identity.
 wires.clear();
 for(int offset : {0,2,30,60}) {
  voice->inputs[12].setVoltage(0);voice->inputs[16].setVoltage(0);
  for(int n=0;n<48000;n++)tick();
  double burst=0;
  for(int n=0;n<24000;n++) {
   voice->inputs[12].setVoltage(n==0?10:0);
   voice->inputs[16].setVoltage(n==offset?10:0);
   float x=tick();burst+=x*x;
  }
  require(burst>0.01,"one-sample gate produced no audio");
 }
 std::cout<<"PASS one-sample gates with reset offsets 0/2/30/60 samples (audio/fault only)\n";
 // Body swap under a held gate, then slow CV and external excitation.
 voice->inputs[12].setVoltage(10);voice->params[13].setValue(1);
 for(int b=0;b<100;b++){for(int s=0;s<480;s++)tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 auto empty=json_pack("{s:s}","lyrics","");voice->dataFromJson(empty);json_decref(empty);
 for(int b=0;b<50;b++){for(int s=0;s<480;s++)tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 voice->params[14].setValue(0.5);voice->params[15].setValue(0.25);voice->inputs[14].channels=1;
 energy=0;
 for(int n=0;n<48000*4;n++) {
  voice->inputs[4].setVoltage(5*std::sin(2*M_PI*n/48000/4));voice->inputs[5].setVoltage(5*std::sin(2*M_PI*n/48000/4));
  voice->inputs[14].setVoltage(5*(2*std::fmod(n*130.8128/48000,1.)-1));
  float x=tick();energy+=x*x;
 }
 require(energy>1,"external path silent");voice->inputs[12].setVoltage(0);
 float tail=0;
 for(int n=0;n<48000*3;n++) {
  // Keep excitation active: silence must come from gate closure, not a stopped source.
  voice->inputs[14].setVoltage(5*(2*std::fmod(n*130.8128/48000,1.)-1));
  float x=tick(); if(n>=48000*2)tail=std::max(tail,std::abs(x));
 }
 std::cout<<"Closed external gate tail peak="<<tail<<" V\n";
 require(tail<0.001,"closed external gate did not decay");
 std::cout<<"PASS body-switch/CV/external smoke; closed-gate final-second peak="<<tail<<" V (no listening or CPU claim)\n";
 json_decref(patch);return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
