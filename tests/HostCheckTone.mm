// umleitung host check — a tiny tone player the tap tests capture: plays a sine into one device's outputs.
// usage: umleitung_host_check_tone <deviceUID or "default"> <hz> <amplitude> <seconds>
// (a bundle with id studio.umleitung.hostcheck.tone, so taps by bundle id can find it)
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

int main (int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc < 5) { fprintf (stderr, "usage: %s uid hz amp seconds\n", argv[0]); return 2; }
        NSString* uid = [NSString stringWithUTF8String: argv[1]];
        const double hz = atof (argv[2]), amp = atof (argv[3]), secs = atof (argv[4]);
        AudioObjectID dev = kAudioObjectUnknown;
        if ([uid isEqualToString: @"default"])
        {
            AudioObjectPropertyAddress a { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
            UInt32 size = sizeof (dev);
            AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, 0, nullptr, &size, &dev);
        }
        else
        {
            CFStringRef s = (__bridge CFStringRef) uid;
            AudioValueTranslation t { &s, sizeof (s), &dev, sizeof (dev) };
            UInt32 size = sizeof (t);
            AudioObjectPropertyAddress a { kAudioHardwarePropertyDeviceForUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
            AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, 0, nullptr, &size, &t);
        }
        if (dev == kAudioObjectUnknown) { fprintf (stderr, "tone: no device %s\n", argv[1]); return 1; }
        Float64 rate = 48000;
        AudioObjectPropertyAddress ra { kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        UInt32 size = sizeof (rate);
        AudioObjectGetPropertyData (dev, &ra, 0, nullptr, &size, &rate);
        __block double phase = 0;
        const double inc = 2.0 * M_PI * hz / rate;
        AudioDeviceIOProcID proc = nullptr;
        AudioDeviceCreateIOProcIDWithBlock (&proc, dev, nullptr,
            ^(const AudioTimeStamp*, const AudioBufferList*, const AudioTimeStamp*, AudioBufferList* out, const AudioTimeStamp*)
            {
                if (out == nullptr || out->mNumberBuffers == 0) return;
                const UInt32 nch0 = out->mBuffers[0].mNumberChannels ? out->mBuffers[0].mNumberChannels : 1;
                const UInt32 frames = out->mBuffers[0].mDataByteSize / (sizeof (float) * nch0);
                double p = phase;
                for (UInt32 b = 0; b < out->mNumberBuffers; ++b)
                {
                    auto& buf = out->mBuffers[b];
                    if (buf.mData == nullptr) continue;
                    const UInt32 nch = buf.mNumberChannels ? buf.mNumberChannels : 1;
                    auto* d = static_cast<float*> (buf.mData);
                    p = phase;
                    for (UInt32 i = 0; i < frames; ++i, p += inc)
                        for (UInt32 c = 0; c < nch; ++c) d[i * nch + c] = (float) (amp * std::sin (p));
                }
                phase = std::fmod (p, 2.0 * M_PI);
            });
        AudioDeviceStart (dev, proc);
        printf ("tone: playing %.0f Hz at %.4f on %s (pid %d)\n", hz, amp, argv[1], getpid());
        fflush (stdout);
        usleep ((useconds_t) (secs * 1e6));
        AudioDeviceStop (dev, proc);
        AudioDeviceDestroyIOProcID (dev, proc);
    }
    return 0;
}
