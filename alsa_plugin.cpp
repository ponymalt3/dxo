
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

#include <stdint.h>
#include <string>
#include <fstream>

#include "fftw3.h"
#include "pcm_stream.h"

/*


#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <alsa/output.h>


*/

/*
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
};*/


class AlsaPluginDxO : public snd_pcm_extplug_t
{
public:
  AlsaPluginDxO()
  {
    memset(this, 0, sizeof(snd_pcm_extplug_t));

    for(auto &i : inputs)
    {
      i = new float [bufferSize_];
    }

    for(auto &o : outputs)
    {
      o = new float [bufferSize_];
    }
  }

  ~AlsaPluginDxO()
  {
    for(auto &i : inputs)
    {
      delete [] i;
    }

    for(auto &o : outputs)
    {
      delete [] o;
    }
  }

  template<typename _InputSampleType, bool _HasLfeChannel>
  uint32_t update(const snd_pcm_channel_area_t *dst_areas,
            snd_pcm_uframes_t dst_offset,
            const snd_pcm_channel_area_t *src_areas,
            snd_pcm_uframes_t src_offset,
            snd_pcm_uframes_t size)
  {
    PcmStream<_InputSampleType> dst(dst_areas, dst_offset);
    PcmStream<_InputSampleType> src(src_areas, src_offset);

    snd_output_printf(output_, "T %d\n", x);
    x += size;

    if(_HasLfeChannel)
    {
      src.extractInterleaved(128, inputs[0], inputs[1], inputs[2]);
    }
    else
    {
      src.extractInterleaved(128, inputs[0], inputs[1]);

      for(uint32_t i = 0; i < bufferSize_; ++i)
      {
        inputs[2][i] = (inputs[0][i] + inputs[1][i]) / 2;
      }
    }

    dst.loadInterleaved(128, inputs[0], inputs[1], inputs[0], inputs[1], inputs[2], inputs[2], inputs[0], inputs[1]);

    return 128;
  }
  uint32_t x{0};
  uint32_t bufferSize_{128};
  uint32_t blockSize_{0};
  std::string coeffPath_{};
  //DigitalCrossover *dxo_{nullptr};
  snd_output_t *output_{nullptr};
  float *inputs[3];
  float *outputs[8];
};

extern "C" {

snd_pcm_sframes_t dxo_transfer(snd_pcm_extplug_t *ext,
            const snd_pcm_channel_area_t *dst_areas,
            snd_pcm_uframes_t dst_offset,
            const snd_pcm_channel_area_t *src_areas,
            snd_pcm_uframes_t src_offset,
            snd_pcm_uframes_t size)
{
  auto *plugin = reinterpret_cast<AlsaPluginDxO*>(ext);

  snd_output_printf(plugin->output_, "XXX\n");
  snd_output_printf(plugin->output_, "  size %d\n", size);
  snd_output_printf(plugin->output_, "  src_areas %d\n", src_areas);
  snd_output_printf(plugin->output_, "  dst_offset %d\n", dst_offset);
  snd_output_printf(plugin->output_, "  channels %d\n", ext->channels);
  snd_output_printf(plugin->output_, "  slave_channels %d\n", ext->slave_channels);
  snd_output_printf(plugin->output_, "  rate %d\n", ext->rate);
  snd_output_printf(plugin->output_, "  format %d\n", ext->format);
  snd_output_printf(plugin->output_, "  slave_format %d\n", ext->slave_format);
  snd_output_flush(plugin->output_);

  if(ext->channels == 2)
  {
    if(ext->format == SND_PCM_FORMAT_S16_LE)
    {
      plugin->update<int16_t, false>(dst_areas, dst_offset, src_areas, src_offset, size);
    }
    else if(ext->format == SND_PCM_FORMAT_S32_LE)
    {
      plugin->update<int32_t, false>(dst_areas, dst_offset, src_areas, src_offset, size);
    }
  }
  else if(ext->channels == 3)
  {
    if(ext->format == SND_PCM_FORMAT_S16_LE)
    {
      plugin->update<int16_t, true>(dst_areas, dst_offset, src_areas, src_offset, size);
    }
    else if(ext->format == SND_PCM_FORMAT_S32_LE)
    {
      plugin->update<int32_t, true>(dst_areas, dst_offset, src_areas, src_offset, size);
    }
  }

  snd_output_printf(plugin->output_, "YYY\n");
  snd_output_flush(plugin->output_);

  return size;
}

int dxo_init(snd_pcm_extplug_t *ext)
{
  auto *plugin = reinterpret_cast<AlsaPluginDxO*>(ext);

  snd_output_printf(plugin->output_, "dxo_init\n");
  snd_output_flush(plugin->output_);

  uint32_t sampleRate = 0;//plugin->rate;
  snd_output_printf(plugin->output_, "samplerate: %d\n", sampleRate);

  //if(plugin->dxo_ != 0)
  {
    //delete plugin->dxo_;
  }

  //plugin->dxo_ = new DigitalCrossover(plugin->blockSize_);
  //plugin->dxo_->loadFilterData(plugin->coeffPath_);

  return 0;
}

int dxo_close(snd_pcm_extplug_t *ext)
{
  auto *plugin = reinterpret_cast<AlsaPluginDxO*>(ext);

  snd_output_printf(plugin->output_, "dxo_close\n");
  snd_output_flush(plugin->output_);

  //if(plugin->dxo_)
  //{
  //  delete plugin->dxo_;
  //  plugin->dxo_ = nullptr;
  //}

  delete plugin;

  return 0;
}

// Channel map (ALSA channel definitions)
static const unsigned int kChannelMap[] = {
        SND_CHMAP_FL,  // Front Left
        SND_CHMAP_FR,  // Front Right
        SND_CHMAP_RL,  // Rear Left
        SND_CHMAP_RR,  // Rear Right
        SND_CHMAP_FC,  // Center (not used)
        SND_CHMAP_LFE, // Subwoofer (direct mapping)
        SND_CHMAP_SL,  // Surround Left
        SND_CHMAP_SR   // Surround Right
};

snd_pcm_chmap_query_t **dxo_query_chmaps(snd_pcm_extplug_t *ext ATTRIBUTE_UNUSED)
{
	auto maps = static_cast<snd_pcm_chmap_query_t**>(malloc(sizeof(snd_pcm_chmap_query_t*) * 2));

	if(!maps)
  {
		return nullptr;
  }

  maps[0] = static_cast<snd_pcm_chmap_query_t*>(malloc(sizeof(snd_pcm_chmap_query_t) + sizeof(kChannelMap)));
  maps[1] = nullptr;

  if(maps[0] == nullptr)
  {
    snd_pcm_free_chmaps(maps);
    return nullptr;
  }

  maps[0]->type = SND_CHMAP_TYPE_FIXED;
  maps[0]->map.channels = sizeof(kChannelMap);
  memcpy(maps[0]->map.pos, kChannelMap, sizeof(kChannelMap));

	return maps;
}

snd_pcm_chmap_t *dxo_get_chmap(snd_pcm_extplug_t *ext ATTRIBUTE_UNUSED)
{
  auto map = static_cast<snd_pcm_chmap_t*>(malloc(sizeof(snd_pcm_chmap_query_t) + sizeof(kChannelMap)));

  if(map)
  {
    map->channels = sizeof(kChannelMap);
    memcpy(map->pos, kChannelMap, sizeof(kChannelMap));
  }

  return map;
}

static const snd_pcm_extplug_callback_t callbacks =
{
  .transfer = dxo_transfer,
  .close = dxo_close,
	.init = dxo_init,
#if SND_PCM_EXTPLUG_VERSION >= 0x10002
	.query_chmaps = dxo_query_chmaps,
  .get_chmap = dxo_get_chmap,
#endif
};

SND_PCM_PLUGIN_DEFINE_FUNC(dxo)
{
  snd_config_iterator_t i, next;

  long int channels = 0;
  long int blockSize = 128;
  std::string coeffPath = "dxo";
  snd_config_t *slaveConfig = 0;

  snd_config_for_each(i, next, conf)
  {
    snd_config_t *config = snd_config_iterator_entry(i);
    const char *id;
    snd_config_get_id(config, &id);

    std::string param(id);

    if(param == "slave")
    {
      slaveConfig = config;
      continue;
    }

    if(param == "channels")
    {
      snd_config_get_integer(config, &channels);
      continue;
    }

    if(param == "blocksize")
    {
      snd_config_get_integer(config, &blockSize);
      continue;
    }

    if(param == "path")
    {
      const char *path;
      snd_config_get_string(config, &path);
      coeffPath = path;
      continue;
    }
  }

  if(slaveConfig == 0)
  {
    return -EINVAL;
  }

  AlsaPluginDxO *plugin = new AlsaPluginDxO();

  plugin->callback = &callbacks;
  plugin->version = SND_PCM_EXTPLUG_VERSION;
  plugin->name = "dxo";
  plugin->private_data = plugin;

  //plugin->blockSize_ = blockSize;
  //plugin->coeffPath_=coeffPath;

  snd_output_stdio_attach(&(plugin->output_), stdout, 0);
  snd_output_printf(plugin->output_, "slave: %d\n", (slaveConfig));

  int32_t result = snd_pcm_extplug_create(plugin, name, root, slaveConfig, stream, mode);

  snd_output_printf(plugin->output_, "result: %d\n", (int32_t)result);

  if(result < 0)
  {
    return result;
  }

  static uint32_t formats[] = {SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_S32_LE};

  snd_pcm_extplug_set_param_minmax(plugin, SND_PCM_EXTPLUG_HW_CHANNELS, 2, 3);
  snd_pcm_extplug_set_param_list(plugin, SND_PCM_EXTPLUG_HW_FORMAT, sizeof(formats) / sizeof(formats[0]), formats);
  
  if(snd_pcm_extplug_set_slave_param(plugin, SND_PCM_EXTPLUG_HW_FORMAT, SND_PCM_FORMAT_S32_LE) < 0)
  {
    return -EINVAL;
  }

  if(snd_pcm_extplug_set_slave_param(plugin, SND_PCM_EXTPLUG_HW_CHANNELS, 8) < 0)
  {
    return -EINVAL;
  }

  snd_output_printf(plugin->output_, "ok VV\n");
  snd_output_flush(plugin->output_);

  *pcmp = plugin->pcm;

  return 0;
}

SND_PCM_PLUGIN_SYMBOL(dxo);

}
/*
int main()
{
    const int N = 1024;
    fftwf_complex *in, *out;
    fftwf_plan p;
    in = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * N);
    out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * N);
    p = fftwf_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    fftwf_execute(p);
    fftwf_destroy_plan(p);
    fftwf_free(in); fftwf_free(out);
  return 0;
}*/
