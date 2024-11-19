
#include "alsa_plugin.h"

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <stdint.h>

extern "C" {

snd_pcm_sframes_t dxo_pointer(snd_pcm_ioplug_t* io)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);
  // std::cout << "dxo_pointer " << (plugin->streamPos_ % (plugin->buffer_size / 2)) << std::endl;

  return plugin->streamPos_ % (plugin->buffer_size / 2);
}

snd_pcm_sframes_t dxo_transfer(snd_pcm_ioplug_t* ext,
                               const snd_pcm_channel_area_t* src_areas,
                               snd_pcm_uframes_t src_offset,
                               snd_pcm_uframes_t size)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(ext);

  if(ext->format == SND_PCM_FORMAT_S16_LE)
  {
    PcmStream<int16_t> src(src_areas, src_offset);
    if(ext->channels == 2)
    {
      plugin->update<false>(src, size);
    }
    else
    {
      plugin->update<true>(src, size);
    }
  }
  /*else if(ext->format == SND_PCM_FORMAT_S32_LE)
  {
    PcmStream<int32_t> src(src_areas, src_offset);
    if(ext->channels == 2)
    {
      plugin->update<false>(src, size);
    }
    else
    {
      plugin->update<true>(src, size);
    }
  }*/

  return size;
}

int dxo_prepare(snd_pcm_ioplug_t* ext)
{
  std::cout << "dxo_prepare" << (ext) << std::endl;
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(ext);

  if(snd_pcm_open(&(plugin->pcm_), plugin->pcmName_.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0)
  {
    snd_output_printf(plugin->output_, "Can't open device.");

    if(snd_pcm_open(&(plugin->pcm_), plugin->pcmName_.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0)
    {
      return -EBUSY;
    }
  }

  std::cout << "dev opened" << std::endl;

  // if(snd_pcm_nonblock(plugin->pcm_, 1) < 0)
  {
    //  std::cout << "Can't set nonblocking mode." << std::endl;
  }

  snd_pcm_hw_params_alloca(&(plugin->params_));
  snd_pcm_hw_params_any(plugin->pcm_, plugin->params_);
  snd_pcm_hw_params_dump(plugin->params_, plugin->output_);

  if(snd_pcm_hw_params_set_access(plugin->pcm_, plugin->params_, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
  {
    std::cout << "Can't set interleaved mode." << std::endl;
  }

  if(snd_pcm_hw_params_set_format(plugin->pcm_, plugin->params_, SND_PCM_FORMAT_S16_LE) < 0)
  {
    std::cout << "Can't set format." << std::endl;
  }

  if(snd_pcm_hw_params_set_channels(plugin->pcm_, plugin->params_, AlsaPluginDxO::kNumOutputChannels) < 0)
  {
    std::cout << "Can't set channels number." << std::endl;
  }

  uint32_t rate = plugin->rate;
  std::cout << "rate: " << (rate) << std::endl;
  if(snd_pcm_hw_params_set_rate_near(plugin->pcm_, plugin->params_, &rate, 0) < 0)
  {
    std::cout << "Can't set rate." << std::endl;
  }

  if(snd_pcm_hw_params_set_period_size(plugin->pcm_, plugin->params_, plugin->blockSize_, 0) < 0)
  {
    snd_output_printf(plugin->output_, "Can't set period size.");
  }

  // if(snd_pcm_hw_params_set_buffer_size(plugin->pcm_, plugin->params_, plugin->blockSize_ * 128) < 0)
  {
      //  snd_output_printf(plugin->output_, "Can't set period size.");
  }

  // if(snd_pcm_hw_params_set_periods(plugin->pcm_, plugin->params_, 128, 0) < 0)
  {
    // snd_output_printf(plugin->output_, "Can't set period size.");
  }

  /*int dir{0};
  if(snd_pcm_hw_params_set_periods_min(plugin->pcm_, plugin->params_, 16U, &dir) < 0)
  {
    snd_output_printf(plugin->output_, "Can't set period size.");
  }

  snd_pcm_uframes_t minBufferSize = plugin->blockSize_ * 128U;
  if(snd_pcm_hw_params_set_buffer_size_min(plugin->pcm_, plugin->params_, &minBufferSize) < 0)
  {
    std::cout << "Can't set buffer size." << std::endl;
  }*/
  if(snd_pcm_hw_params(plugin->pcm_, plugin->params_) < 0)
  {
    std::cout << "Can't set harware parameters" << std::endl;
    snd_pcm_close(plugin->pcm_);
    plugin->pcm_ = nullptr;
    return -1;
  }

  snd_pcm_uframes_t periodSize{0};
  int dir{0};
  snd_pcm_hw_params_get_period_size(plugin->params_, &periodSize, &dir);
  auto periods{0U};
  snd_pcm_hw_params_get_periods(plugin->params_, &periods, &dir);
  snd_pcm_uframes_t bufferSize, bufferSizeMin, bufferSizeMax;
  snd_pcm_hw_params_get_buffer_size(plugin->params_, &bufferSize);
  std::cout << "HW:\n  PeriodSize: " << (periodSize) << "\n  Periods: " << (periods)
            << "\n  BufferSize: " << (bufferSize) << std::endl;
  /*snd_pcm_hw_params_get_buffer_size_min(plugin->params_, &bufferSizeMin);
  snd_pcm_hw_params_get_buffer_size_max(plugin->params_, &bufferSizeMax);
  std::cout << "XXX:\n  BufferSize: " << (bufferSize) << "\n  BufferSizeMin: " << (bufferSizeMin)
            << "\n  BufferSizeMax: " << (bufferSizeMax) << std::endl;

  uint32_t periods, periodsMin, periodsMax;
  snd_pcm_hw_params_get_periods(plugin->params_, &periods, nullptr);
  snd_pcm_hw_params_get_periods_min(plugin->params_, &periodsMin, nullptr);
  snd_pcm_hw_params_get_periods_max(plugin->params_, &periodsMax, nullptr);

  std::cout << "HW:\n  PeriodSize: " << (periodSize) << "\n  Periods: " << (periods)
            << "\n  PeriodsMin: " << (periodsMin) << "\n  PeriodsMax: " << (periodsMax) << std::endl;*/

  /* if(snd_pcm_hw_params_set_buffer_size(plugin->pcm_, plugin->params_, bufferSizeMax) < 0)
   {
     std::cout << "Can't set buffer size33." << std::endl;
   }

   if(snd_pcm_hw_params_set_periods(plugin->pcm_, plugin->params_, periodsMax, 0) < 0)
   {
     snd_output_printf(plugin->output_, "Can't set period size22.");
   }*/

  /* snd_pcm_sw_params_t* sw_params;
   auto rc = snd_pcm_sw_params_malloc(&sw_params);
   if(rc < 0)
   {
     fprintf(stderr, "cannot allocate software parameters structure (%s)\n", snd_strerror(rc));
     snd_pcm_close(plugin->pcm_);
     return (-1);
   }

   rc = snd_pcm_sw_params_current(plugin->pcm_, sw_params);
   if(rc < 0)
   {
     fprintf(stderr, "cannot initialize software parameters structure (%s)\n", snd_strerror(rc));
     snd_pcm_close(plugin->pcm_);
     return (-1);
   }

   snd_pcm_uframes_t thres = {0};
   if(snd_pcm_sw_params_get_start_threshold(sw_params, &thres) < 0)
   {
     fprintf(stderr, "Error setting start threshold\n");
     snd_pcm_close(plugin->pcm_);
     return -1;
   }

   std::cout << "Thres: " << (thres) << std::endl;

   snd_pcm_uframes_t startThreshold{std::max<snd_pcm_uframes_t>(((95 * periods) / 100) * periodSize, 1U)};
   std::cout << "startThreshold: " << (startThreshold) << std::endl;
   if(snd_pcm_sw_params_set_start_threshold(plugin->pcm_, sw_params, startThreshold) < 0)
   {
     fprintf(stderr, "Error setting start threshold\n");
     snd_pcm_close(plugin->pcm_);
     return -1;
   }

   snd_pcm_uframes_t stopThreshold{0};
   if(snd_pcm_sw_params_set_stop_threshold(plugin->pcm_, sw_params, stopThreshold) < 0)
   {
     fprintf(stderr, "Error setting stop threshold\n");
     snd_pcm_close(plugin->pcm_);
     return -1;
   }

   if((rc = snd_pcm_sw_params(plugin->pcm_, sw_params)) < 0)
   {
     fprintf(stderr, "cannot set software parameters (%s)\n", snd_strerror(rc));
     snd_pcm_close(plugin->pcm_);
     return (-1);
   }

   snd_pcm_sw_params_free(sw_params);*/

  return 0;
}

int dxo_close(snd_pcm_ioplug_t* ext)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(ext);

  std::cout << "dxo_close!" << std::endl;

  delete plugin;

  if(plugin->pcm_)
  {
    snd_pcm_drain(plugin->pcm_);
    snd_pcm_close(plugin->pcm_);
    plugin->pcm_ = nullptr;
  }

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

snd_pcm_chmap_query_t** dxo_query_chmaps(snd_pcm_ioplug_t* ext ATTRIBUTE_UNUSED)
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

snd_pcm_chmap_t* dxo_get_chmap(snd_pcm_ioplug_t* ext ATTRIBUTE_UNUSED)
{
  auto map = static_cast<snd_pcm_chmap_t*>(malloc(sizeof(snd_pcm_chmap_query_t) + sizeof(kChannelMap)));

  if(map)
  {
    map->channels = sizeof(kChannelMap);
    memcpy(map->pos, kChannelMap, sizeof(kChannelMap));
  }

  return map;
}

static const snd_pcm_ioplug_callback_t callbacks = {
    .start = [](snd_pcm_ioplug_t*) { return 0; },
    .stop = [](snd_pcm_ioplug_t*) { return 0; },
    .pointer = dxo_pointer,
    .transfer = dxo_transfer,
    .close = dxo_close,
    .prepare = dxo_prepare,
#if SND_PCM_EXTPLUG_VERSION >= 0x10002
    .query_chmaps = dxo_query_chmaps,
    .get_chmap = dxo_get_chmap,
#endif
};

SND_PCM_PLUGIN_DEFINE_FUNC(dxo)
{
  long int channels = 0;
  long int blockSize = 128;
  std::string coeffPath;
  std::string slavePcm;
  snd_config_t* slaveConfig = nullptr;

  snd_output_t* output;
  snd_output_stdio_attach(&(output), stdout, 0);

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

  // snd_output_t* output;
  // snd_output_stdio_attach(&(output), stdout, 0);

  AlsaPluginDxO* plugin = new AlsaPluginDxO(coeffPath, blockSize, 1024);
  plugin->callback = &callbacks;
  plugin->version = SND_PCM_IOPLUG_VERSION;
  plugin->name = "dxo";
  plugin->private_data = plugin;
  plugin->output_ = output;
  plugin->pcmName_ = slavePcm;

  int32_t result = snd_pcm_ioplug_create(plugin, name, stream, mode);

  std::cout << "snd_pcm_ioplug_create " << (result) << "  " << (plugin) << std::endl;

  if(result < 0)
  {
    return result;
  }

  static constexpr uint32_t supportedAccess[] = {SND_PCM_ACCESS_RW_INTERLEAVED};
  static constexpr uint32_t supportedFormats[] = {SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_S32_LE};
  static constexpr uint32_t supportedPeriodSize[] = {
      128 * 4, 256 * 4, 512 * 4, 1024 * 4, 128 * 6, 256 * 6, 512 * 6, 1024 * 6};

  if(snd_pcm_ioplug_set_param_list(
         plugin, SND_PCM_IOPLUG_HW_ACCESS, std::size(supportedAccess), supportedAccess) < 0)
  {
    std::cout << "Error SND_PCM_IOPLUG_HW_ACCESS failed" << std::endl;
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_list(
         plugin, SND_PCM_IOPLUG_HW_FORMAT, std::size(supportedFormats), supportedFormats) < 0)
  {
    std::cout << "Error SND_PCM_IOPLUG_HW_FORMAT failed" << std::endl;
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_minmax(plugin, SND_PCM_IOPLUG_HW_CHANNELS, 2, 3) < 0)
  {
    std::cout << "Error SND_PCM_IOPLUG_HW_CHANNELS failed" << std::endl;
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_minmax(plugin, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 16, 2 * 1024 * 1024) < 0)
  {
    std::cout << "Error SND_PCM_IOPLUG_HW_PERIOD_BYTES failed" << std::endl;
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_minmax(plugin, SND_PCM_IOPLUG_HW_BUFFER_BYTES, 16, 2 * 1024 * 1024) < 0)
  {
    std::cout << "Error SND_PCM_IOPLUG_HW_BUFFER_BYTES failed" << std::endl;
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_minmax(plugin, SND_PCM_IOPLUG_HW_RATE, 44100, 48000) < 0)
  {
    std::cout << "Error SND_PCM_IOPLUG_HW_RATE failed" << std::endl;
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_minmax(plugin, SND_PCM_IOPLUG_HW_PERIODS, 1, 1024) < 0)
  {
    std::cout << "Error SND_PCM_IOPLUG_HW_PERIOD_BYTES failed" << std::endl;
    return -EINVAL;
  }

  *pcmp = plugin->pcm;

  return 0;
}

SND_PCM_PLUGIN_SYMBOL(dxo);
}