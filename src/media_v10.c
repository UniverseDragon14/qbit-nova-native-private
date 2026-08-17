#include "qn_media.h"

#include <math.h>
#include <string.h>

static QNStatus media_error(QNDiagnostic *diag, const char *code,
                            const char *message) {
    qn_diag_set_code(diag, code, 0, 0, "%s", message);
    return QN_ERR_SEMANTIC;
}

static bool continuation(uint8_t c) {
    return (c & 0xc0u) == 0x80u;
}

bool qn_utf8_string_is_valid(QNStringView value, bool reject_nul) {
    if (value.byte_length == 0u) return true;
    if (!value.data || value.byte_length > QN_MAX_STRING_BYTES) return false;

    const uint8_t *s = (const uint8_t *)value.data;
    uint32_t i = 0u;
    while (i < value.byte_length) {
        uint8_t c = s[i];
        if (reject_nul && c == 0u) return false;
        if (c <= 0x7fu) {
            ++i;
            continue;
        }

        if (c >= 0xc2u && c <= 0xdfu) {
            if (i + 1u >= value.byte_length || !continuation(s[i + 1u])) return false;
            i += 2u;
            continue;
        }

        if (c == 0xe0u) {
            if (i + 2u >= value.byte_length ||
                s[i + 1u] < 0xa0u || s[i + 1u] > 0xbfu ||
                !continuation(s[i + 2u])) return false;
            i += 3u;
            continue;
        }

        if ((c >= 0xe1u && c <= 0xecu) || (c >= 0xeeu && c <= 0xefu)) {
            if (i + 2u >= value.byte_length ||
                !continuation(s[i + 1u]) || !continuation(s[i + 2u])) return false;
            i += 3u;
            continue;
        }

        if (c == 0xedu) {
            if (i + 2u >= value.byte_length ||
                s[i + 1u] < 0x80u || s[i + 1u] > 0x9fu ||
                !continuation(s[i + 2u])) return false;
            i += 3u;
            continue;
        }

        if (c == 0xf0u) {
            if (i + 3u >= value.byte_length ||
                s[i + 1u] < 0x90u || s[i + 1u] > 0xbfu ||
                !continuation(s[i + 2u]) || !continuation(s[i + 3u])) return false;
            i += 4u;
            continue;
        }

        if (c >= 0xf1u && c <= 0xf3u) {
            if (i + 3u >= value.byte_length ||
                !continuation(s[i + 1u]) || !continuation(s[i + 2u]) ||
                !continuation(s[i + 3u])) return false;
            i += 4u;
            continue;
        }

        if (c == 0xf4u) {
            if (i + 3u >= value.byte_length ||
                s[i + 1u] < 0x80u || s[i + 1u] > 0x8fu ||
                !continuation(s[i + 2u]) || !continuation(s[i + 3u])) return false;
            i += 4u;
            continue;
        }

        return false;
    }

    return true;
}

QNStatus qn_f32_validate(float value, QNDiagnostic *diag) {
    if (!isfinite(value)) {
        return media_error(diag, "QN-E7801", "f32 value must be finite");
    }
    return QN_OK;
}

QNStatus qn_string_validate(QNStringView value, uint32_t max_bytes,
                            bool reject_nul, QNDiagnostic *diag) {
    if (max_bytes > QN_MAX_STRING_BYTES) max_bytes = QN_MAX_STRING_BYTES;
    if (value.byte_length > max_bytes) {
        return media_error(diag, "QN-E7802", "string exceeds configured byte limit");
    }
    if (value.byte_length > 0u && !value.data) {
        return media_error(diag, "QN-E7802", "non-empty string has null data");
    }
    if (!qn_utf8_string_is_valid(value, reject_nul)) {
        return media_error(diag, "QN-E7802", "string is not valid bounded UTF-8");
    }
    return QN_OK;
}

QNStatus qn_bytes_validate(QNBytesView value, uint32_t max_bytes,
                           QNDiagnostic *diag) {
    if (max_bytes > QN_MAX_BYTES_BUFFER) max_bytes = QN_MAX_BYTES_BUFFER;
    if (value.byte_length > max_bytes) {
        return media_error(diag, "QN-E7803", "bytes buffer exceeds configured byte limit");
    }
    if (value.byte_length > 0u && !value.data) {
        return media_error(diag, "QN-E7803", "non-empty bytes buffer has null data");
    }
    return QN_OK;
}

QNStatus qn_audio_buffer_validate(const QNAudioBufferView *audio,
                                  QNDiagnostic *diag) {
    if (!audio) return media_error(diag, "QN-E7804", "audio buffer is null");
    if (audio->abi_version != QN_MEDIA_ABI_V1) {
        return media_error(diag, "QN-E7804", "unsupported audio ABI version");
    }
    if (audio->sample_format != QN_AUDIO_SAMPLE_FORMAT_F32 ||
        audio->layout != QN_AUDIO_LAYOUT_INTERLEAVED || audio->flags != 0u) {
        return media_error(diag, "QN-E7804", "unsupported audio format, layout, or flags");
    }
    if (audio->sample_rate_hz < QN_MIN_AUDIO_SAMPLE_RATE_HZ ||
        audio->sample_rate_hz > QN_MAX_AUDIO_SAMPLE_RATE_HZ) {
        return media_error(diag, "QN-E7804", "audio sample rate is outside bounded range");
    }
    if (audio->channels == 0u || audio->channels > QN_MAX_AUDIO_CHANNELS) {
        return media_error(diag, "QN-E7804", "audio channel count is outside bounded range");
    }
    if (audio->frame_count > QN_MAX_AUDIO_FRAMES) {
        return media_error(diag, "QN-E7804", "audio frame count exceeds bounded limit");
    }

    uint64_t expected_samples = (uint64_t)audio->frame_count * audio->channels;
    uint64_t expected_bytes = expected_samples * sizeof(float);
    if (expected_samples > UINT32_MAX || expected_bytes > UINT32_MAX ||
        audio->sample_count != (uint32_t)expected_samples ||
        audio->byte_size != (uint32_t)expected_bytes) {
        return media_error(diag, "QN-E7804", "audio shape and byte size are inconsistent");
    }
    if (audio->sample_count > 0u && !audio->samples) {
        return media_error(diag, "QN-E7804", "non-empty audio buffer has null samples");
    }

    for (uint32_t i = 0u; i < audio->sample_count; ++i) {
        float sample = audio->samples[i];
        if (!isfinite(sample) || sample < -1.0f || sample > 1.0f) {
            return media_error(diag, "QN-E7805", "audio sample must be finite normalized f32");
        }
    }
    return QN_OK;
}

QNStatus qn_voice_request_validate(const QNVoiceRequest *request,
                                   QNDiagnostic *diag) {
    if (!request) return media_error(diag, "QN-E7806", "voice request is null");
    if (request->abi_version != QN_VOICE_REQUEST_ABI_V1 ||
        request->flags != QN_VOICE_FLAG_NONE || request->reserved != 0u) {
        return media_error(diag, "QN-E7806", "unsupported voice request ABI or flags");
    }
    if (request->sample_format != QN_AUDIO_SAMPLE_FORMAT_F32) {
        return media_error(diag, "QN-E7806", "voice output must use f32 audio in ABI v1");
    }
    if (request->sample_rate_hz < QN_MIN_AUDIO_SAMPLE_RATE_HZ ||
        request->sample_rate_hz > QN_MAX_AUDIO_SAMPLE_RATE_HZ) {
        return media_error(diag, "QN-E7806", "voice sample rate is outside bounded range");
    }
    if (request->channels == 0u || request->channels > QN_MAX_AUDIO_CHANNELS) {
        return media_error(diag, "QN-E7806", "voice channel count is outside bounded range");
    }

    QNStatus status = qn_string_validate(request->text, QN_MAX_VOICE_TEXT_BYTES,
                                         true, diag);
    if (status != QN_OK) return status;
    if (request->text.byte_length == 0u) {
        return media_error(diag, "QN-E7806", "voice text must not be empty");
    }
    return qn_string_validate(request->voice_id, QN_MAX_VOICE_ID_BYTES,
                              true, diag);
}

QNStatus qn_voice_request_init(QNVoiceRequest *out,
                               const char *text,
                               uint32_t text_bytes,
                               QNDiagnostic *diag) {
    if (!out) return media_error(diag, "QN-E7806", "voice request output is null");
    memset(out, 0, sizeof(*out));
    out->abi_version = QN_VOICE_REQUEST_ABI_V1;
    out->text.data = text;
    out->text.byte_length = text_bytes;
    out->sample_rate_hz = QN_DEFAULT_VOICE_SAMPLE_RATE_HZ;
    out->channels = 1u;
    out->sample_format = QN_AUDIO_SAMPLE_FORMAT_F32;
    return qn_voice_request_validate(out, diag);
}

QNStatus qn_value_validate(const QNValue *value, QNDiagnostic *diag) {
    if (!value) return media_error(diag, "QN-E7807", "value is null");
    switch (value->kind) {
        case QN_VALUE_F32:
            return qn_f32_validate(value->as.f32, diag);
        case QN_VALUE_STRING:
            return qn_string_validate(value->as.string, QN_MAX_STRING_BYTES, false, diag);
        case QN_VALUE_BYTES:
            return qn_bytes_validate(value->as.bytes, QN_MAX_BYTES_BUFFER, diag);
        case QN_VALUE_AUDIO_BUFFER:
            return qn_audio_buffer_validate(&value->as.audio, diag);
        case QN_VALUE_VOICE_REQUEST:
            return qn_voice_request_validate(&value->as.voice, diag);
        case QN_VALUE_NONE:
        default:
            return media_error(diag, "QN-E7807", "unsupported V10 value kind");
    }
}

const char *qn_value_kind_name(QNValueKind kind) {
    switch (kind) {
        case QN_VALUE_F32: return "f32";
        case QN_VALUE_STRING: return "string";
        case QN_VALUE_BYTES: return "bytes";
        case QN_VALUE_AUDIO_BUFFER: return "audio_buffer";
        case QN_VALUE_VOICE_REQUEST: return "voice_request";
        case QN_VALUE_NONE: return "none";
        default: return "unknown";
    }
}
