#include <iostream>
#include "demuxthread.h"
#include "decodethread.h"
#include "audiooutput.h"
#include "videooutput.h"
#include "AVSync.h"
using namespace std;
#undef main
int main(int argc, char *argv[])
{
    cout << "Hello World!" << endl;
    printf("url :%s\n", argv[1]);
    int ret = 0;
    AVPacketQueue audio_packet_queue;
    AVPacketQueue video_packet_queue;
    AVFrameQueue audio_frame_queue;
    AVFrameQueue video_frame_queue;
    AVSync avsync;
    //创建解复用线程，获取音频队列和视频队列
    DemuxThread *demux_thread = new DemuxThread(&audio_packet_queue,  &video_packet_queue);
    //传入文件路径
    ret = demux_thread->Init(argv[1]);
    if(ret < 0) {
        printf("%s(%d) demux_thread Init\n", __FUNCTION__, __LINE__);
        return -1;
    }
    ret = demux_thread->Start();
    if(ret < 0) {
        printf("%s(%d) demux_thread Start\n", __FUNCTION__, __LINE__);
        return -1;
    }
    //音频解码线程
    DecodeThread *audio_decode_thread = new DecodeThread(&audio_packet_queue, &audio_frame_queue);
    ret = audio_decode_thread->Init(demux_thread->AudioCodecParameters());
    if(ret < 0) {
        printf("%s(%d) audio_decode_thread Init\n", __FUNCTION__, __LINE__);
        return -1;
    }
    ret = audio_decode_thread->Start();
    if(ret < 0) {
        printf("%s(%d) audio_decode_thread Start\n", __FUNCTION__, __LINE__);
        return -1;
    }
    // 视频解码线程
    DecodeThread *video_decode_thread = new DecodeThread(&video_packet_queue, &video_frame_queue);
    ret = video_decode_thread->Init(demux_thread->VideoCodecParameters());
    if(ret < 0) {
        printf("%s(%d) video_decode_thread Init\n", __FUNCTION__, __LINE__);
        return -1;
    }
    ret = video_decode_thread->Start();
    if(ret < 0) {
        printf("%s(%d) video_decode_thread Start\n", __FUNCTION__, __LINE__);
        return -1;
    }
    avsync.InitClock();
    //进行音频重采样
    AudioParams audio_params;
    memset(&audio_params, 0, sizeof(audio_params));
    //获取声道布局
    audio_params.ch_layout = audio_decode_thread->GetAVCodecContext()->ch_layout;
    //获取采样格式
    audio_params.fmt =  audio_decode_thread->GetAVCodecContext()->sample_fmt;
    //获取采样率
    audio_params.freq = audio_decode_thread->GetAVCodecContext()->sample_rate;
    AudioOutput *audio_output = new AudioOutput(&avsync, audio_params, &audio_frame_queue, demux_thread->AudioStreamTimebase());
    ret = audio_output->Init();
    if(ret < 0) {
        printf("%s(%d) audio_output Init\n", __FUNCTION__, __LINE__);
        return -1;
    }
    //    std::this_thread::sleep_for(std::chrono::milliseconds(3000000));
    VideoOutput *video_output_ = new VideoOutput(&avsync, &video_frame_queue, video_decode_thread->GetAVCodecContext()->width,
            video_decode_thread->GetAVCodecContext()->height, demux_thread->VideoStreamTimebase());
    ret = video_output_->Init();
    if(ret < 0) {
        printf("%s(%d) video_output_ Init\n", __FUNCTION__, __LINE__);
        return -1;
    }
    video_output_->MainLoop();
    printf("%s(%d) demux_thread delete\n", __FUNCTION__, __LINE__);
    audio_output->DeInit();
    delete audio_output;
    demux_thread->Stop();
    delete demux_thread;
    printf("%s(%d) audio_decode_thread delete\n", __FUNCTION__, __LINE__);
    audio_decode_thread->Stop();
    delete audio_decode_thread;
    printf("%s(%d) video_decode_thread delete\n", __FUNCTION__, __LINE__);
    video_decode_thread->Stop();
    delete video_decode_thread;
    printf("main finish\n");
    return 0;
}
