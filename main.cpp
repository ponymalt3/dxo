#include <dlfcn.h>

#include <iostream>

int main()
{
  auto lib = dlopen("./libasound_module_pcm_dxo.so", RTLD_NOW);

  if(lib == nullptr)
  {
    std::cout << "Error loading lib " << (dlerror()) << std::endl;
  }

  return 0;
}