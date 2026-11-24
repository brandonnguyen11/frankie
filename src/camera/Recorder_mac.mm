// AVFoundation recorder: H.264 video + AAC audio into an .mp4.
#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "camera/Recorder.h"
#include "core/Platform.h"
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>

namespace og {
namespace {

class AVRecorder : public Recorder {
 public:
  ~AVRecorder() override {
    std::string e;
    if (active_) stop(e);
  }

  bool start(const std::string& path, int w, int h, int fps, int rate, int ch, std::string& error) override {
    @autoreleasepool {
      path_ = path;
      w_ = w; h_ = h; fps_ = fps; rate_ = rate; ch_ = ch;
      audioFrames_ = 0;
      lastVideo_ = -1;
      std::filesystem::remove(path);
      NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
      NSError* err = nil;
      writer_ = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeMPEG4 error:&err];
      if (!writer_) {
        error = err ? err.localizedDescription.UTF8String : "AVAssetWriter failed";
        return false;
      }
      NSDictionary* comp = @{
        AVVideoAverageBitRateKey : @(24 * 1000 * 1000),
        AVVideoMaxKeyFrameIntervalKey : @(fps),
        AVVideoProfileLevelKey : AVVideoProfileLevelH264HighAutoLevel,
      };
      NSDictionary* vset = @{
        AVVideoCodecKey : AVVideoCodecTypeH264,
        AVVideoWidthKey : @(w),
        AVVideoHeightKey : @(h),
        AVVideoCompressionPropertiesKey : comp,
      };
      video_ = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:vset];
      video_.expectsMediaDataInRealTime = YES;
      NSDictionary* pba = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferWidthKey : @(w),
        (id)kCVPixelBufferHeightKey : @(h),
      };
      adaptor_ = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:video_
                                                                                 sourcePixelBufferAttributes:pba];
      AudioChannelLayout layout{};
      layout.mChannelLayoutTag = ch == 1 ? kAudioChannelLayoutTag_Mono : kAudioChannelLayoutTag_Stereo;
      NSDictionary* aset = @{
        AVFormatIDKey : @(kAudioFormatMPEG4AAC),
        AVSampleRateKey : @(rate),
        AVNumberOfChannelsKey : @(ch),
        AVEncoderBitRateKey : @(192000),
        AVChannelLayoutKey : [NSData dataWithBytes:&layout length:sizeof(layout)],
      };
      audio_ = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:aset];
      audio_.expectsMediaDataInRealTime = YES;
      if (![writer_ canAddInput:video_] || ![writer_ canAddInput:audio_]) {
        error = "AVAssetWriter rejected inputs";
        return false;
      }
      [writer_ addInput:video_];
      [writer_ addInput:audio_];
      if (![writer_ startWriting]) {
        error = writer_.error ? writer_.error.localizedDescription.UTF8String : "startWriting failed";
        return false;
      }
      [writer_ startSessionAtSourceTime:kCMTimeZero];

      AudioStreamBasicDescription asbd{};
      asbd.mSampleRate = rate;
      asbd.mFormatID = kAudioFormatLinearPCM;
      asbd.mFormatFlags = kLinearPCMFormatFlagIsFloat | kAudioFormatFlagIsPacked;
      asbd.mBytesPerPacket = asbd.mBytesPerFrame = sizeof(float) * ch;
      asbd.mFramesPerPacket = 1;
      asbd.mChannelsPerFrame = ch;
      asbd.mBitsPerChannel = 32;
      if (CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &asbd, 0, nullptr, 0, nullptr, nullptr, &afmt_) != noErr) {
        error = "audio format description failed";
        return false;
      }
      queue_ = dispatch_queue_create("og.recorder", DISPATCH_QUEUE_SERIAL);
      active_ = true;
      return true;
    }
  }

  void pushVideo(const unsigned char* bgra, int w, int h, int64_t idx) override {
    if (!active_ || w != w_ || h != h_ || idx <= lastVideo_) return;
    // constant frame rate: if the game hitched, hold this frame across the gap
    int64_t first = lastVideo_ < 0 ? idx : lastVideo_ + 1;
    lastVideo_ = idx;
    CVPixelBufferRef pb = nullptr;
    if (adaptor_.pixelBufferPool) CVPixelBufferPoolCreatePixelBuffer(nullptr, adaptor_.pixelBufferPool, &pb);
    if (!pb) CVPixelBufferCreate(nullptr, w, h, kCVPixelFormatType_32BGRA, nullptr, &pb);
    if (!pb) return;
    CVPixelBufferLockBaseAddress(pb, 0);
    auto* dst = (unsigned char*)CVPixelBufferGetBaseAddress(pb);
    size_t stride = CVPixelBufferGetBytesPerRow(pb);
    for (int y = 0; y < h; ++y)  // flip: GL rows are bottom-up
      std::memcpy(dst + (size_t)y * stride, bgra + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    CVPixelBufferUnlockBaseAddress(pb, 0);
    AVAssetWriterInputPixelBufferAdaptor* ad = adaptor_;
    AVAssetWriterInput* in = video_;
    int fps = fps_;
    dispatch_async(queue_, ^{
      for (int64_t k = first; k <= idx; ++k) {
        for (int tries = 0; tries < 200 && !in.readyForMoreMediaData; ++tries) usleep(1000);
        if (in.readyForMoreMediaData) [ad appendPixelBuffer:pb withPresentationTime:CMTimeMake(k, fps)];
      }
      CVPixelBufferRelease(pb);
    });
  }

  void pushAudio(const float* data, size_t frames) override {
    if (!active_ || frames == 0) return;
    size_t bytes = frames * ch_ * sizeof(float);
    CMBlockBufferRef block = nullptr;
    if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, bytes, kCFAllocatorDefault, nullptr, 0,
                                           bytes, kCMBlockBufferAssureMemoryNowFlag, &block) != noErr)
      return;
    CMBlockBufferReplaceDataBytes(data, block, 0, bytes);
    CMSampleBufferRef sb = nullptr;
    OSStatus st = CMAudioSampleBufferCreateReadyWithPacketDescriptions(
        kCFAllocatorDefault, block, afmt_, (CMItemCount)frames, CMTimeMake(audioFrames_, rate_), nullptr, &sb);
    CFRelease(block);
    audioFrames_ += (int64_t)frames;
    if (st != noErr || !sb) return;
    AVAssetWriterInput* in = audio_;
    dispatch_async(queue_, ^{
      for (int tries = 0; tries < 200 && !in.readyForMoreMediaData; ++tries) usleep(1000);
      if (in.readyForMoreMediaData) [in appendSampleBuffer:sb];
      CFRelease(sb);
    });
  }

  bool stop(std::string& error) override {
    if (!active_) return false;
    active_ = false;
    dispatch_sync(queue_, ^{});  // drain pending appends
    [video_ markAsFinished];
    [audio_ markAsFinished];
    CMTime end = CMTimeMake(std::max<int64_t>(lastVideo_ + 1, 1), fps_);
    [writer_ endSessionAtSourceTime:end];
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [writer_ finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(sem); }];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 20LL * NSEC_PER_SEC));
    bool ok = writer_.status == AVAssetWriterStatusCompleted;
    if (!ok) error = writer_.error ? writer_.error.localizedDescription.UTF8String : "finishWriting failed";
    if (afmt_) CFRelease(afmt_);
    afmt_ = nullptr;
    writer_ = nil;
    video_ = audio_ = nil;
    adaptor_ = nil;
    return ok;
  }

  bool active() const override { return active_; }
  const char* backend() const override { return "AVFoundation (H.264/AAC)"; }

 private:
  AVAssetWriter* writer_ = nil;
  AVAssetWriterInput* video_ = nil;
  AVAssetWriterInput* audio_ = nil;
  AVAssetWriterInputPixelBufferAdaptor* adaptor_ = nil;
  CMAudioFormatDescriptionRef afmt_ = nullptr;
  dispatch_queue_t queue_ = nullptr;
  bool active_ = false;
  int w_ = 0, h_ = 0, fps_ = 30, rate_ = 48000, ch_ = 2;
  int64_t audioFrames_ = 0, lastVideo_ = -1;
};

}  // namespace

std::unique_ptr<Recorder> Recorder::create() { return std::make_unique<AVRecorder>(); }

std::string defaultRecordingPath() {
  @autoreleasepool {
    NSArray* dirs = NSSearchPathForDirectoriesInDomains(NSMoviesDirectory, NSUserDomainMask, YES);
    std::string base = dirs.count ? std::string([dirs[0] UTF8String]) : std::string(getenv("HOME") ?: ".");
    std::filesystem::path dir = std::filesystem::path(base) / "Opera Grenouille";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    char name[64];
    std::time_t t = std::time(nullptr);
    std::strftime(name, sizeof(name), "Take %Y-%m-%d %H.%M.%S.mp4", std::localtime(&t));
    return (dir / name).string();
  }
}

bool askSaveLocation(const std::string& current, std::string& chosen) {
  @autoreleasepool {
    NSSavePanel* panel = [NSSavePanel savePanel];
    std::filesystem::path p(current);
    panel.title = @"Save your Opéra Grenouille take";
    panel.nameFieldStringValue = [NSString stringWithUTF8String:p.filename().string().c_str()];
    panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:p.parent_path().string().c_str()]];
    panel.allowedContentTypes = @[ [UTType typeWithFilenameExtension:@"mp4"] ];
    if ([panel runModal] != NSModalResponseOK || !panel.URL) return false;
    chosen = panel.URL.path.UTF8String;
    if (chosen == current) return true;
    std::error_code ec;
    std::filesystem::remove(chosen, ec);
    std::filesystem::rename(current, chosen, ec);
    if (ec) {  // cross-volume: copy then delete
      std::filesystem::copy_file(current, chosen, ec);
      if (ec) return false;
      std::filesystem::remove(current, ec);
    }
    return true;
  }
}

bool openFileDialog(const std::string& title, const std::vector<std::string>& exts, std::string& out) {
  @autoreleasepool {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = [NSString stringWithUTF8String:title.c_str()];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    NSMutableArray* types = [NSMutableArray array];
    for (auto& e : exts) {
      UTType* t = [UTType typeWithFilenameExtension:[NSString stringWithUTF8String:e.c_str()]];
      if (t) [types addObject:t];
    }
    if (types.count) panel.allowedContentTypes = types;
    if ([panel runModal] != NSModalResponseOK || !panel.URL) return false;
    out = panel.URL.path.UTF8String;
    return true;
  }
}

bool saveFileDialog(const std::string& title, const std::string& defaultName, std::string& out) {
  @autoreleasepool {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = [NSString stringWithUTF8String:title.c_str()];
    panel.nameFieldStringValue = [NSString stringWithUTF8String:defaultName.c_str()];
    if ([panel runModal] != NSModalResponseOK || !panel.URL) return false;
    out = panel.URL.path.UTF8String;
    return true;
  }
}

}  // namespace og
