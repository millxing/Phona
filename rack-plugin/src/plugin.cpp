#include "plugin.hpp"
Plugin* pluginInstance;
void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelPhona);
	p->addModel(modelPhonaLab);
	p->addModel(modelPhonaView);
	p->addModel(modelPhonaScope);
}
