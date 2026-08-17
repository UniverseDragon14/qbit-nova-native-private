#include "qn_media.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void qn_diag_set_code(QNDiagnostic *diag, const char *code,
                      int line, int column, const char *fmt, ...) {
    if (!diag) return;
    memset(diag, 0, sizeof(*diag));
    diag->line = line;
    diag->column = column;
    snprintf(diag->code, sizeof(diag->code), "%s", code ? code : "");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(diag->message, sizeof(diag->message), fmt, ap);
    va_end(ap);
}

static void expect_ok(QNStatus status, const QNDiagnostic *diag) {
    if (status != QN_OK) {
        fprintf(stderr, "unexpected error %s: %s\n", diag->code, diag->message);
        assert(status == QN_OK);
    }
}

int main(void) {
    QNDiagnostic diag = {0};

    QNVoiceRequest hello;
    const char *text = "Vanakkam Aslam";
    expect_ok(qn_voice_request_init(&hello, text, (uint32_t)strlen(text), &diag), &diag);
    assert(hello.abi_version == QN_VOICE_REQUEST_ABI_V1);
    assert(hello.sample_rate_hz == 24000u);
    assert(hello.channels == 1u);
    assert(hello.sample_format == QN_AUDIO_SAMPLE_FORMAT_F32);

    const char tamil[] = "வணக்கம் Aslam";
    QNStringView tamil_view = { tamil, (uint32_t)strlen(tamil) };
    expect_ok(qn_string_validate(tamil_view, QN_MAX_VOICE_TEXT_BYTES, true, &diag), &diag);

    const char invalid_utf8[] = { (char)0xc0, (char)0x80 };
    QNStringView bad = { invalid_utf8, 2u };
    assert(qn_string_validate(bad, QN_MAX_STRING_BYTES, true, &diag) == QN_ERR_SEMANTIC);
    assert(strcmp(diag.code, "QN-E7802") == 0);

    assert(qn_f32_validate(0.25f, &diag) == QN_OK);
    assert(qn_f32_validate(INFINITY, &diag) == QN_ERR_SEMANTIC);

    const uint8_t bytes_data[] = {0x00u, 0x01u, 0xffu};
    QNBytesView bytes = { bytes_data, 3u };
    expect_ok(qn_bytes_validate(bytes, QN_MAX_BYTES_BUFFER, &diag), &diag);

    const float samples[] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
    QNAudioBufferView audio = {
        .abi_version = QN_MEDIA_ABI_V1,
        .sample_format = QN_AUDIO_SAMPLE_FORMAT_F32,
        .layout = QN_AUDIO_LAYOUT_INTERLEAVED,
        .sample_rate_hz = 24000u,
        .channels = 1u,
        .flags = 0u,
        .frame_count = 5u,
        .sample_count = 5u,
        .byte_size = 5u * (uint32_t)sizeof(float),
        .samples = samples
    };
    expect_ok(qn_audio_buffer_validate(&audio, &diag), &diag);

    QNAudioBufferView bad_shape = audio;
    bad_shape.byte_size -= 1u;
    assert(qn_audio_buffer_validate(&bad_shape, &diag) == QN_ERR_SEMANTIC);
    assert(strcmp(diag.code, "QN-E7804") == 0);

    const float bad_sample[] = { 1.25f };
    QNAudioBufferView bad_audio = audio;
    bad_audio.frame_count = 1u;
    bad_audio.sample_count = 1u;
    bad_audio.byte_size = (uint32_t)sizeof(float);
    bad_audio.samples = bad_sample;
    assert(qn_audio_buffer_validate(&bad_audio, &diag) == QN_ERR_SEMANTIC);
    assert(strcmp(diag.code, "QN-E7805") == 0);

    QNValue voice_value = { .kind = QN_VALUE_VOICE_REQUEST };
    voice_value.as.voice = hello;
    expect_ok(qn_value_validate(&voice_value, &diag), &diag);
    assert(strcmp(qn_value_kind_name(QN_VALUE_VOICE_REQUEST), "voice_request") == 0);

    puts("QBIT_NOVA_V10_MEDIA_FOUNDATION=PASS");
    puts("V10_F32=PASS");
    puts("V10_UTF8_STRING=PASS");
    puts("V10_BYTES=PASS");
    puts("V10_TYPED_AUDIO_BUFFER=PASS");
    puts("V10_VOICE_REQUEST=PASS");
    puts("V10_VOICE_TEXT_VANAKKAM_ASLAM=PASS");
    return 0;
}
