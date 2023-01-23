#include <stdint.h>
#include <string.h>

#define RIFF_HDR_SIZE 8
#define FMT_PCM_CHUNK_SIZE 16
#define AUDIO_FORMAT_PCM 1 // Linear quantization
#define HDR_STR_LEN 4      // Generically used for strings scattered about the WAVE header

// Used to fill in the numChannels field
typedef enum
{
    MONO = 1,
    STEREO = 2
} channels_t;

// Used to fill in the sampleRate field
typedef enum
{
    SR_8K = 8000,
    SR_11K = 11025,
    SR_16K = 16000,
    SR_22K = 22050,
    SR_44K = 44100,
    SR_48K = 48000,
    SR_88K = 88200,
    SR_96K = 96000,
    SR_176K = 176400,
    SR_192K = 192000,
    SR_352K = 352800,
    SR_384K = 384000
} sampleRate_t;

// Used to fill in the bitDepth field
typedef enum
{
    BD_8 = 8,
    BD_16 = 16,
    BD_20 = 20,
    BD_24 = 24,
    BD_32 = 32
} bitDepth_t;

typedef struct wavHdr_t
{
    // RIFF Header
    char riffStr[HDR_STR_LEN];
    uint32_t fileSize; // file length - 8
    char format[HDR_STR_LEN];

    // Format Header
    char fmtHdr[HDR_STR_LEN];
    uint32_t fmtChunkSize;
    uint16_t audio_format;
    uint16_t numChannels; // channels_t
    uint32_t sampleRate;  // sampleRate_t
    uint32_t byteRate;    // numChannels * sampleRate * bitDepth / 8
    uint16_t blockAlign;  // numChannels * bitDepth / 8
    uint16_t bitDepth;    // bitDepth_t
} wavHdr_t;

typedef struct wavDataHdr_t
{
    char dataHdr[HDR_STR_LEN];
    uint32_t data_bytes; // Number of bytes in data. Number of samples * num_channels * sample byte size
} wavDataHdr_t;

static inline void cfgWave(wavHdr_t *wHdr, channels_t channels, sampleRate_t sRate, bitDepth_t bDepth)
{
    // Set constants
    wHdr->audio_format = AUDIO_FORMAT_PCM;
    strcpy(&wHdr->riffStr[0], "RIFF");
    strcpy(&wHdr->format[0], "WAVE");
    strcpy(&wHdr->fmtHdr[0], "fmt ");
    wHdr->fmtChunkSize = FMT_PCM_CHUNK_SIZE;

    // configure variables
    wHdr->numChannels = channels;
    wHdr->sampleRate = sRate;
    wHdr->bitDepth = bDepth;

    // calculate rate/alignment fields
    wHdr->byteRate = (channels * sRate * (bDepth / 8));
    wHdr->blockAlign = (channels * (bDepth / 8));
}