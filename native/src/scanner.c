#include <jni.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}
#include <ebur128.h>
#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/tstringlist.h>

JNIEXPORT jstring JNICALL Java_com_himig_offline_RgScan_nativeScanFd(JNIEnv* env, jobject, jint fd) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) {
        return env->NewStringUTF("{\"error\":\"open\"}");
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return env->NewStringUTF("{\"error\":\"stream_info\"}");
    }
    int streamIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIdx < 0) {
        avformat_close_input(&fmt);
        return env->NewStringUTF("{\"error\":\"no_audio\"}");
    }
    AVStream* st = fmt->streams[streamIdx];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return env->NewStringUTF("{\"error\":\"codec\"}");
    }
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, st->codecpar);
    ctx->thread_count = 0;
    ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return env->NewStringUTF("{\"error\":\"open_codec\"}");
    }
    int channels = ctx->ch_layout.nb_channels;
    if (channels <= 0) channels = 2;
    int sampleRate = ctx->sample_rate;
    if (sampleRate <= 0) sampleRate = 44100;
    ebur128_state* r128 = ebur128_init(channels, sampleRate, EBUR128_MODE_I);
    if (!r128) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return env->NewStringUTF("{\"error\":\"ebur128\"}");
    }
    SwrContext* swr = nullptr;
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, channels);
    swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, sampleRate, &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
    swr_init(swr);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    double peak = 0.0;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == streamIdx) {
            if (avcodec_send_packet(ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    AVFrame* filt = av_frame_alloc();
                    filt->ch_layout = outLayout;
                    filt->sample_rate = sampleRate;
                    filt->format = AV_SAMPLE_FMT_FLT;
                    swr_convert_frame(swr, filt, frame);
                    int nb = filt->nb_samples;
                    float* data = (float*)filt->data[0];
                    for (int i = 0; i < nb * channels; i++) {
                        double v = fabs(data[i]);
                        if (v > peak) peak = v;
                    }
                    ebur128_add_frames_float(r128, data, nb);
                    av_frame_free(&filt);
                }
            }
        }
        av_packet_unref(pkt);
    }
    avcodec_send_packet(ctx, nullptr);
    while (avcodec_receive_frame(ctx, frame) == 0) {
        AVFrame* filt = av_frame_alloc();
        filt->ch_layout = outLayout;
        filt->sample_rate = sampleRate;
        filt->format = AV_SAMPLE_FMT_FLT;
        swr_convert_frame(swr, filt, frame);
        int nb = filt->nb_samples;
        float* data = (float*)filt->data[0];
        for (int i = 0; i < nb * channels; i++) {
            double v = fabs(data[i]);
            if (v > peak) peak = v;
        }
        ebur128_add_frames_float(r128, data, nb);
        av_frame_free(&filt);
    }
    double lufs = -70.0;
    ebur128_loudness_global(r128, &lufs);
    double gain = -18.0 - lufs;
    if (peak < 0.000001) peak = 0.000001;
    if (peak > 1.0) peak = 1.0;
    ebur128_destroy(&r128);
    swr_free(&swr);
    av_channel_layout_uninit(&outLayout);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    char json[256];
    snprintf(json, sizeof(json), "{\"lufs\":%.2f,\"peak\":%.6f,\"gain\":%.2f}", lufs, peak, gain);
    return env->NewStringUTF(json);
}

JNIEXPORT jint JNICALL Java_com_himig_offline_RgScan_nativeWriteTags(JNIEnv* env, jobject, jint fd, jdouble trackGain, jdouble trackPeak, jdouble albumGain, jdouble albumPeak) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    TagLib::FileRef f(path);
    if (f.isNull()) return -1;
    char tg[32], tp[32], ag[32], ap[32];
    snprintf(tg, sizeof(tg), "%.2f dB", trackGain);
    snprintf(tp, sizeof(tp), "%.6f", trackPeak);
    snprintf(ag, sizeof(ag), "%.2f dB", albumGain);
    snprintf(ap, sizeof(ap), "%.6f", albumPeak);
    TagLib::PropertyMap props = f.file()->properties();
    props.replace("REPLAYGAIN_TRACK_GAIN", TagLib::StringList(TagLib::String(tg)));
    props.replace("REPLAYGAIN_TRACK_PEAK", TagLib::StringList(TagLib::String(tp)));
    props.replace("REPLAYGAIN_ALBUM_GAIN", TagLib::StringList(TagLib::String(ag)));
    props.replace("REPLAYGAIN_ALBUM_PEAK", TagLib::StringList(TagLib::String(ap)));
    f.file()->setProperties(props);
    bool ok = f.save();
    return ok? 0 : -2;
}
