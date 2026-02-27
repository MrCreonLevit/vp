// Viewpoints (MIT License) - See LICENSE file
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ColorChangedCallback)(int brushIndex, float r, float g, float b, float a, void* userData);

void ShowColorPanel(int brushIndex, float r, float g, float b, float a,
                    ColorChangedCallback callback, void* userData);
void CloseColorPanel(void);

#ifdef __cplusplus
}
#endif
