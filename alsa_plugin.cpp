
#include "alsa_plugin.h"

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

AlsaPluginDxO::AlsaPluginDxO(const std::string& path,
                             uint32_t blockSize,
                             const std::string slavePcm,
                             const snd_pcm_ioplug_callback_t* callbacks)
    : blockSize_(blockSize),
      inputs_(3),
      outputs_(7),
      inputOffset_(0),
      outputBuffer_{new int16_t[blockSize_ * kNumOutputChannels]},
      pcmName_(slavePcm)
{
  memset(this, 0, sizeof(snd_pcm_ioplug_t));

  // Init ALSA structure
  snd_pcm_ioplug_t::version = SND_PCM_IOPLUG_VERSION;
  snd_pcm_ioplug_t::name = "dxo";
  snd_pcm_ioplug_t::private_data = this;
  snd_pcm_ioplug_t::callback = callbacks;

  auto coeffs = loadFIRCoeffs(path, kScaleS16LE);
  assert(coeffs.size() == 7 && "Coeffs file need to provide 7 FIR transfer functions");

  std::vector<FirMultiChannelCrossover::ConfigType> config{{0, coeffs[0]},
                                                           {0, coeffs[1]},
                                                           {0, coeffs[2]},
                                                           {1, coeffs[3]},
                                                           {1, coeffs[4]},
                                                           {1, coeffs[5]},
                                                           {2, coeffs[6]}};

  crossover_ = std::make_unique<FirMultiChannelCrossover>(blockSize_, 3, config, 3);

  for(auto i{0}; i < inputs_.size(); ++i)
  {
    inputs_[i] = crossover_->getInputBuffer(i).data();
  }

  for(auto i{0}; i < outputs_.size(); ++i)
  {
    outputs_[i] = crossover_->getOutputBuffer(i).data();
  }
}

std::vector<std::vector<float>> AlsaPluginDxO::loadFIRCoeffs(const std::string& path, float scale)
{
  std::ifstream file(path);

  if(!file)
  {
    throw std::invalid_argument("Error: loadFIRCoeffs with " + path + " failed!");
  }

  std::vector<std::vector<float>> filters;
  while(!file.eof())
  {
    std::string line;
    while(std::getline(file, line))
    {
      uint32_t posNotWhiteSpace = 0;
      while(std::isspace(line[posNotWhiteSpace]))
      {
        ++posNotWhiteSpace;
      }

      if(line.substr(posNotWhiteSpace).starts_with('#') || line.length() == posNotWhiteSpace)
      {
        continue;
      }

      std::vector<float> coeffs;
      std::istringstream iss(line);
      while(!iss.eof())
      {
        double value = 0;
        iss >> value;
        coeffs.push_back(static_cast<float>(value) * scale);
      }

      if(coeffs.size() > 0)
      {
        filters.push_back(coeffs);
      }
    }
  }

  return filters;
}

void AlsaPluginDxO::enableLogging()
{
#ifdef BUILD_ARM
  logging_.open("/storage/dxo.txt", std::ios::app | std::ios::out);
#else
  logging_.open("/home/malte/dxo.txt", std::ios::app | std::ios::out);
#endif
}

bool AlsaPluginDxO::writePcm(const int16_t* data, const uint32_t frames)
{
  auto result = snd_pcm_writei(pcm_output_device_, data, frames);

  if(result != frames)
  {
    if(result < 0)
    {
      print("write error [", snd_strerror(result), "]");
    }
    else
    {
      print("incomplete write ", result, "/", frames);
    }

    snd_pcm_recover(pcm_output_device_, result, 0);

    return false;
  }

  return true;
}

extern "C" {

snd_pcm_sframes_t AlsaPluginDxO::dxo_pointer(snd_pcm_ioplug_t* io)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);
  return plugin->streamPos_ % (plugin->buffer_size / 2);
  /*
    // Verfügbar-Update beim Slave erzwingen
    if(snd_pcm_avail_update(plugin->pcm_output_device_) < 0)
    {
      return 0;
    }

    // Hardware-Pointer des Slave-Geräts ermitteln
    snd_pcm_sframes_t delay;
    snd_pcm_delay(plugin->pcm_output_device_, &delay);

    // Unser hw_ptr aus appl_ptr und delay rekonstruieren
    const auto hw_ptr = (io->appl_ptr + io->buffer_size - delay) % io->buffer_size;

    return hw_ptr;*/
}

snd_pcm_sframes_t AlsaPluginDxO::dxo_transfer(snd_pcm_ioplug_t* io,
                                              const snd_pcm_channel_area_t* src_areas,
                                              snd_pcm_uframes_t src_offset,
                                              snd_pcm_uframes_t size)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);

  const auto writer = [plugin](const int16_t* data, uint32_t frames) {
    return plugin->writePcm(data, frames);
  };

  if(io->format == SND_PCM_FORMAT_S16_LE)
  {
    PcmStream<int16_t> src(src_areas, src_offset);
    plugin->update(src, size, io->channels == 3, writer);
  }
  else if(io->format == SND_PCM_FORMAT_FLOAT_LE)
  {
    PcmStream<float> src(src_areas, src_offset);
    plugin->update(src, size, io->channels == 3, writer);
  }

  return size;
}

int AlsaPluginDxO::dxo_try_open_device(AlsaPluginDxO* plugin)
{
  if(plugin->pcm_output_device_)
  {
    return 0;
  }

  const auto result =
      snd_pcm_open(&(plugin->pcm_output_device_), plugin->pcmName_.c_str(), SND_PCM_STREAM_PLAYBACK, 0);

  if(result < 0)
  {
    plugin->pcm_output_device_ = nullptr;
    plugin->print("snd_pcm_open failed ", snd_strerror(result));
    return -EBUSY;
  }

  snd_pcm_hw_params_t* params{nullptr};
  snd_pcm_hw_params_alloca(&params);
  memset(params, 0, snd_pcm_hw_params_sizeof());

  snd_pcm_hw_params_any(plugin->pcm_output_device_, params);
  // snd_pcm_hw_params_dump(params, plugin->output_);

  if(snd_pcm_hw_params_set_access(plugin->pcm_output_device_, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
  {
    plugin->print("snd_pcm_hw_params_set_access failed");
  }

  if(snd_pcm_hw_params_set_format(plugin->pcm_output_device_, params, SND_PCM_FORMAT_S16_LE) < 0)
  {
    plugin->print("snd_pcm_hw_params_set_format failed");
  }

  if(snd_pcm_hw_params_set_channels(plugin->pcm_output_device_, params, AlsaPluginDxO::kNumOutputChannels) <
     0)
  {
    plugin->print("snd_pcm_hw_params_set_channels failed");
  }

  uint32_t rate = plugin->rate;
  if(snd_pcm_hw_params_set_rate_near(plugin->pcm_output_device_, params, &rate, 0) < 0)
  {
    plugin->print("snd_pcm_hw_params_set_rate_near failed");
  }

  if(snd_pcm_hw_params_set_period_size(plugin->pcm_output_device_, params, plugin->blockSize_, 0) < 0)
  {
    plugin->print("snd_pcm_hw_params_set_period_size failed");
  }

  if(snd_pcm_hw_params(plugin->pcm_output_device_, params) < 0)
  {
    plugin->print("snd_pcm_hw_params failed");
    snd_pcm_close(plugin->pcm_output_device_);
    plugin->pcm_output_device_ = nullptr;
    return -EINVAL;
  }

  auto chMap = snd_pcm_get_chmap(plugin->pcm_output_device_);

  if(chMap)
  {
    for(auto i{0}; i < chMap[0].channels; ++i)
    {
      plugin->channelMap_[i] = kMapAlsaChannel[chMap[0].pos[i]];
      plugin->print("CHMAP[", i, "]: ", chMap[0].pos[i], "  -> ", plugin->channelMap_[i]);
    }
  }

  return 0;
}

int AlsaPluginDxO::dxo_prepare(snd_pcm_ioplug_t* io)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);
  plugin->print("dxo_prepare");
  plugin->streamPos_ = 0;
  plugin->inputOffset_ = 0;
  plugin->crossover_->resetDelayLine();
  return 0;
}

int AlsaPluginDxO::dxo_close(snd_pcm_ioplug_t* io)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);

  plugin->print("dxo_close");
  plugin->print("avg time: ", plugin->totalTime_ / plugin->totalBlocks_);

  if(plugin->pcm_output_device_)
  {
    snd_pcm_drain(plugin->pcm_output_device_);
    snd_pcm_close(plugin->pcm_output_device_);
    plugin->pcm_output_device_ = nullptr;
  }

  delete plugin;

  return 0;
}

// Channel map (ALSA channel definitions)
struct ChannelMap
{
  uint32_t num_channels;
  std::array<unsigned int, 3> channels{};
};

static const ChannelMap kChannelMaps[] = {{2, {SND_CHMAP_FL, SND_CHMAP_FR, SND_CHMAP_UNKNOWN}},
                                          {3, {SND_CHMAP_FL, SND_CHMAP_FR, SND_CHMAP_LFE}}};
const int kNumChannelMaps = std::size(kChannelMaps);

snd_pcm_chmap_query_t** AlsaPluginDxO::dxo_query_chmaps(snd_pcm_ioplug_t* io)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);
  plugin->print("dxo_query_chmaps");

  auto maps =
      static_cast<snd_pcm_chmap_query_t**>(malloc(sizeof(snd_pcm_chmap_query_t*) * (kNumChannelMaps + 1)));

  if(!maps)
  {
    plugin->print("  invalid maps");
    return nullptr;
  }

  for(auto i{0}; i < kNumChannelMaps; ++i)
  {
    maps[i] = static_cast<snd_pcm_chmap_query_t*>(
        malloc(sizeof(snd_pcm_chmap_query_t) + sizeof(kChannelMaps[i].channels)));

    if(maps[i] == nullptr)
    {
      plugin->print("  invalid maps[i]");
      snd_pcm_free_chmaps(maps);
      return nullptr;
    }

    maps[i]->type = SND_CHMAP_TYPE_FIXED;
    maps[i]->map.channels = kChannelMaps[i].num_channels;
    memcpy(maps[i]->map.pos, kChannelMaps[i].channels.data(), sizeof(kChannelMaps[i].channels));
  }

  maps[kNumChannelMaps] = nullptr;

  return maps;
}

snd_pcm_chmap_t* AlsaPluginDxO::dxo_get_chmap(snd_pcm_ioplug_t* io)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);

  auto map =
      static_cast<snd_pcm_chmap_t*>(malloc(sizeof(snd_pcm_chmap_t) + sizeof(kChannelMaps[0].channels)));

  if(map)
  {
    const auto map_index =
        std::min(std::max(static_cast<int32_t>(plugin->channels) - 2, 0), kNumChannelMaps - 1);
    map->channels = plugin->channels;
    memcpy(map->pos, kChannelMaps[map_index].channels.data(), sizeof(kChannelMaps[0].channels));
  }

  return map;
}

int AlsaPluginDxO::dxo_hw_params(snd_pcm_ioplug_t* io, snd_pcm_hw_params_t* params)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);
  plugin->print("dxo_hw_params");

  snd_pcm_hw_params_get_rate(params, &plugin->rate, 0);
  snd_pcm_hw_params_get_channels(params, &plugin->channels);
  snd_pcm_hw_params_get_format(params, &plugin->format);
  snd_pcm_hw_params_get_period_size(params, &plugin->period_size, 0);
  snd_pcm_hw_params_get_buffer_size(params, &plugin->buffer_size);
  snd_pcm_hw_params_get_access(params, &plugin->access);

  return dxo_try_open_device(plugin);
}

int AlsaPluginDxO::dxo_delay(snd_pcm_ioplug_t* io, snd_pcm_sframes_t* delayp)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);
  plugin->print("dxo_delay");

  snd_pcm_sframes_t slave_delay{0};
  /*const auto result = snd_pcm_delay(plugin->pcm_output_device_, &slave_delay);
  if(result < 0)
  {
    plugin->print("snd_pcm_delay failed!");
    return 0;
  }*/

  const auto firDelay = 1;
  const auto algorithmDelay = plugin->blockSize_ - plugin->inputOffset_;
  *delayp = firDelay + algorithmDelay;

  return 0;
}

int AlsaPluginDxO::dxo_start(snd_pcm_ioplug_t* io)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);
  plugin->print("dxo_start");

  plugin->streamPos_ = 0;
  plugin->inputOffset_ = 0;

  return 0;
}

int AlsaPluginDxO::dxo_stop(snd_pcm_ioplug_t* io)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);
  plugin->print("dxo_stop");
  return 0;
}

static const snd_pcm_ioplug_callback_t callbacks = {
    .start = &AlsaPluginDxO::dxo_start,
    .stop = &AlsaPluginDxO::dxo_stop,
    .pointer = &AlsaPluginDxO::dxo_pointer,
    .transfer = &AlsaPluginDxO::dxo_transfer,
    .close = &AlsaPluginDxO::dxo_close,
    .hw_params = &AlsaPluginDxO::dxo_hw_params,
    .prepare = &AlsaPluginDxO::dxo_prepare,
#if SND_PCM_EXTPLUG_VERSION >= 0x10002
    //.delay = &AlsaPluginDxO::dxo_delay,
    .query_chmaps = &AlsaPluginDxO::dxo_query_chmaps,
    .get_chmap = &AlsaPluginDxO::dxo_get_chmap,
#endif
};

SND_PCM_PLUGIN_DEFINE_FUNC(dxo)
{
  long int channels = 0;
  long int blockSize = 128;
  std::string coeffPath;
  std::string slavePcm;
  snd_config_t* slaveConfig = nullptr;

  snd_config_iterator_t i, next;
  snd_config_for_each(i, next, conf)
  {
    snd_config_t* config = snd_config_iterator_entry(i);
    const char* id;
    snd_config_get_id(config, &id);

    std::string param(id);

    if(param == "slave")
    {
      snd_config_iterator_t i, next;
      snd_config_for_each(i, next, config)
      {
        snd_config_t* config = snd_config_iterator_entry(i);
        const char* id;
        snd_config_get_id(config, &id);

        if(std::string(id) == "pcm")
        {
          const char* str;
          snd_config_get_string(config, &str);
          slavePcm = str;
          break;
        }
      }
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

  if(slavePcm.length() == 0)
  {
    return -EINVAL;
  }

  AlsaPluginDxO* plugin = new AlsaPluginDxO(coeffPath, blockSize, slavePcm, &callbacks);
  plugin->enableLogging();

  auto result = snd_pcm_ioplug_create(plugin, name, stream, mode);

  if(result < 0)
  {
    plugin->print("snd_pcm_ioplug_create failed");
    return result;
  }

  static constexpr uint32_t supportedAccess[] = {SND_PCM_ACCESS_RW_INTERLEAVED};
  static constexpr uint32_t supportedFormats[] = {SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_FLOAT_LE};
  static constexpr uint32_t supportedHwRates[] = {44100, 48000};

  if(snd_pcm_ioplug_set_param_list(
         plugin, SND_PCM_IOPLUG_HW_ACCESS, std::size(supportedAccess), supportedAccess) < 0)
  {
    plugin->print("SND_PCM_IOPLUG_HW_ACCESS failed");
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_list(
         plugin, SND_PCM_IOPLUG_HW_FORMAT, std::size(supportedFormats), supportedFormats) < 0)
  {
    plugin->print("SND_PCM_IOPLUG_HW_FORMAT failed");
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_minmax(plugin, SND_PCM_IOPLUG_HW_CHANNELS, 2, 3) < 0)
  {
    plugin->print("SND_PCM_IOPLUG_HW_CHANNELS failed");
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_minmax(plugin, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 16 * 1024, 2 * 1024 * 1024) < 0)
  {
    plugin->print("SND_PCM_IOPLUG_HW_PERIOD_BYTES failed");
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_minmax(plugin, SND_PCM_IOPLUG_HW_BUFFER_BYTES, 16, 2 * 1024 * 1024) < 0)
  {
    plugin->print("SND_PCM_IOPLUG_HW_BUFFER_BYTES failed");
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_list(
         plugin, SND_PCM_IOPLUG_HW_RATE, std::size(supportedHwRates), supportedHwRates) < 0)
  {
    plugin->print("SND_PCM_IOPLUG_HW_RATE failed");
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_minmax(plugin, SND_PCM_IOPLUG_HW_PERIODS, 1, 1024) < 0)
  {
    plugin->print("SND_PCM_IOPLUG_HW_PERIODS failed");
    return -EINVAL;
  }

  *pcmp = plugin->pcm;

  plugin->print("SND_PCM_PLUGIN_DEFINE_FUNC: Ok");

  return 0;
}

SND_PCM_PLUGIN_SYMBOL(dxo);
}