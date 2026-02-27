// Viewpoints (MIT License) - See LICENSE file
#import <Cocoa/Cocoa.h>
#include "ColorPanelHelper.h"

@interface VPColorPanelTarget : NSObject
@property (assign) int brushIndex;
@property (assign) ColorChangedCallback callback;
@property (assign) void* userData;
- (void)colorChanged:(id)sender;
@end

@implementation VPColorPanelTarget

- (void)colorChanged:(id)sender {
    NSColorPanel* panel = [NSColorPanel sharedColorPanel];
    NSColor* color = [[panel color] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (!color) return;

    float r = (float)[color redComponent];
    float g = (float)[color greenComponent];
    float b = (float)[color blueComponent];
    float a = (float)[color alphaComponent];

    if (_callback)
        _callback(_brushIndex, r, g, b, a, _userData);
}

@end

static VPColorPanelTarget* sTarget = nil;

void ShowColorPanel(int brushIndex, float r, float g, float b, float a,
                    ColorChangedCallback callback, void* userData) {
    if (!sTarget)
        sTarget = [[VPColorPanelTarget alloc] init];

    sTarget.brushIndex = brushIndex;
    sTarget.callback = callback;
    sTarget.userData = userData;

    NSColorPanel* panel = [NSColorPanel sharedColorPanel];
    [panel setTarget:sTarget];
    [panel setAction:@selector(colorChanged:)];
    [panel setShowsAlpha:YES];
    [panel setContinuous:YES];

    NSColor* initial = [NSColor colorWithSRGBRed:r green:g blue:b alpha:a];
    [panel setColor:initial];

    [panel orderFront:nil];
}

void CloseColorPanel(void) {
    NSColorPanel* panel = [NSColorPanel sharedColorPanel];
    [panel close];
}
