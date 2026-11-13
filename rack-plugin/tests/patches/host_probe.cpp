// Offline Rack module probe. Never creates an audio device, window, or Rack user folder.
#include <rack.hpp>
#include <dlfcn.h>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace rack;
namespace rack { namespace core { extern plugin::Model* modelNotes; } }
int main(int argc, char** argv) {
 try {
  if (argc < 3) throw std::runtime_error("usage: host_probe PLUGIN_DIR MODEL [MODEL ...]");
  Context context; context.engine = new engine::Engine; contextSet(&context);
  auto p = new plugin::Plugin;
  p->path = argv[1];
  auto lib = dlopen((p->path + "/plugin.dylib").c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!lib) throw std::runtime_error(dlerror());
  auto init = reinterpret_cast<void(*)(plugin::Plugin*)>(dlsym(lib, "init"));
  if (!init) throw std::runtime_error("missing init");
  init(p);
  json_t* result = json_object();
  for (int a = 2; a < argc; ++a) {
   auto model = std::string(argv[a]) == "Notes" ? core::modelNotes : p->getModel(argv[a]);
   if (!model) throw std::runtime_error("missing model");
   std::unique_ptr<engine::Module> m(model->createModule());
   auto j = json_object(); auto params = json_array();
   for (size_t i=0;i<m->params.size();i++) {
    auto q=m->getParamQuantity(i);
    json_array_append_new(params,json_pack("{s:i,s:s,s:f,s:f,s:f}","id",int(i),"name",q?q->name.c_str():"", "min",q?q->minValue:0.,"max",q?q->maxValue:0.,"default",double(m->params[i].getValue())));
   }
   json_object_set_new(j,"params",params);
   for (bool input : {true,false}) {
    auto ports=json_array(); auto& infos=input?m->inputInfos:m->outputInfos;
    for (size_t i=0;i<infos.size();i++) json_array_append_new(ports,json_pack("{s:i,s:s}","id",int(i),"name",infos[i]?infos[i]->name.c_str():""));
    json_object_set_new(j,input?"inputs":"outputs",ports);
   }
   auto data=m->dataToJson(); if(data) json_object_set_new(j,"data",data);
   json_object_set_new(result,argv[a],j);
  }
  char* s=json_dumps(result,JSON_INDENT(2)); std::cout<<s<<"\n"; free(s); json_decref(result);
  // Keep plugin/model registration alive until process exit.
  return 0;
 } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
