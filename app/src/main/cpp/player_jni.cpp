#pragma once

#include <jni.h>
#include <android/log.h>
#include <string>
#include "player/Player.h"
#include "LogUtil.h"
#include "ThreadPool.h"
#include "PlayerWrap.h"
#include "RealPlayer.h"
#include "EglDisplay.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,"okunu",__VA_ARGS__)

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <unistd.h>

extern "C" {
#include <libavutil/avutil.h>
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
}

extern "C"
JNIEXPORT void JNICALL
Java_com_ou_demo_player_NativePlayer_playVideo(JNIEnv *env, jobject thiz, jstring path_, jobject surface) {
    // 记录结果
    int result;
    // R1 Java String -> C String
    const char *path = env->GetStringUTFChars(path_, 0);

    av_register_all();
    // R2 初始化 AVFormatContext 上下文
    AVFormatContext *format_context = avformat_alloc_context();
    // 打开视频文件
    result = avformat_open_input(&format_context, path, NULL, NULL);
    if (result < 0) {
        LOGI("Player Error : Can not open video file");
        return;
    }
    // 查找视频文件的流信息
    result = avformat_find_stream_info(format_context, NULL);
    if (result < 0) {
        LOGI("Player Error : Can not find video file stream info");
        return;
    }
    // 查找视频编码器
    int video_stream_index = -1;
    for (int i = 0; i < format_context->nb_streams; i++) {
        // 匹配视频流
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
        }
    }
    // 没找到视频流
    if (video_stream_index == -1) {
        LOGI("Player Error : Can not find video stream");
        return;
    }
    // 初始化视频编码器上下文
    AVCodecContext *video_codec_context = avcodec_alloc_context3(NULL);
    avcodec_parameters_to_context(video_codec_context, format_context->streams[video_stream_index]->codecpar);
    // 初始化视频编码器
    AVCodec *video_codec = avcodec_find_decoder(video_codec_context->codec_id);
    if (video_codec == NULL) {
        LOGI("Player Error : Can not find video codec");
        return;
    }
    // R3 打开视频解码器
    result = avcodec_open2(video_codec_context, video_codec, NULL);
    if (result < 0) {
        LOGI("Player Error : Can not find video stream");
        return;
    }
    // 获取视频的宽高
    int videoWidth = video_codec_context->width;
    int videoHeight = video_codec_context->height;
    // R4 初始化 Native Window 用于播放视频
    ANativeWindow *native_window = ANativeWindow_fromSurface(env, surface);
    if (native_window == NULL) {
        LOGI("Player Error : Can not create native window");
        return;
    }
    // 通过设置宽高限制缓冲区中的像素数量，而非屏幕的物理显示尺寸。
    // 如果缓冲区与物理屏幕的显示尺寸不相符，则实际显示可能会是拉伸，或者被压缩的图像
    result = ANativeWindow_setBuffersGeometry(native_window, videoWidth, videoHeight, WINDOW_FORMAT_RGBA_8888);
    if (result < 0) {
        LOGI("Player Error : Can not set native window buffer");
        ANativeWindow_release(native_window);
        return;
    }
    // 定义绘图缓冲区
    ANativeWindow_Buffer window_buffer;
    // 声明数据容器 有3个
    // R5 解码前数据容器 Packet 编码数据
    AVPacket *packet = av_packet_alloc();
    // R6 解码后数据容器 Frame 像素数据 不能直接播放像素数据 还要转换
    AVFrame *frame = av_frame_alloc();
    // R7 转换后数据容器 这里面的数据可以用于播放
    AVFrame *rgba_frame = av_frame_alloc();
    // 数据格式转换准备
    // 输出 Buffer
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, videoWidth, videoHeight, 1);
    // R8 申请 Buffer 内存
    uint8_t *out_buffer = (uint8_t *) av_malloc(buffer_size * sizeof(uint8_t));
    av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, out_buffer, AV_PIX_FMT_RGBA, videoWidth, videoHeight,
                         1);
    // R9 数据格式转换上下文
    struct SwsContext *data_convert_context = sws_getContext(
            videoWidth, videoHeight, video_codec_context->pix_fmt,
            videoWidth, videoHeight, AV_PIX_FMT_RGBA,
            SWS_BICUBIC, NULL, NULL, NULL);
    // 开始读取帧
    while (av_read_frame(format_context, packet) >= 0) {
        // 匹配视频流
        if (packet->stream_index == video_stream_index) {
            LOGI("read a packet flag:%d", packet->flags);
            // 解码
            result = avcodec_send_packet(video_codec_context, packet);
            if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
                LOGI("Player Error : codec step 1 fail");
                return;
            }
//            result = avcodec_receive_frame(video_codec_context, frame);
            while (avcodec_receive_frame(video_codec_context, frame) == 0) {
                LOGI("read a frame flag:%lld", frame->pkt_dts);
                // 数据格式转换
                result = sws_scale(
                        data_convert_context,
                        (const uint8_t *const *) frame->data, frame->linesize,
                        0, videoHeight,
                        rgba_frame->data, rgba_frame->linesize);
                if (result <= 0) {
                    LOGI("Player Error : data convert fail");
                    return;
                }
                // 播放
                result = ANativeWindow_lock(native_window, &window_buffer, NULL);
                if (result < 0) {
                    LOGI("Player Error : Can not lock native window");
                } else {
                    // 将图像绘制到界面上
                    // 注意 : 这里 rgba_frame 一行的像素和 window_buffer 一行的像素长度可能不一致
                    // 需要转换好 否则可能花屏
                    uint8_t *bits = (uint8_t *) window_buffer.bits;
                    for (int h = 0; h < videoHeight; h++) {
                        memcpy(bits + h * window_buffer.stride * 4,
                               out_buffer + h * rgba_frame->linesize[0],
                               rgba_frame->linesize[0]);
                    }
                    ANativeWindow_unlockAndPost(native_window);
                }
//                usleep(30000);
            }
        }
        // 释放 packet 引用
        av_packet_unref(packet);
    }
    // 释放 R9
    sws_freeContext(data_convert_context);
    // 释放 R8
    av_free(out_buffer);
    // 释放 R7
    av_frame_free(&rgba_frame);
    // 释放 R6
    av_frame_free(&frame);
    // 释放 R5
    av_packet_free(&packet);
    // 释放 R4
    ANativeWindow_release(native_window);
    // 关闭 R3
    avcodec_close(video_codec_context);
    // 释放 R2
    avformat_close_input(&format_context);
    // 释放 R1
    env->ReleaseStringUTFChars(path_, path);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_ou_demo_player_NativePlayer_playAudio(JNIEnv *env, jobject thiz, jstring path_) {
    int result;
    const char *path = env->GetStringUTFChars(path_, 0);

    av_register_all();
    AVFormatContext *format_context = avformat_alloc_context();
    avformat_open_input(&format_context, path, nullptr, nullptr);
    result = avformat_find_stream_info(format_context, nullptr);
    if (result < 0) {
        LOGI("Player Error : Can not find video file stream info");
        return;
    }

    int audio_stream_index = -1;
    for (int i = 0; i < format_context->nb_streams; ++i) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
        }
    }

    if (audio_stream_index == -1) {
        LOGI("Player Error : Can not find audio stream");
        return;
    }

    AVCodecContext *audio_codec_context = avcodec_alloc_context3(nullptr);
    avcodec_parameters_to_context(audio_codec_context, format_context->streams[audio_stream_index]->codecpar);

    AVCodec *audio_codec = avcodec_find_decoder(audio_codec_context->codec_id);
    if (audio_codec == NULL) {
        LOGI("Player Error : Can not find audio codec");
        return;
    }

    result = avcodec_open2(audio_codec_context, audio_codec, nullptr);
    if (audio_codec == NULL) {
        LOGI("Player Error : Can not find audio codec");
        return;
    }

    struct SwrContext *swr_context = swr_alloc();
    uint8_t *out_buffer = (uint8_t *) av_malloc(44100 * 2);
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    enum AVSampleFormat out_format = AV_SAMPLE_FMT_S16;
    int out_sample_rate = audio_codec_context->sample_rate;
    swr_alloc_set_opts(swr_context, out_channel_layout, out_format, out_sample_rate,
                       audio_codec_context->channel_layout,
                       audio_codec_context->sample_fmt, audio_codec_context->sample_rate,
                       0, nullptr);
    swr_init(swr_context);
    int out_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);

    jclass player_class = env->GetObjectClass(thiz);
    jmethodID create_audio_track_method_id = env->GetMethodID(player_class, "createAudioTrack", "(II)V");
    env->CallVoidMethod(thiz, create_audio_track_method_id, 44100, out_channels);

    jmethodID play_audio_track_method_id = env->GetMethodID(player_class, "playAudioTrack", "([BI)V");

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == audio_stream_index) {
            result = avcodec_send_packet(audio_codec_context, packet);
            if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
                LOGI("Player Error : codec step 1 fail");
                return;
            }
            while (avcodec_receive_frame(audio_codec_context, frame) == 0) {
                swr_convert(swr_context, &out_buffer, 44100*2, (const uint8_t **)frame->data, frame->nb_samples);
                int size = av_samples_get_buffer_size(nullptr, out_channels, frame->nb_samples, AV_SAMPLE_FMT_S16, 1);
                jbyteArray audio_sample_array = env->NewByteArray(size);
                env->SetByteArrayRegion(audio_sample_array, 0, size, (const jbyte *) out_buffer);
                env->CallVoidMethod(thiz, play_audio_track_method_id, audio_sample_array, size);
                env->DeleteLocalRef(audio_sample_array);
            }
        }
        av_packet_unref(packet);
    }
    jmethodID release_audio_track_method_id = env->GetMethodID(player_class, "releaseAudioTrack", "()V");
    env->CallVoidMethod(thiz, release_audio_track_method_id);
    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&swr_context);
    avcodec_close(audio_codec_context);
    avformat_close_input(&format_context);
    avformat_free_context(format_context);
    env->ReleaseStringUTFChars(path_, path);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_ou_demo_player_NativePlayer_play(JNIEnv *env, jobject thiz, jstring _path,
                                          jobject surface) {
    const char* path = env->GetStringUTFChars(_path, 0);
    int result = 1;
    Player* player;
    player_init(&player, env, thiz, surface);
    if (result > 0) {
        result = format_init(player, path);
    }
    if (result > 0) {
        result = codec_init(player, AVMEDIA_TYPE_VIDEO);
    }
    if (result > 0) {
        result = codec_init(player, AVMEDIA_TYPE_AUDIO);
    }
    if (result > 0) {
        play_start(player);
    }

    ThreadPool pool(2);
    auto result1 = pool.submit("", [](int a, int b){
        return a + b;
    }, 7, 2);
    LOGI("result1 = %d", result1.get());

    auto result2 = pool.submit("", [](int a, int b){
        return a + b;
    }, 3, 4);
    LOGI("result2 = %d", result2.get());
    env->ReleaseStringUTFChars(_path, path);
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_ou_demo_player_NativePlayer_init_1player(JNIEnv *env, jobject thiz) {
    PlayerWrap* player = new PlayerWrap();
    return reinterpret_cast<jlong>(player);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_ou_demo_player_NativePlayer_real_1play(JNIEnv *env, jobject thiz, jlong ref, jstring _path,
                                                jobject surface) {
    PlayerWrap* player = reinterpret_cast<PlayerWrap*>(ref);
    const char* path = env->GetStringUTFChars(_path, 0);
    int result = player->player_init(thiz, surface, path);
    if (result > 0) {
        player->play_start();
    }
    env->ReleaseStringUTFChars(_path, path);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ou_demo_player_NativePlayer_play_1or_1pause(JNIEnv *env, jobject thiz, jlong ref) {

    PlayerWrap* player = reinterpret_cast<PlayerWrap*>(ref);
    player->pause();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_ou_demo_player_PlayerSurfaceView_surfaceChange(JNIEnv *env, jobject thiz, jlong ref,
                                                        jint width, jint height, jobject surface) {
    RealPlayer* player = reinterpret_cast<RealPlayer*>(ref);
    ANativeWindow *nwin = ANativeWindow_fromSurface(env, surface);
    EglDisplay display{nwin};
    player->surface_changed(width, height, display);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_ou_demo_player_NativePlayer2_play(JNIEnv *env, jobject thiz, jlong ref, jstring _path) {
    RealPlayer* player = reinterpret_cast<RealPlayer*>(ref);
    const char* path = env->GetStringUTFChars(_path, 0);
    player->playWrap(path);
    env->ReleaseStringUTFChars(_path, path);
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_ou_demo_player_NativePlayer2_init_1player(JNIEnv *env, jobject thiz) {
    RealPlayer* player = new RealPlayer();
    return reinterpret_cast<jlong>(player);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_ou_demo_player_NativePlayer2_seek(JNIEnv *env, jobject thiz, jlong ref) {
    RealPlayer* player = reinterpret_cast<RealPlayer*>(ref);
    player->seek();
}