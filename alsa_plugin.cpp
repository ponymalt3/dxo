
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <stdint.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "crossover/fir_crossover.h"
#include "fftw3.h"
#include "pcm_stream.h"

class AlsaPluginDxO : public snd_pcm_ioplug_t
{
public:
  enum
  {
    kNumOutputChannels = 8,
    kNumPeriods = 4
  };

  AlsaPluginDxO(const std::string& path, uint32_t blockSize)
      : blockSize_(blockSize),
        inputs_(3),
        outputs_(7),
        inputOffset_(0),
        outputBuffer_{new(std::align_val_t(64)) int16_t[kNumOutputChannels * blockSize_ * kNumPeriods]}
  {
    memset(this, 0, sizeof(snd_pcm_ioplug_t));

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
  }

  ~AlsaPluginDxO() {}

  std::vector<std::vector<float>> loadFIRCoeffs(const std::string& path)
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
        std::vector<float> coeffs;
        std::istringstream iss(line);
        while(!iss.eof())
        {
          double value = 0;
          iss >> value;
          coeffs.push_back(static_cast<float>(value));
        }

        filters.push_back(coeffs);
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

  template <bool _HasLfeChannel, typename _InputSampleType>
  uint32_t update(PcmStream<_InputSampleType>& src, uint32_t size)
  {
    PcmStream<int16_t> dst{outputBuffer_.get(), kNumOutputChannels};

    auto i{0U};
    while(i < size)
    {
      uint32_t segmentSize = std::min(size - i, blockSize_ - inputOffset_);

      if(_HasLfeChannel)
      {
        src.extractInterleaved(
            segmentSize, inputs_[0] + inputOffset_, inputs_[1] + inputOffset_, inputs_[2] + inputOffset_);
      }
      else
      {
        src.extractInterleaved(segmentSize, inputs_[0] + inputOffset_, inputs_[1] + inputOffset_);

        for(auto j{inputOffset_}; j < inputOffset_ + segmentSize; ++j)
        {
          inputs_[2][j] = (inputs_[0][j] + inputs_[1][j]) / 2;
        }
      }

      inputOffset_ += segmentSize;
      i += segmentSize;

      if(inputOffset_ == blockSize_)
      {
        // memcpy(outputs_[0], inputs_[0], sizeof(float) * blockSize_);
        // memcpy(outputs_[1], inputs_[1], sizeof(float) * blockSize_);
        // snd_pcm_areas_copy(dst_areas, dst_offset, src_areas, src_offset, 2, blockSize_, 2);

        crossover_->updateInputs();

        dst.loadInterleaved(blockSize_,
                            outputs_[0],
                            outputs_[1],
                            outputs_[2],
                            outputs_[3],
                            outputs_[0],  // unused
                            outputs_[6],
                            outputs_[4],
                            outputs_[5]);

        syncOutputBuffer();

        inputOffset_ = 0;
        dst = PcmStream<int16_t>{outputBuffer_.get(), kNumOutputChannels};
      }
    }

    streamPos_ += size;
    return 0;
  }

  bool syncOutputBuffer()
  {
    auto result = snd_pcm_writei(pcm_, outputBuffer_.get(), blockSize_);

    if(result == -EPIPE)
    {
      snd_output_printf(output_, "underflow\n");  // snd_strerror(result));
      snd_output_flush(output_);
    }

    return result == blockSize_;
  }

  uint32_t blockSize_{128};
  std::vector<float*> inputs_;
  std::vector<float*> outputs_;
  uint32_t inputOffset_;
  snd_output_t* output_{nullptr};
  std::unique_ptr<FirMultiChannelCrossover> crossover_;
  snd_pcm_t* pcm_;
  snd_pcm_hw_params_t* params_;
  std::unique_ptr<int16_t[]> outputBuffer_;
  std::string pcmName_;
  uint32_t streamPos_{0};
};

extern "C" {

snd_pcm_sframes_t dxo_pointer(snd_pcm_ioplug_t* io)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(io);
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
  else if(ext->format == SND_PCM_FORMAT_S32_LE)
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
  }

  return size;
}

int dxo_prepare(snd_pcm_ioplug_t* ext)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(ext);

  if(snd_pcm_open(&(plugin->pcm_), plugin->pcmName_.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0)
  {
    snd_output_printf(plugin->output_, "Can't open device.");
  }

  snd_pcm_hw_params_alloca(&(plugin->params_));
  snd_pcm_hw_params_any(plugin->pcm_, plugin->params_);

  if(snd_pcm_hw_params_set_access(plugin->pcm_, plugin->params_, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
  {
    snd_output_printf(plugin->output_, "Can't set interleaved mode.");
  }

  if(snd_pcm_hw_params_set_format(plugin->pcm_, plugin->params_, SND_PCM_FORMAT_S16_LE) < 0)
  {
    snd_output_printf(plugin->output_, "Can't set format.");
  }

  if(snd_pcm_hw_params_set_channels(plugin->pcm_, plugin->params_, AlsaPluginDxO::kNumOutputChannels) < 0)
  {
    snd_output_printf(plugin->output_, "Can't set channels number.");
  }

  uint32_t rate = plugin->rate;
  if(snd_pcm_hw_params_set_rate_near(plugin->pcm_, plugin->params_, &rate, 0) < 0)
  {
    snd_output_printf(plugin->output_, "Can't set rate.");
  }

  if(snd_pcm_hw_params_set_period_size(plugin->pcm_, plugin->params_, plugin->blockSize_, 0) < 0)
  {
    snd_output_printf(plugin->output_, "Can't set period size.");
  }

  if(snd_pcm_hw_params_set_buffer_size(
         plugin->pcm_, plugin->params_, plugin->blockSize_ * AlsaPluginDxO::kNumPeriods) < 0)
  {
    snd_output_printf(plugin->output_, "Can't set period size.");
  }

  if(snd_pcm_hw_params(plugin->pcm_, plugin->params_) < 0)
  {
    snd_output_printf(plugin->output_, "Can't set harware parameters.");
  }

  snd_output_flush(plugin->output_);

  return 0;
}

int dxo_close(snd_pcm_ioplug_t* ext)
{
  auto* plugin = reinterpret_cast<AlsaPluginDxO*>(ext);

  snd_pcm_drain(plugin->pcm_);
  snd_pcm_close(plugin->pcm_);

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

  snd_output_t* output;
  snd_output_stdio_attach(&(output), stdout, 0);

  AlsaPluginDxO* plugin = new AlsaPluginDxO(coeffPath, blockSize);
  plugin->callback = &callbacks;
  plugin->version = SND_PCM_IOPLUG_VERSION;
  plugin->name = "dxo";
  plugin->private_data = plugin;
  plugin->output_ = output;
  plugin->pcmName_ = slavePcm;

  int32_t result = snd_pcm_ioplug_create(plugin, name, stream, mode);

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

  if(snd_pcm_ioplug_set_param_list(
         plugin, SND_PCM_IOPLUG_HW_PERIOD_BYTES, std::size(supportedPeriodSize), supportedPeriodSize) < 0)
  {
    std::cout << "Error SND_PCM_IOPLUG_HW_PERIOD_BYTES failed" << std::endl;
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_minmax(plugin, SND_PCM_IOPLUG_HW_BUFFER_BYTES, 16, 2 * 1024 * 1024) < 0)
  {
    std::cout << "Error SND_PCM_IOPLUG_HW_BUFFER_BYTES failed" << std::endl;
    return -EINVAL;
  }

  if(snd_pcm_ioplug_set_param_minmax(plugin, SND_PCM_IOPLUG_HW_RATE, 48000, 48000) < 0)
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