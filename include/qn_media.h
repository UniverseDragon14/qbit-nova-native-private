#ifndef QN_MEDIA_H
#define QN_MEDIA_H

#include "qn.h"

enum {
    QN_MEDIA_ABI_V1 = 1u,
    QN_VOICE_REQUEST_ABI_V1 = 1u,
    QN_AUDIO_SAMPLE_FORMAT_F32 = 1u,
    QN_AUDIO_LAYOUT_INTERLEAVED = 1u,
    QN_VOICE_FLAG_NONE = 0u,
    QN_MAX_STRING_BYTES = 65536u,
    QN_MAX_BYTES_BUFFER = 16u * 1024u * 1024u,
    QN_MAX_VOICE_TEXT_BYTES = 8192u,
    QN_MAX_VOICE_ID_BYTES = 128u,
    QN_MAX_AUDIO_CHANNELS = 2u,
    QN_MAX_AUDIO_FRAMES = 4800000u,
    QN_MIN_AUDIO_SAMPLE_RATE_HZ = 8000u,
    QN_MAX_AUDIO_SAMPLE_RATE_HZ = 192000u,
    QN_DEFAULT_VOICE_SAMPLE_RATE_HZ = 24000u
};

typedef enum {
    QN_VALUE_NONE = 0,
    QN_VALUE_F32 = 1,
    QN_VALUE_STRING = 2,
    QN_VALUE_BYTES = 3,
    QN_VALUE_AUDIO_BUFFER = 4,
    QN_VALUE_VOICE_REQUEST = 5
} QNValueKind;

typedef struct {
    const char *data;
    uint32_t byte_length;
} QNStringView;

typedef struct {
    const uint8_t *data;
    uint32_t byte_length;
} QNBytesView;

typedef struct {
    uint16_t abi_version;
    uint8_t sample_format;
    uint8_t layout;
    uint32_t sample_rate_hz;
    uint16_t channels;
    uint16_t flags;
    uint32_t frame_count;
    uint32_t sample_count;
    uint32_t byte_size;
    const float *samples;
} QNAudioBufferView;

typedef struct {
    uint16_t abi_version;
    uint16_t flags;
    QNStringView text;
    QNStringView voice_id;
    uint32_t sample_rate_hz;
    uint16_t channels;
    uint8_t sample_format;
    uint8_t reserved;
} QNVoiceRequest;

typedef struct {
    QNValueKind kind;
    union {
        float f32;
        QNStringView string;
        QNBytesView bytes;
        QNAudioBufferView audio;
        QNVoiceRequest voice;
    } as;
} QNValue;

bool qn_utf8_string_is_valid(QNStringView value, bool reject_nul);
QNStatus qn_f32_validate(float value, QNDiagnostic *diag);
QNStatus qn_string_validate(QNStringView value, uint32_t max_bytes,
                            bool reject_nul, QNDiagnostic *diag);
QNStatus qn_bytes_validate(QNBytesView value, uint32_t max_bytes,
                           QNDiagnostic *diag);
QNStatus qn_audio_buffer_validate(const QNAudioBufferView *audio,
                                  QNDiagnostic *diag);
QNStatus qn_voice_request_validate(const QNVoiceRequest *request,
                                   QNDiagnostic *diag);
QNStatus qn_voice_request_init(QNVoiceRequest *out,
                               const char *text,
                               uint32_t text_bytes,
                               QNDiagnostic *diag);
QNStatus qn_value_validate(const QNValue *value, QNDiagnostic *diag);
const char *qn_value_kind_name(QNValueKind kind);

#endif
