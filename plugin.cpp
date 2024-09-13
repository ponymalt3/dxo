/*
 * plugin.cpp
 *
 *  Created on: Dec 30, 2016
 *      Author: malte
 */

  #include "fftw3.h"

#define PIC

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <alsa/output.h>

#include <stdint.h>

#include <string>
#include <fstream>

#include <alsa/pcm.h>

#if SND_PCM_EXTPLUG_VERSION >= 0x10002
#endif

class DigitalCrossover
{
public:
  DigitalCrossover(uint32_t blockSize) : log_("/home/kodi/dxottt.log",std::ios::app|std::ios::out)
  {
    blockSize_=blockSize;

    inputOffset_=0;
    outputOffset_=0;
    filterLength_=0;
    formatChecked_=false;
  }

  ~DigitalCrossover()
  {
  }

  bool loadFilterData(const std::string &path)
  {

    return true;
  }

  template<typename SampleType,uint32_t NumInputChannel,uint32_t NumOutputChannel>
  uint32_t update(const snd_pcm_channel_area_t *dst_areas,
                  snd_pcm_uframes_t dst_offset,
                  const snd_pcm_channel_area_t *src_areas,
                  snd_pcm_uframes_t src_offset,
                  snd_pcm_uframes_t size,snd_output_t *out)
  {
    if(!formatChecked_)
    {
      //assert that format is interleaved eg
      // cha | chb | chc | chd ... | cha | chb | ...

      formatChecked_=true;
    }

    //snd_pcm_areas_copy(dst_areas,dst_offset,src_areas,src_offset,2,size,SND_PCM_FORMAT_S16_LE);

    uint32_t samples=0;

    uint32_t x=size;

    //snd_output_printf(out,"blockSize: %d size: %d inputOff: %d samples: %d\n",blockSize_,x,inputOffset_,samples);

    //log_<<"call: "<<(size)<<"  selected samples: "<<(samples)<<"\n  blocksize: "<<(blockSize_)<<"\n  "<<(inputOffset_)<<"\n";

      

    //snd_output_printf(out,"  samples: %d\n",samples);
    //snd_output_printf(out,"  inputOffset_+samples: %d\n",inputOffset_+samples);


    snd_pcm_areas_copy(dst_areas,dst_offset,src_areas,src_offset,2,samples,SND_PCM_FORMAT_S16_LE);

    outputOffset_+=samples;

    //log_<<"new inputOffset: "<<(inputOffset_)<<"  outputOffset: "<<(outputOffset_)<<"\n";

    return samples;
  }

protected:
  uint32_t blockSize_;
  uint32_t filterLength_;

  std::fstream log_;

  uint32_t inputOffset_;
  uint32_t outputOffset_;

  bool formatChecked_;
};

struct plugin_t : public snd_pcm_extplug_t
{
  plugin_t()
  {
    memset(this,0,sizeof(snd_pcm_extplug_t));
    dxo_=0;
    output_=0;
  }

  uint32_t blockSize_;
  std::string coeffPath_;
  DigitalCrossover *dxo_;
  snd_output_t *output_;
};

extern "C" {

snd_pcm_sframes_t dxo_transfer(snd_pcm_extplug_t *ext,
            const snd_pcm_channel_area_t *dst_areas,
            snd_pcm_uframes_t dst_offset,
            const snd_pcm_channel_area_t *src_areas,
            snd_pcm_uframes_t src_offset,
            snd_pcm_uframes_t size)
{
  plugin_t *plugin=(plugin_t*)ext;

  uint32_t remainingSamples=size;

  do
  {
    uint32_t samplesProcessed=plugin->dxo_->update<int16_t,2,8>(dst_areas,dst_offset,src_areas,src_offset,remainingSamples,plugin->output_);
    remainingSamples-=samplesProcessed;
    dst_offset+=samplesProcessed;
    src_offset+=samplesProcessed;
  }
  while(remainingSamples > 0);

  return size;
}

int dxo_init(snd_pcm_extplug_t *ext)
{
  plugin_t *plugin=(plugin_t*)ext;
  snd_output_printf(plugin->output_,"dxo_init\n");
  snd_output_flush(plugin->output_);

  uint32_t sampleRate=plugin->rate;
  snd_output_printf(plugin->output_,"samplerate: %d\n",sampleRate);

  if(plugin->dxo_ != 0)
  {
    //delete plugin->dxo_;
  }

  plugin->dxo_=new DigitalCrossover(plugin->blockSize_);

  plugin->dxo_->loadFilterData(plugin->coeffPath_);

  return 0;
}

int dxo_close(snd_pcm_extplug_t *ext)
{
  plugin_t *plugin=(plugin_t*)ext;
  snd_output_printf(plugin->output_,"dxo_close\n");
  snd_output_flush(plugin->output_);

  if(plugin->dxo_)
  {
    delete plugin->dxo_;
    plugin->dxo_=0;
  }

  delete plugin;

  return 0;
}

static const snd_pcm_extplug_callback_t callbacks=
{
  dxo_transfer,
  dxo_close,
  0,
  0,
  0,
  dxo_init,
  0,
  0,
  0
};

SND_PCM_PLUGIN_DEFINE_FUNC(dxo)
{
  snd_config_iterator_t i,next;

  long int channels=0;
  long int blockSize=4096;
  std::string coeffPath="dxo";
  snd_config_t *slaveConfig=0;

  snd_config_for_each(i,next,conf)
  {
    snd_config_t *config=snd_config_iterator_entry(i);
    const char *id;
    snd_config_get_id(config,&id);

    std::string param(id);

    if(param == "channels")
    {
      snd_config_get_integer(config,&channels);
      continue;
    }

    if(param == "slave")
    {
      slaveConfig=config;
      continue;
    }

    if(param == "blocksize")
    {
      snd_config_get_integer(config,&blockSize);
      continue;
    }

    if(param == "path")
    {
      const char *path;
      snd_config_get_string(config, &path);
      coeffPath=path;
      continue;
    }
  }

  if(slaveConfig == 0)
  {
    return -EINVAL;
  }

  plugin_t *plugin = new plugin_t();

  plugin->callback=&callbacks;
  plugin->version=SND_PCM_EXTPLUG_VERSION;
  plugin->name="alsadxo";

  plugin->blockSize_=blockSize;
  //plugin->coeffPath_=coeffPath;

  snd_output_stdio_attach(&(plugin->output_),stdout,0);
  snd_output_printf(plugin->output_,"slave: %d\n",reinterpret_cast<int>(slaveConfig));

  int32_t result=snd_pcm_extplug_create(plugin,name,root,slaveConfig,stream,mode);

  snd_output_printf(plugin->output_,"result: %d\n",result);

  if(result < 0)
  {
    return result;
  }

  snd_pcm_extplug_set_param_minmax(plugin,SND_PCM_EXTPLUG_HW_CHANNELS,1,2);
  snd_pcm_extplug_set_slave_param(plugin,SND_PCM_EXTPLUG_HW_CHANNELS,8);

  static uint32_t formats[]={SND_PCM_FORMAT_S16_LE,SND_PCM_FORMAT_S32_LE};

  snd_pcm_extplug_set_param_list(plugin,SND_PCM_EXTPLUG_HW_FORMAT,2,formats);
  snd_pcm_extplug_set_slave_param_list(plugin,SND_PCM_EXTPLUG_HW_FORMAT,2,formats);

  snd_output_printf(plugin->output_,"ok\n");
  snd_output_flush(plugin->output_);

  *pcmp=plugin->pcm;

  return 0;
}

SND_PCM_PLUGIN_SYMBOL(dxo);

}

int main()
{
    const int N = 1024;
    fftwf_complex *in, *out;
    fftwf_plan p;
    in = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * N);
    out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * N);
    p = fftwf_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    fftwf_execute(p); /* repeat as needed */
    fftwf_destroy_plan(p);
    fftwf_free(in); fftwf_free(out);
  return 0;
}
