
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <stdint.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "crossover/fir_crossover.h"
#include "fftw3.h"
#include "pcm_stream.h"

class AlsaPluginDxO : public snd_pcm_extplug_t
{
public:
  AlsaPluginDxO(const std::string& path, uint32_t blockSize, snd_output_t* output)
      : blockSize_(blockSize), inputs_(3), outputs_(7), output_(output)
  {
    memset(this, 0, sizeof(snd_pcm_extplug_t));

    auto coeffs = loadFIRCoeffs(path);

    assert(coeffs.size() == 7 && "Coeffs file need to provide 7 FIR transfer functions");

    std::vector<FirMultiChannelCrossover::ConfigType> config{{0, coeffs[0]},
                                                             {0, coeffs[1]},
                                                             {0, coeffs[2]},
                                                             {1, coeffs[3]},
                                                             {1, coeffs[4]},
                                                             {1, coeffs[5]},
                                                             {2, coeffs[6]}};

    crossover_ = std::make_unique<FirMultiChannelCrossover>(blockSize_, 3, config, 2);

    for(auto i{0}; i < inputs_.size(); ++i)
    {
      inputs_[i] = crossover_->getInputBuffer(i).data();
    }

    for(auto i{0}; i < outputs_.size(); ++i)
    {
      outputs_[i] = crossover_->getOutputBuffer(i).data();
    }

    snd_output_printf(output_, "DxO load complete\n");
    snd_output_flush(output_);
  }

  ~AlsaPluginDxO() {}

  std::vector<std::vector<float>> loadFIRCoeffs(const std::string& path)
  {
    std::ifstream file(path);

    if(!file)
    {
      snd_output_printf(output_, "cant open file %s\n", path.c_str());
      snd_output_flush(output_);
      return {};
    }

    std::vector<std::vector<float>> filters;

    while(!file.eof())
    {
      std::string line;
      while(std::getline(file, line))
      {
        std::vector<float> coeffs;

        std::istringstream iss(line);
        while(!iss.eof())
        {
          double value = 0;
          iss >> value;
          coeffs.push_back(static_cast<float>(value));
        }

        filters.push_back(coeffs);
        snd_output_printf(output_, "next %c\n", file.peek());
        snd_output_flush(output_);
      }
    }

    return filters;
  }

  void print(const snd_pcm_channel_area_t* x, uint32_t offset)
  {
    auto* a = reinterpret_cast<uint16_t*>(snd_pcm_channel_area_addr(x + 0, offset));
    auto* b = reinterpret_cast<uint16_t*>(snd_pcm_channel_area_addr(x + 1, offset));

    std::cout << std::hex << "  a: " << (a) << "\n  b: " << (b) << "\n";
    for(uint32_t i = 0; i < 4; ++i)
    {
      std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0')
                << (a[i * snd_pcm_channel_area_step(x + 0) / sizeof(uint16_t)]) << " ";
      std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0')
                << (b[i * snd_pcm_channel_area_step(x + 1) / sizeof(uint16_t)]) << "\n";
    }
    std::cout << std::dec << std::endl;
  }

  template <typename _InputSampleType, bool _HasLfeChannel>
  uint32_t update(const snd_pcm_channel_area_t* dst_areas,
                  snd_pcm_uframes_t dst_offset,
                  const snd_pcm_channel_area_t* src_areas,
                  snd_pcm_uframes_t src_offset,
                  snd_pcm_uframes_t size)
  {
    PcmStream<int16_t> dst(dst_areas, dst_offset);
    PcmStream<_InputSampleType> src(src_areas, src_offset);
    std::cout << "size: " << (size) << std::endl;

    for(auto i{0}; i < size; i += blockSize_)
    {
      x += size;

      std::cout << "src:\n";
      print(src_areas, src_offset);

      if(_HasLfeChannel)
      {
        src.extractInterleaved(blockSize_, inputs_[0], inputs_[1], inputs_[2]);
      }
      else
      {
        src.extractInterleaved(blockSize_, inputs_[0], inputs_[1]);

        for(uint32_t i = 0; i < blockSize_; ++i)
        {
          inputs_[2][i] = (inputs_[0][i] + inputs_[1][i]) / 2;
        }
      }

      // std::cout << "copy" << std::endl;
      memcpy(outputs_[0], inputs_[0], sizeof(float) * blockSize_);
      memcpy(outputs_[1], inputs_[1], sizeof(float) * blockSize_);

      crossover_->updateInputs();

      dst.loadInterleaved(blockSize_,
                          outputs_[0],
                          outputs_[1]/*,
                          outputs_[2],
                          outputs_[3],
                          outputs_[0],  // unused
                          outputs_[6],
                          outputs_[4],
                          outputs_[5]);*/);

      // snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset, 2, blockSize_, 2);

      std::cout << "dst:\n";
      print(dst_areas, dst_offset);
      break;
    }

    // snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset, 2, size, 2);

    snd_output_printf(output_, "T %d\n", x);

    return 0;
  }

  uint32_t x{0};
  uint32_t blockSize_{128};
  std::vector<float*> inputs_;
  std::vector<float*> outputs_;
  snd_output_t* output_{nullptr};
  std::unique_ptr<FirMultiChannelCrossover> crossover_;
};

extern "C" {

snd_pcm_sframes_t dxo_transfer(snd_pcm_extplug_t* ext,
                               const snd_pcm_channel_area_t* dst_areas,
                               snd_pcm_uframes_t dst_offset,
                               const snd_pcm_channel_area_t* src_areas,
                               snd_pcm_uframes_t src_offset,
                               snd_pcm_uframes_t size)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(ext);

  snd_output_printf(plugin->output_, "XXX\n");
  snd_output_printf(plugin->output_, "  size %d\n", size);
  snd_output_printf(plugin->output_, "  src_areas %d\n", src_areas);
  snd_output_printf(plugin->output_, "  src_offset %d\n", src_offset);
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

int dxo_init(snd_pcm_extplug_t* ext)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(ext);

  snd_output_printf(plugin->output_, "dxo_init\n");
  snd_output_flush(plugin->output_);

  uint32_t sampleRate = 0;  // plugin->rate;
  snd_output_printf(plugin->output_, "samplerate: %d\n", sampleRate);

  return 0;
}

int dxo_close(snd_pcm_extplug_t* ext)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(ext);

  snd_output_printf(plugin->output_, "dxo_close\n");
  snd_output_flush(plugin->output_);

  delete plugin;

  return 0;
}

// Channel map (ALSA channel definitions)
static const unsigned int kChannelMap[] = {
    SND_CHMAP_FL,   // Front Left
    SND_CHMAP_FR,   // Front Right
    SND_CHMAP_RL,   // Rear Left
    SND_CHMAP_RR,   // Rear Right
    SND_CHMAP_FC,   // Center (not used)
    SND_CHMAP_LFE,  // Subwoofer (direct mapping)
    SND_CHMAP_SL,   // Surround Left
    SND_CHMAP_SR    // Surround Right
};

snd_pcm_chmap_query_t** dxo_query_chmaps(snd_pcm_extplug_t* ext ATTRIBUTE_UNUSED)
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

snd_pcm_chmap_t* dxo_get_chmap(snd_pcm_extplug_t* ext ATTRIBUTE_UNUSED)
{
  auto map = static_cast<snd_pcm_chmap_t*>(malloc(sizeof(snd_pcm_chmap_query_t) + sizeof(kChannelMap)));

  if(map)
  {
    map->channels = sizeof(kChannelMap);
    memcpy(map->pos, kChannelMap, sizeof(kChannelMap));
  }

  return map;
}

static const snd_pcm_extplug_callback_t callbacks = {
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
  snd_config_t* slaveConfig = 0;

  snd_config_for_each(i, next, conf)
  {
    snd_config_t* config = snd_config_iterator_entry(i);
    const char* id;
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
      const char* path;
      snd_config_get_string(config, &path);
      coeffPath = path;
      continue;
    }
  }

  if(slaveConfig == 0)
  {
    return -EINVAL;
  }

  snd_output_t* output;
  snd_output_stdio_attach(&(output), stdout, 0);

  snd_output_printf(output, "  path %s\n", coeffPath.c_str());
  snd_output_flush(output);

  AlsaPluginDxO* plugin = new AlsaPluginDxO(coeffPath, 64, output);

  plugin->callback = &callbacks;
  plugin->version = SND_PCM_EXTPLUG_VERSION;
  plugin->name = "dxo";
  plugin->private_data = plugin;
  plugin->output_ = output;

  snd_output_printf(plugin->output_, "slave: %d\n", (slaveConfig));

  int32_t result = snd_pcm_extplug_create(plugin, name, root, slaveConfig, stream, mode);

  snd_output_printf(plugin->output_, "result: %d\n", (int32_t)result);

  if(result < 0)
  {
    return result;
  }

  static uint32_t formats[] = {SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_S32_LE};

  snd_pcm_extplug_set_param_minmax(plugin, SND_PCM_EXTPLUG_HW_CHANNELS, 2, 3);
  snd_pcm_extplug_set_param_list(
      plugin, SND_PCM_EXTPLUG_HW_FORMAT, sizeof(formats) / sizeof(formats[0]), formats);

  if(snd_pcm_extplug_set_slave_param(plugin, SND_PCM_EXTPLUG_HW_FORMAT, SND_PCM_FORMAT_S16_LE) < 0)
  {
    return -EINVAL;
  }

  if(snd_pcm_extplug_set_slave_param(plugin, SND_PCM_EXTPLUG_HW_CHANNELS, 2) < 0)
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