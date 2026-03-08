// gpustat.m — Per-process GPU utilization for macOS via IOKit
// Compile: clang -framework IOKit -framework CoreFoundation -o gpustat gpustat.m
// Usage:   gpustat [interval_ms]
//
// Outputs one line per GPU-using process: PID GPU%
// Designed to be called by sysmon.sh to merge GPU data into the process table.

#import <IOKit/IOKitLib.h>
#import <CoreFoundation/CoreFoundation.h>
#import <stdio.h>
#import <stdlib.h>
#import <string.h>
#import <unistd.h>

typedef struct {
    int pid;
    int64_t gpuTime;  // nanoseconds accumulated
} GPUEntry;

static int collectGPUStats(GPUEntry *entries, int maxEntries) {
    int count = 0;
    io_iterator_t iter;
    kern_return_t kr;

    // Find the GPU accelerator service(s)
    CFMutableDictionaryRef match = IOServiceMatching("IOAccelerator");
    if (!match) return 0;

    kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
    if (kr != KERN_SUCCESS) return 0;

    io_service_t accel;
    while ((accel = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        // Iterate over user clients (children) of the accelerator
        io_iterator_t childIter;
        kr = IORegistryEntryGetChildIterator(accel, kIOServicePlane, &childIter);
        if (kr != KERN_SUCCESS) {
            IOObjectRelease(accel);
            continue;
        }

        io_service_t child;
        while ((child = IOIteratorNext(childIter)) != IO_OBJECT_NULL && count < maxEntries) {
            CFMutableDictionaryRef props = NULL;
            kr = IORegistryEntryCreateCFProperties(child, &props, kCFAllocatorDefault, 0);
            if (kr != KERN_SUCCESS || !props) {
                IOObjectRelease(child);
                continue;
            }

            // Extract PID from "IOUserClientCreator" = "pid 449, WindowServer"
            int pid = -1;
            CFStringRef creator = CFDictionaryGetValue(props, CFSTR("IOUserClientCreator"));
            if (creator && CFGetTypeID(creator) == CFStringGetTypeID()) {
                char buf[256];
                if (CFStringGetCString(creator, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                    if (strncmp(buf, "pid ", 4) == 0) {
                        pid = atoi(buf + 4);
                    }
                }
            }

            // Extract accumulated GPU time from "AppUsage" array
            int64_t totalGPUTime = 0;
            CFArrayRef appUsage = CFDictionaryGetValue(props, CFSTR("AppUsage"));
            if (appUsage && CFGetTypeID(appUsage) == CFArrayGetTypeID()) {
                CFIndex n = CFArrayGetCount(appUsage);
                for (CFIndex i = 0; i < n; i++) {
                    CFDictionaryRef entry = CFArrayGetValueAtIndex(appUsage, i);
                    if (!entry || CFGetTypeID(entry) != CFDictionaryGetTypeID()) continue;

                    CFNumberRef gpuTimeRef = CFDictionaryGetValue(entry, CFSTR("accumulatedGPUTime"));
                    if (gpuTimeRef && CFGetTypeID(gpuTimeRef) == CFNumberGetTypeID()) {
                        int64_t t = 0;
                        CFNumberGetValue(gpuTimeRef, kCFNumberSInt64Type, &t);
                        totalGPUTime += t;
                    }
                }
            }

            if (pid > 0) {
                // Merge: if this PID already has an entry, add to it
                int found = 0;
                for (int i = 0; i < count; i++) {
                    if (entries[i].pid == pid) {
                        entries[i].gpuTime += totalGPUTime;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    entries[count].pid = pid;
                    entries[count].gpuTime = totalGPUTime;
                    count++;
                }
            }

            CFRelease(props);
            IOObjectRelease(child);
        }
        IOObjectRelease(childIter);
        IOObjectRelease(accel);
    }
    IOObjectRelease(iter);
    return count;
}

int main(int argc, char *argv[]) {
    int intervalMs = 1000;
    if (argc > 1) intervalMs = atoi(argv[1]);
    if (intervalMs < 100) intervalMs = 100;

    int maxEntries = 4096;
    GPUEntry *sample1 = calloc(maxEntries, sizeof(GPUEntry));
    GPUEntry *sample2 = calloc(maxEntries, sizeof(GPUEntry));

    int n1 = collectGPUStats(sample1, maxEntries);
    usleep(intervalMs * 1000);
    int n2 = collectGPUStats(sample2, maxEntries);

    double intervalSec = intervalMs / 1000.0;

    for (int i = 0; i < n2; i++) {
        int64_t prev = 0;
        for (int j = 0; j < n1; j++) {
            if (sample1[j].pid == sample2[i].pid) {
                prev = sample1[j].gpuTime;
                break;
            }
        }
        int64_t delta = sample2[i].gpuTime - prev;
        // accumulatedGPUTime is in nanoseconds
        double pct = (delta / (intervalSec * 1e9)) * 100.0;
        if (pct < 0) pct = 0;
        printf("%d %.1f\n", sample2[i].pid, pct);
    }

    free(sample1);
    free(sample2);
    return 0;
}
