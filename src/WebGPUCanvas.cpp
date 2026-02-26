// Viewpoints (MIT License) - See LICENSE file
#include "WebGPUCanvas.h"
#include "WebGPUContext.h"
#include "ColorMap.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>

const char* SymbolName(int symbol) {
    switch (symbol) {
        case SYMBOL_CIRCLE:         return "Circle";
        case SYMBOL_SQUARE:         return "Square";
        case SYMBOL_DIAMOND:        return "Diamond";
        case SYMBOL_TRIANGLE_UP:    return "Triangle Up";
        case SYMBOL_TRIANGLE_DOWN:  return "Triangle Down";
        case SYMBOL_CROSS:          return "Cross";
        case SYMBOL_PLUS:           return "Plus";
        case SYMBOL_STAR:           return "Star";
        case SYMBOL_RING:           return "Ring";
        case SYMBOL_SQUARE_OUTLINE: return "Square Outline";
        default:                    return "Circle";
    }
}

#ifdef __WXMAC__
#include <Cocoa/Cocoa.h>
#include <QuartzCore/QuartzCore.h>
#endif

static void makeOrtho(float* m, float left, float right, float bottom, float top) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0]  = 2.0f / (right - left);
    m[5]  = 2.0f / (top - bottom);
    m[10] = -1.0f;
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[15] = 1.0f;
}

WebGPUCanvas::WebGPUCanvas(wxWindow* parent, WebGPUContext* ctx, int plotIndex)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS)
    , m_ctx(ctx)
    , m_plotIndex(plotIndex)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    Bind(wxEVT_PAINT, &WebGPUCanvas::OnPaint, this);
    Bind(wxEVT_SIZE, &WebGPUCanvas::OnSize, this);
    Bind(wxEVT_LEFT_DOWN, &WebGPUCanvas::OnMouse, this);
    Bind(wxEVT_LEFT_UP, &WebGPUCanvas::OnMouse, this);
    Bind(wxEVT_MIDDLE_DOWN, &WebGPUCanvas::OnMouse, this);
    Bind(wxEVT_MIDDLE_UP, &WebGPUCanvas::OnMouse, this);
    Bind(wxEVT_RIGHT_DOWN, &WebGPUCanvas::OnMouse, this);
    Bind(wxEVT_RIGHT_UP, &WebGPUCanvas::OnMouse, this);
    Bind(wxEVT_MOTION, &WebGPUCanvas::OnMouse, this);
    Bind(wxEVT_MOUSEWHEEL, &WebGPUCanvas::OnMouse, this);
    Bind(wxEVT_MAGNIFY, &WebGPUCanvas::OnMagnify, this);
    Bind(wxEVT_KEY_DOWN, &WebGPUCanvas::OnKeyDown, this);
    Bind(wxEVT_CHAR, &WebGPUCanvas::OnKeyDown, this);
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) {
        if (m_showTooltip && onPointHover)
            onPointHover(m_plotIndex, -1, 0, 0);
        event.Skip();
    });

    CallAfter([this]() { InitSurface(); });
}

WebGPUCanvas::~WebGPUCanvas() {
    Cleanup();
}

void WebGPUCanvas::SetDisplayIndices(std::vector<size_t> indices) {
    m_displayIndices = std::move(indices);
}

void WebGPUCanvas::SetPoints(std::vector<PointVertex> points) {
    m_basePositions.resize(points.size() * 2);
    m_baseColors.resize(points.size() * 3);
    for (size_t i = 0; i < points.size(); i++) {
        m_basePositions[i * 2] = points[i].x;
        m_basePositions[i * 2 + 1] = points[i].y;
        m_baseColors[i * 3] = points[i].r;
        m_baseColors[i * 3 + 1] = points[i].g;
        m_baseColors[i * 3 + 2] = points[i].b;
    }
    m_selection.assign(points.size(), 0);
    m_points = std::move(points);
    if (m_initialized) {
        UpdateVertexBuffer();
        UpdateHistograms();
        Refresh();
    }
}

void WebGPUCanvas::SetAxisInfo(const std::string& xLabel, const std::string& yLabel,
                               float xDataMin, float xDataMax,
                               float yDataMin, float yDataMax) {
    m_xLabel = xLabel;
    m_yLabel = yLabel;
    m_xDataMin = xDataMin;
    m_xDataMax = xDataMax;
    m_yDataMin = yDataMin;
    m_yDataMax = yDataMax;
    Refresh();
}

void WebGPUCanvas::SetPointSize(float size) {
    m_pointSize = size;
    Refresh();
}

void WebGPUCanvas::SetHistBins(int bins) {
    m_histBins = std::max(2, bins);
    Refresh();
}

void WebGPUCanvas::SetPanZoom(float panX, float panY, float zoomX, float zoomY) {
    m_panX = panX;
    m_panY = panY;
    m_zoomX = zoomX;
    m_zoomY = zoomY;
    Refresh();
}

void WebGPUCanvas::SetShowTooltip(bool show) {
    m_showTooltip = show;
    if (!show && onPointHover)
        onPointHover(m_plotIndex, -1, 0, 0);
}

int WebGPUCanvas::FindNearestPoint(int sx, int sy) {
    size_t numPoints = m_basePositions.size() / 2;
    if (numPoints == 0) return -1;

    float wx, wy;
    ScreenToWorld(sx, sy, wx, wy);

    wxSize size = GetClientSize();
    if (size.GetWidth() < 1 || size.GetHeight() < 1) return -1;

    float hw = 1.0f / m_zoomX;
    float hh = 1.0f / m_zoomY;
    float threshX = 10.0f / size.GetWidth() * 2.0f * hw;
    float threshY = 10.0f / size.GetHeight() * 2.0f * hh;
    float threshSq = threshX * threshX + threshY * threshY;

    int bestIdx = -1;
    float bestDistSq = threshSq;

    bool hasRotation = (std::abs(m_rotationY) > 0.01f);
    float cosA = 1.0f, sinA = 0.0f;
    if (hasRotation) {
        cosA = std::cos(m_rotationY * 3.14159265f / 180.0f);
        sinA = std::sin(m_rotationY * 3.14159265f / 180.0f);
    }

    for (size_t i = 0; i < numPoints; i++) {
        float px = m_basePositions[i * 2];
        float py = m_basePositions[i * 2 + 1];

        if (hasRotation && i < m_points.size()) {
            float pz = m_points[i].z;
            px = px * cosA + pz * sinA;
        }

        float dx = px - wx;
        float dy = py - wy;
        float distSq = dx * dx + dy * dy;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestIdx = static_cast<int>(i);
        }
    }

    return bestIdx;
}

size_t WebGPUCanvas::GetOriginalDataRow(int displayIndex) const {
    if (displayIndex < 0) return static_cast<size_t>(-1);
    if (!m_displayIndices.empty()) {
        if (displayIndex < static_cast<int>(m_displayIndices.size()))
            return m_displayIndices[displayIndex];
        return static_cast<size_t>(-1);
    }
    return static_cast<size_t>(displayIndex);
}

void WebGPUCanvas::SetShowUnselected(bool show) {
    m_showUnselected = show;
    UpdatePointColors();
    Update();
}

void WebGPUCanvas::SetShowGridLines(bool show) {
    m_showGridLines = show;
    Refresh();
}

void WebGPUCanvas::SetShowHistograms(bool show) {
    m_showHistograms = show;
    Refresh();
}

void WebGPUCanvas::SetRotation(float degrees) {
    m_rotationY = degrees;
    Refresh();
}

void WebGPUCanvas::SetBackground(float brightness) {
    m_bgBrightness = brightness;
    Refresh();
}

void WebGPUCanvas::SetUseAdditiveBlending(bool additive) {
    m_useAdditive = additive;
    Refresh();
}

void WebGPUCanvas::SetColorMap(int colorMap, int colorVariable) {
    m_colorMap = colorMap;
    m_colorVariable = colorVariable;
    RecomputeDensityColors();
}

void WebGPUCanvas::SetGridLinePositions(const std::vector<float>& xPositions,
                                        const std::vector<float>& yPositions) {
    m_gridXPositions = xPositions;
    m_gridYPositions = yPositions;
    Refresh();
}

void WebGPUCanvas::SetOpacity(float alpha) {
    m_opacity = alpha;
    // Update vertex alpha in-place (apply brush 0 opacity offset for unselected points)
    float brush0Opacity = m_opacity;
    if (!m_brushColors.empty())
        brush0Opacity = std::max(0.0f, std::min(1.0f, m_opacity + m_brushColors[0].opacityOffset / 100.0f));
    for (size_t i = 0; i < m_points.size(); i++) {
        if (!m_showUnselected && (i >= m_selection.size() || m_selection[i] == 0))
            m_points[i].a = 0.0f;
        else
            m_points[i].a = brush0Opacity;
    }
    if (m_initialized) {
        // Re-upload brush colors since brush alpha depends on m_opacity,
        // but preserve the selection overlay buffer
        if (!m_brushColors.empty() && m_brushColorGpuBuffer && m_brushParamsGpuBuffer) {
            float colorData[8 * 4] = {};
            for (size_t i = 0; i < m_brushColors.size() && i < 8; i++) {
                colorData[i * 4 + 0] = m_brushColors[i].r;
                colorData[i * 4 + 1] = m_brushColors[i].g;
                colorData[i * 4 + 2] = m_brushColors[i].b;
                float brushOpacity = std::max(0.0f, std::min(1.0f, m_opacity + m_brushColors[i].opacityOffset / 100.0f));
                colorData[i * 4 + 3] = (i == 0) ? m_brushColors[i].a : brushOpacity * m_brushColors[i].a;
            }
            wgpuQueueWriteBuffer(m_ctx->GetQueue(), m_brushColorGpuBuffer, 0, colorData, sizeof(colorData));
        }
        UpdateVertexBuffer();
        Refresh();
    }
}

void WebGPUCanvas::SetBrushColors(const std::vector<BrushColor>& colors) {
    m_brushColors = colors;
    // Upload brush data to GPU
    if (m_initialized && m_brushColorGpuBuffer && m_brushParamsGpuBuffer) {
        float colorData[8 * 4] = {};  // 8 vec4f: index 0=unselected, 1-7=brushes
        float paramData[8 * 4] = {};  // 8 vec4f
        for (size_t i = 0; i < colors.size() && i < 8; i++) {
            colorData[i * 4 + 0] = colors[i].r;
            colorData[i * 4 + 1] = colors[i].g;
            colorData[i * 4 + 2] = colors[i].b;
            float brushOpacity = std::max(0.0f, std::min(1.0f, m_opacity + colors[i].opacityOffset / 100.0f));
            colorData[i * 4 + 3] = (i == 0) ? colors[i].a : brushOpacity * colors[i].a;
            paramData[i * 4 + 0] = static_cast<float>(colors[i].symbol);
            paramData[i * 4 + 1] = std::max(0.1f, 1.0f + colors[i].sizeOffset / m_pointSize);
            paramData[i * 4 + 2] = colors[i].useVertexColor ? 1.0f : 0.0f;
            paramData[i * 4 + 3] = 0.0f;
        }
        auto queue = m_ctx->GetQueue();
        wgpuQueueWriteBuffer(queue, m_brushColorGpuBuffer, 0, colorData, sizeof(colorData));
        wgpuQueueWriteBuffer(queue, m_brushParamsGpuBuffer, 0, paramData, sizeof(paramData));

        // Update vertex alpha for brush 0 opacity offset
        float brush0Opacity = std::max(0.0f, std::min(1.0f, m_opacity + colors[0].opacityOffset / 100.0f));
        for (size_t i = 0; i < m_points.size(); i++) {
            if (!m_showUnselected && (i >= m_selection.size() || m_selection[i] == 0))
                m_points[i].a = 0.0f;
            else
                m_points[i].a = brush0Opacity;
        }
        UpdateVertexBuffer();

        Refresh();
    }
}

void WebGPUCanvas::SetSelection(const std::vector<int>& sel) {
    // If subsampled, remap full-dataset selection to display indices
    if (!m_displayIndices.empty()) {
        if (sel.size() < m_displayIndices.back() + 1) {
            fprintf(stderr, "  Plot %d: SetSelection SKIP: sel=%zu < displayIndices.back()+1=%zu\n",
                    m_plotIndex, sel.size(), m_displayIndices.back() + 1);
            return;
        }
        m_selection.resize(m_displayIndices.size());
        int selCount = 0;
        for (size_t i = 0; i < m_displayIndices.size(); i++) {
            m_selection[i] = sel[m_displayIndices[i]];
            if (m_selection[i] > 0) selCount++;
        }
        static int dbgCount = 0;
        if (dbgCount++ % 20 == 0) {
            int fullSelCount = 0;
            for (auto s : sel) if (s > 0) fullSelCount++;
            fprintf(stderr, "  Plot %d: remap %zu→%zu, fullSel=%d, displaySel=%d, points=%zu\n",
                    m_plotIndex, sel.size(), m_displayIndices.size(),
                    fullSelCount, selCount, m_points.size());
        }
    } else {
        if (sel.size() != m_selection.size()) return;
        m_selection = sel;
    }

    // Upload remapped selection to GPU storage buffer
    if (m_initialized && m_ctx) {
        auto device = m_ctx->GetDevice();
        auto queue = m_ctx->GetQueue();
        size_t numPoints = m_selection.size();  // use remapped size, not raw input

        // Resize GPU selection buffer if needed
        if (numPoints != m_selectionBufferSize) {
            if (m_selectionGpuBuffer) wgpuBufferRelease(m_selectionGpuBuffer);
            size_t bufSize = std::max(numPoints, (size_t)1) * sizeof(uint32_t);
            WGPUBufferDescriptor sDesc = {};
            sDesc.label = {"selection_gpu", WGPU_STRLEN};
            sDesc.size = bufSize;
            sDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            m_selectionGpuBuffer = wgpuDeviceCreateBuffer(device, &sDesc);
            m_selectionBufferSize = numPoints;

            // Recreate bind group with new buffer
            if (m_selectionBindGroup) wgpuBindGroupRelease(m_selectionBindGroup);
            WGPUBindGroupEntry entries[3] = {};
            entries[0].binding = 0; entries[0].buffer = m_selectionGpuBuffer;
            entries[0].offset = 0; entries[0].size = bufSize;
            entries[1].binding = 1; entries[1].buffer = m_brushColorGpuBuffer;
            entries[1].offset = 0; entries[1].size = 8 * 16;
            entries[2].binding = 2; entries[2].buffer = m_brushParamsGpuBuffer;
            entries[2].offset = 0; entries[2].size = 8 * 16;
            WGPUBindGroupDescriptor bgDesc = {};
            bgDesc.layout = m_ctx->GetSelectionBindGroupLayout();
            bgDesc.entryCount = 3;
            bgDesc.entries = entries;
            m_selectionBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
        }

        // Convert remapped selection to u32 and upload
        std::vector<uint32_t> selU32(numPoints);
        for (size_t i = 0; i < numPoints; i++)
            selU32[i] = static_cast<uint32_t>(m_selection[i]);
        wgpuQueueWriteBuffer(queue, m_selectionGpuBuffer, 0,
                             selU32.data(), numPoints * sizeof(uint32_t));
    }

    // When unselected points are hidden, we must rebuild the vertex buffer
    // so newly-unselected points get alpha=0 and newly-selected points become visible.
    if (!m_showUnselected) {
        UpdatePointColors();
    }

    // Clear the overlay buffer so old selections don't persist on screen.
    if (m_selVertexBuffer) {
        wgpuBufferRelease(m_selVertexBuffer);
        m_selVertexBuffer = nullptr;
    }
    m_selVertexCount = 0;
    Refresh();
}

void WebGPUCanvas::ClearSelection() {
    std::fill(m_selection.begin(), m_selection.end(), 0);
    m_showLastRect = false;
    m_hasLastRect = false;
    m_selRectVertexCount = 0;
    UpdatePointColors();
}

void WebGPUCanvas::InvertSelection() {
    for (auto& s : m_selection)
        s = (s == 0) ? 1 : 0;
    UpdatePointColors();
}

int WebGPUCanvas::GetSelectedCount() const {
    int count = 0;
    for (int s : m_selection)
        if (s > 0) count++;
    return count;
}

void WebGPUCanvas::SetActive(bool active) {
    m_isActive = active;
    Refresh();
}

void WebGPUCanvas::ResetView() {
    m_panX = 0.0f;
    m_panY = 0.0f;
    m_zoomX = 1.0f;
    m_zoomY = 1.0f;
    m_rotationY = 0.0f;
    if (m_initialized) {
        Refresh();
        Update();  // force immediate repaint
    }
}

void WebGPUCanvas::UpdatePointColors() {
    // Set BASE colors in the vertex buffer. The GPU shader overrides these
    // for selected points using the selection storage buffer + brush uniforms.
    // Symbol and sizeScale are always base values here — brush-specific
    // values come from the GPU uniform lookup only.
    float brush0Opacity = m_opacity;
    if (!m_brushColors.empty())
        brush0Opacity = std::max(0.0f, std::min(1.0f, m_opacity + m_brushColors[0].opacityOffset / 100.0f));
    for (size_t i = 0; i < m_points.size(); i++) {
        if (!m_showUnselected && (i >= m_selection.size() || m_selection[i] == 0)) {
            m_points[i].r = 0.0f;
            m_points[i].g = 0.0f;
            m_points[i].b = 0.0f;
            m_points[i].a = 0.0f;
        } else {
            m_points[i].r = (i * 3 < m_baseColors.size()) ? m_baseColors[i * 3] : 0.15f;
            m_points[i].g = (i * 3 + 1 < m_baseColors.size()) ? m_baseColors[i * 3 + 1] : 0.4f;
            m_points[i].b = (i * 3 + 2 < m_baseColors.size()) ? m_baseColors[i * 3 + 2] : 1.0f;
            m_points[i].a = brush0Opacity;
        }
        m_points[i].symbol = 0.0f;
        m_points[i].sizeScale = 1.0f;
    }

    // Build overlay buffer for all selected points (brushes 1-7).
    // The main pass skips these; they are drawn only here with alpha blending.
    // Vertex positions come from the point data; the shader reads brush color,
    // symbol, and size from uniforms via the overlay selection buffer.
    std::vector<PointVertex> selPoints;
    std::vector<uint32_t> selBrushIds;
    for (size_t i = 0; i < m_points.size(); i++) {
        int brushIdx = (i < m_selection.size()) ? m_selection[i] : 0;
        if (brushIdx > 0 && brushIdx < (int)m_brushColors.size()) {
            selPoints.push_back(m_points[i]);
            selBrushIds.push_back(static_cast<uint32_t>(brushIdx));
        }
    }
    m_selVertexCount = selPoints.size();

    if (m_initialized) {
        UpdateVertexBuffer();

        // Update selection vertex buffer
        auto device = m_ctx->GetDevice();
        auto queue = m_ctx->GetQueue();
        if (m_selVertexBuffer) {
            wgpuBufferRelease(m_selVertexBuffer);
            m_selVertexBuffer = nullptr;
        }
        if (m_overlaySelBuffer) { wgpuBufferRelease(m_overlaySelBuffer); m_overlaySelBuffer = nullptr; }
        if (m_overlayBindGroup) { wgpuBindGroupRelease(m_overlayBindGroup); m_overlayBindGroup = nullptr; }
        if (!selPoints.empty()) {
            size_t dataSize = selPoints.size() * sizeof(PointVertex);
            WGPUBufferDescriptor sbDesc = {};
            sbDesc.label = {"sel_instances", WGPU_STRLEN};
            sbDesc.size = dataSize;
            sbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            m_selVertexBuffer = wgpuDeviceCreateBuffer(device, &sbDesc);
            wgpuQueueWriteBuffer(queue, m_selVertexBuffer, 0, selPoints.data(), dataSize);

            // Selection buffer with correct brush indices for overlay points
            size_t selSize = selBrushIds.size() * sizeof(uint32_t);
            WGPUBufferDescriptor zDesc = {};
            zDesc.label = {"overlay_sel", WGPU_STRLEN};
            zDesc.size = selSize;
            zDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            m_overlaySelBuffer = wgpuDeviceCreateBuffer(device, &zDesc);
            wgpuQueueWriteBuffer(queue, m_overlaySelBuffer, 0, selBrushIds.data(), selSize);

            WGPUBindGroupEntry entries[3] = {};
            entries[0].binding = 0; entries[0].buffer = m_overlaySelBuffer;
            entries[0].offset = 0; entries[0].size = selSize;
            entries[1].binding = 1; entries[1].buffer = m_brushColorGpuBuffer;
            entries[1].offset = 0; entries[1].size = 8 * 16;
            entries[2].binding = 2; entries[2].buffer = m_brushParamsGpuBuffer;
            entries[2].offset = 0; entries[2].size = 8 * 16;
            WGPUBindGroupDescriptor bgDesc = {};
            bgDesc.layout = m_ctx->GetSelectionBindGroupLayout();
            bgDesc.entryCount = 3;
            bgDesc.entries = entries;
            m_overlayBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
        }

        UpdateHistograms();
        Refresh();
    }
}

void WebGPUCanvas::ScreenToWorld(int sx, int sy, float& wx, float& wy) {
    wxSize size = GetClientSize();
    float ndcX = (static_cast<float>(sx) / size.GetWidth()) * 2.0f - 1.0f;
    float ndcY = 1.0f - (static_cast<float>(sy) / size.GetHeight()) * 2.0f;
    float hw = 1.0f / m_zoomX;
    float hh = 1.0f / m_zoomY;
    wx = m_panX + ndcX * hw;
    wy = m_panY + ndcY * hh;
}

void WebGPUCanvas::WorldToScreen(float wx, float wy, int& sx, int& sy) {
    wxSize size = GetClientSize();
    float hw = 1.0f / m_zoomX;
    float hh = 1.0f / m_zoomY;
    float ndcX = (wx - m_panX) / hw;
    float ndcY = (wy - m_panY) / hh;
    sx = static_cast<int>((ndcX + 1.0f) * 0.5f * size.GetWidth());
    sy = static_cast<int>((1.0f - ndcY) * 0.5f * size.GetHeight());
}

void WebGPUCanvas::InitSurface() {
    if (!m_ctx || !m_ctx->IsInitialized())
        return;

    auto device = m_ctx->GetDevice();
    auto instance = m_ctx->GetInstance();

#ifdef __WXMAC__
    NSView* nsView = (NSView*)GetHandle();
    if (!nsView) {
        fprintf(stderr, "Canvas %d: Failed to get NSView\n", m_plotIndex);
        return;
    }

    [nsView setWantsLayer:YES];

    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.contentsScale = nsView.window.backingScaleFactor;
    metalLayer.frame = nsView.bounds;
    metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    [nsView.layer addSublayer:metalLayer];
    m_metalLayer = (__bridge void*)metalLayer;

    WGPUSurfaceSourceMetalLayer metalDesc = {};
    metalDesc.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    metalDesc.layer = (__bridge void*)metalLayer;

    WGPUSurfaceDescriptor surfDesc = {};
    surfDesc.nextInChain = &metalDesc.chain;
    m_surface = wgpuInstanceCreateSurface(instance, &surfDesc);
#else
    fprintf(stderr, "Platform not supported\n");
    return;
#endif

    if (!m_surface) {
        fprintf(stderr, "Canvas %d: Failed to create surface\n", m_plotIndex);
        return;
    }

    // Get surface format
    auto adapter = m_ctx->GetAdapter();
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(m_surface, adapter, &caps);
    m_surfaceFormat = caps.formats[0];
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    CreatePipeline();

    // Create uniform buffer
    WGPUBufferDescriptor ubDesc = {};
    ubDesc.label = {"uniforms", WGPU_STRLEN};
    ubDesc.size = sizeof(Uniforms);
    ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(device, &ubDesc);

    // Create bind group for point rendering
    WGPUBindGroupEntry bgEntry = {};
    bgEntry.binding = 0;
    bgEntry.buffer = m_uniformBuffer;
    bgEntry.offset = 0;
    bgEntry.size = sizeof(Uniforms);
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = m_ctx->GetBindGroupLayout();
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    m_bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);

    // Create GPU selection buffer and brush uniform buffers (bind group 1)
    {
        // Selection buffer: start with 1 element, will resize in SetPoints/SetSelection
        WGPUBufferDescriptor selDesc = {};
        selDesc.label = {"selection_gpu", WGPU_STRLEN};
        selDesc.size = 4;  // minimum 1 u32
        selDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        m_selectionGpuBuffer = wgpuDeviceCreateBuffer(device, &selDesc);
        m_selectionBufferSize = 1;

        // Brush colors: 8 × vec4f (RGBA)
        WGPUBufferDescriptor bcDesc = {};
        bcDesc.label = {"brush_colors", WGPU_STRLEN};
        bcDesc.size = 8 * 16;
        bcDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        m_brushColorGpuBuffer = wgpuDeviceCreateBuffer(device, &bcDesc);

        // Brush params: 8 × vec4f (symbol, sizeScale, 0, 0)
        WGPUBufferDescriptor bpDesc = {};
        bpDesc.label = {"brush_params", WGPU_STRLEN};
        bpDesc.size = 8 * 16;
        bpDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        m_brushParamsGpuBuffer = wgpuDeviceCreateBuffer(device, &bpDesc);

        // Create selection bind group (group 1)
        WGPUBindGroupEntry selEntries[3] = {};
        selEntries[0].binding = 0;
        selEntries[0].buffer = m_selectionGpuBuffer;
        selEntries[0].offset = 0;
        selEntries[0].size = 4;
        selEntries[1].binding = 1;
        selEntries[1].buffer = m_brushColorGpuBuffer;
        selEntries[1].offset = 0;
        selEntries[1].size = 8 * 16;
        selEntries[2].binding = 2;
        selEntries[2].buffer = m_brushParamsGpuBuffer;
        selEntries[2].offset = 0;
        selEntries[2].size = 8 * 16;

        WGPUBindGroupDescriptor selBgDesc = {};
        selBgDesc.layout = m_ctx->GetSelectionBindGroupLayout();
        selBgDesc.entryCount = 3;
        selBgDesc.entries = selEntries;
        m_selectionBindGroup = wgpuDeviceCreateBindGroup(device, &selBgDesc);
    }

    // Create separate uniform buffer + bind group for histograms (identity projection)
    WGPUBufferDescriptor hubDesc = {};
    hubDesc.label = {"hist_uniforms", WGPU_STRLEN};
    hubDesc.size = sizeof(Uniforms);
    hubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_histUniformBuffer = wgpuDeviceCreateBuffer(device, &hubDesc);

    Uniforms histUniforms = {};
    makeOrtho(histUniforms.projection, -1.0f, 1.0f, -1.0f, 1.0f);
    wgpuQueueWriteBuffer(m_ctx->GetQueue(), m_histUniformBuffer, 0,
                         &histUniforms, sizeof(Uniforms));

    WGPUBindGroupEntry hbgEntry = {};
    hbgEntry.binding = 0;
    hbgEntry.buffer = m_histUniformBuffer;
    hbgEntry.offset = 0;
    hbgEntry.size = sizeof(Uniforms);
    WGPUBindGroupDescriptor hbgDesc = {};
    hbgDesc.layout = m_ctx->GetBindGroupLayout();
    hbgDesc.entryCount = 1;
    hbgDesc.entries = &hbgEntry;
    m_histBindGroup = wgpuDeviceCreateBindGroup(device, &hbgDesc);

    if (!m_points.empty())
        UpdateVertexBuffer();

    wxSize size = GetClientSize();
    double scale = GetContentScaleFactor();
    ConfigureSurface(static_cast<int>(size.GetWidth() * scale),
                     static_cast<int>(size.GetHeight() * scale));

    m_initialized = true;

    // Upload brush colors/params to GPU (may have been set before InitSurface ran)
    if (!m_brushColors.empty()) {
        SetBrushColors(m_brushColors);
    }

    // Upload selection state if already set
    if (!m_selection.empty()) {
        SetSelection(m_selection);
    }

    Refresh();
}

void WebGPUCanvas::CreatePipeline() {
    auto device = m_ctx->GetDevice();
    auto shaderModule = m_ctx->GetShaderModule();
    auto pipelineLayout = m_ctx->GetPipelineLayout();

    WGPUVertexAttribute quadAttr = {};
    quadAttr.format = WGPUVertexFormat_Float32x2;
    quadAttr.offset = 0;
    quadAttr.shaderLocation = 0;

    WGPUVertexAttribute instanceAttrs[4] = {};
    instanceAttrs[0].format = WGPUVertexFormat_Float32x3;  // position (x, y, z)
    instanceAttrs[0].offset = offsetof(PointVertex, x);
    instanceAttrs[0].shaderLocation = 1;
    instanceAttrs[1].format = WGPUVertexFormat_Float32x4;  // color
    instanceAttrs[1].offset = offsetof(PointVertex, r);
    instanceAttrs[1].shaderLocation = 2;
    instanceAttrs[2].format = WGPUVertexFormat_Float32;    // symbol
    instanceAttrs[2].offset = offsetof(PointVertex, symbol);
    instanceAttrs[2].shaderLocation = 3;
    instanceAttrs[3].format = WGPUVertexFormat_Float32;    // sizeScale
    instanceAttrs[3].offset = offsetof(PointVertex, sizeScale);
    instanceAttrs[3].shaderLocation = 4;

    WGPUVertexBufferLayout vbLayouts[2] = {};
    vbLayouts[0].arrayStride = 2 * sizeof(float);
    vbLayouts[0].stepMode = WGPUVertexStepMode_Vertex;
    vbLayouts[0].attributeCount = 1;
    vbLayouts[0].attributes = &quadAttr;
    vbLayouts[1].arrayStride = sizeof(PointVertex);
    vbLayouts[1].stepMode = WGPUVertexStepMode_Instance;
    vbLayouts[1].attributeCount = 4;
    vbLayouts[1].attributes = instanceAttrs;

    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_One;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_One;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = m_surfaceFormat;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = {"fs_main", WGPU_STRLEN};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor rpDesc = {};
    rpDesc.label = {"point_pipeline", WGPU_STRLEN};
    rpDesc.layout = pipelineLayout;
    rpDesc.vertex.module = shaderModule;
    rpDesc.vertex.entryPoint = {"vs_main", WGPU_STRLEN};
    rpDesc.vertex.bufferCount = 2;
    rpDesc.vertex.buffers = vbLayouts;
    rpDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpDesc.fragment = &fragmentState;
    rpDesc.multisample.count = 1;
    rpDesc.multisample.mask = 0xFFFFFFFF;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &rpDesc);

    // Selection overlay pipeline: same vertex layout but standard alpha blending
    // so selected points are drawn opaquely on top of the additive background
    WGPUBlendState selBlend = {};
    selBlend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    selBlend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    selBlend.color.operation = WGPUBlendOperation_Add;
    selBlend.alpha.srcFactor = WGPUBlendFactor_One;
    selBlend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    selBlend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState selColorTarget = {};
    selColorTarget.format = m_surfaceFormat;
    selColorTarget.blend = &selBlend;
    selColorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState selFragState = {};
    selFragState.module = shaderModule;
    selFragState.entryPoint = {"fs_main", WGPU_STRLEN};
    selFragState.targetCount = 1;
    selFragState.targets = &selColorTarget;

    WGPURenderPipelineDescriptor selRpDesc = {};
    selRpDesc.label = {"sel_pipeline", WGPU_STRLEN};
    selRpDesc.layout = pipelineLayout;
    selRpDesc.vertex.module = shaderModule;
    selRpDesc.vertex.entryPoint = {"vs_main", WGPU_STRLEN};
    selRpDesc.vertex.bufferCount = 2;
    selRpDesc.vertex.buffers = vbLayouts;
    selRpDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    selRpDesc.fragment = &selFragState;
    selRpDesc.multisample.count = 1;
    selRpDesc.multisample.mask = 0xFFFFFFFF;

    m_selPipeline = wgpuDeviceCreateRenderPipeline(device, &selRpDesc);

    // Histogram pipeline: simple triangles with per-vertex position+color
    // Reuse the same shader but with a single vertex buffer (no instancing)
    // The histogram vertices are pre-transformed to clip space, so vs_main
    // just passes them through via the projection uniform set to identity-like.
    // Actually, we can use the same projection - histogram bars are in the
    // same coordinate space as points.

    // For histograms, we render PointVertex directly as triangles (not instanced).
    // We use the instance buffer layout but as per-vertex, with a dummy quad buffer.
    // Simpler: create a new pipeline with just one vertex buffer.
    WGPUVertexAttribute histAttrs[2] = {};
    histAttrs[0].format = WGPUVertexFormat_Float32x2;
    histAttrs[0].offset = offsetof(PointVertex, x);
    histAttrs[0].shaderLocation = 1;  // maps to point_pos in shader
    histAttrs[1].format = WGPUVertexFormat_Float32x4;
    histAttrs[1].offset = offsetof(PointVertex, r);
    histAttrs[1].shaderLocation = 2;  // maps to point_color in shader

    WGPUVertexBufferLayout histVbLayout = {};
    histVbLayout.arrayStride = sizeof(PointVertex);
    histVbLayout.stepMode = WGPUVertexStepMode_Vertex;
    histVbLayout.attributeCount = 2;
    histVbLayout.attributes = histAttrs;

    // Alpha blending (not additive) for histogram bars
    WGPUBlendState histBlend = {};
    histBlend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    histBlend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    histBlend.color.operation = WGPUBlendOperation_Add;
    histBlend.alpha.srcFactor = WGPUBlendFactor_One;
    histBlend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    histBlend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState histColorTarget = {};
    histColorTarget.format = m_surfaceFormat;
    histColorTarget.blend = &histBlend;
    histColorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState histFragState = {};
    histFragState.module = shaderModule;
    histFragState.entryPoint = {"fs_main", WGPU_STRLEN};
    histFragState.targetCount = 1;
    histFragState.targets = &histColorTarget;

    // We need a special vertex shader for histograms that doesn't use
    // the quad expansion. We'll create a simple pass-through by using
    // point_pos as position directly (location 1) with a dummy location 0.
    // But actually, we can just use the quad buffer with vertex (0,0) so the
    // offset is zero and point_pos becomes the position directly.
    // Simpler: just put histogram vertices in world space and use the same
    // projection. Skip the quad expansion by not binding the quad buffer.

    // Actually the cleanest approach: create histogram vertices as PointVertex
    // in world space, use the instanced pipeline with instance count 1 per bar
    // vertex... no, that's wrong.

    // Let me just use a separate simple shader embedded here for histograms.
    // This avoids all the instancing complexity.

    static const char* kHistShaderSource = R"(
struct Uniforms {
    projection: mat4x4f,
    point_size: f32,
    viewport_w: f32,
    viewport_h: f32,
    _pad: f32,
}
@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
}

@vertex
fn hist_vs(@location(0) pos: vec2f, @location(1) color: vec4f) -> VertexOutput {
    var output: VertexOutput;
    output.position = uniforms.projection * vec4f(pos, 0.0, 1.0);
    output.color = color;
    return output;
}

@fragment
fn hist_fs(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
)";

    WGPUShaderSourceWGSL histWgsl = {};
    histWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    histWgsl.code = {kHistShaderSource, WGPU_STRLEN};
    WGPUShaderModuleDescriptor histSmDesc = {};
    histSmDesc.nextInChain = &histWgsl.chain;
    WGPUShaderModule histShader = wgpuDeviceCreateShaderModule(device, &histSmDesc);

    // Histogram vertex layout: vec2f position (location 0) + vec4f color (location 1)
    WGPUVertexAttribute histAttrs2[2] = {};
    histAttrs2[0].format = WGPUVertexFormat_Float32x2;
    histAttrs2[0].offset = offsetof(PointVertex, x);
    histAttrs2[0].shaderLocation = 0;
    histAttrs2[1].format = WGPUVertexFormat_Float32x4;
    histAttrs2[1].offset = offsetof(PointVertex, r);
    histAttrs2[1].shaderLocation = 1;

    WGPUVertexBufferLayout histLayout = {};
    histLayout.arrayStride = sizeof(PointVertex);
    histLayout.stepMode = WGPUVertexStepMode_Vertex;
    histLayout.attributeCount = 2;
    histLayout.attributes = histAttrs2;

    histFragState.module = histShader;
    histFragState.entryPoint = {"hist_fs", WGPU_STRLEN};

    WGPURenderPipelineDescriptor histRpDesc = {};
    histRpDesc.label = {"hist_pipeline", WGPU_STRLEN};
    histRpDesc.layout = m_ctx->GetHistPipelineLayout();
    histRpDesc.vertex.module = histShader;
    histRpDesc.vertex.entryPoint = {"hist_vs", WGPU_STRLEN};
    histRpDesc.vertex.bufferCount = 1;
    histRpDesc.vertex.buffers = &histLayout;
    histRpDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    histRpDesc.fragment = &histFragState;
    histRpDesc.multisample.count = 1;
    histRpDesc.multisample.mask = 0xFFFFFFFF;

    m_histPipeline = wgpuDeviceCreateRenderPipeline(device, &histRpDesc);
    wgpuShaderModuleRelease(histShader);
}

void WebGPUCanvas::UpdateVertexBuffer() {
    if (!m_ctx || m_points.empty())
        return;
    auto device = m_ctx->GetDevice();
    auto queue = m_ctx->GetQueue();

    size_t dataSize = m_points.size() * sizeof(PointVertex);

    if (m_vertexBuffer) {
        wgpuBufferRelease(m_vertexBuffer);
        m_vertexBuffer = nullptr;
    }

    WGPUBufferDescriptor vbDesc = {};
    vbDesc.label = {"point_instances", WGPU_STRLEN};
    vbDesc.size = dataSize;
    vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_vertexBuffer = wgpuDeviceCreateBuffer(device, &vbDesc);
    wgpuQueueWriteBuffer(queue, m_vertexBuffer, 0, m_points.data(), dataSize);
}

void WebGPUCanvas::UpdateHistograms() {
    if (!m_ctx || m_basePositions.empty() || !m_showHistograms)
        return;

    int numBins = m_histBins;
    constexpr float HIST_HEIGHT = 0.3f;
    size_t numPoints = m_basePositions.size() / 2;

    float hw = 1.0f / m_zoomX;
    float hh = 1.0f / m_zoomY;
    float xViewMin = m_panX - hw;
    float xViewMax = m_panX + hw;
    float yViewMin = m_panY - hh;
    float yViewMax = m_panY + hh;

    float xBinWidth = (xViewMax - xViewMin) / numBins;
    float yBinWidth = (yViewMax - yViewMin) / numBins;

    std::vector<int> xBinsAll(numBins, 0), yBinsAll(numBins, 0);
    std::vector<int> xBinsSel(numBins, 0), yBinsSel(numBins, 0);

    for (size_t i = 0; i < numPoints; i++) {
        float px = m_basePositions[i * 2];
        float py = m_basePositions[i * 2 + 1];
        bool selected = (i < m_selection.size() && m_selection[i] > 0);

        if (px >= xViewMin && px <= xViewMax) {
            int xBin = static_cast<int>((px - xViewMin) / xBinWidth);
            xBin = std::max(0, std::min(xBin, numBins - 1));
            xBinsAll[xBin]++;
            if (selected) xBinsSel[xBin]++;
        }

        if (py >= yViewMin && py <= yViewMax) {
            int yBin = static_cast<int>((py - yViewMin) / yBinWidth);
            yBin = std::max(0, std::min(yBin, numBins - 1));
            yBinsAll[yBin]++;
            if (selected) yBinsSel[yBin]++;
        }
    }

    int xMaxAll = *std::max_element(xBinsAll.begin(), xBinsAll.end());
    int yMaxAll = *std::max_element(yBinsAll.begin(), yBinsAll.end());
    if (xMaxAll == 0) xMaxAll = 1;
    if (yMaxAll == 0) yMaxAll = 1;

    std::vector<PointVertex> histVerts;
    histVerts.reserve(numBins * 24 * 2);  // outline bars use more vertices

    // Filled rectangle helper
    auto addFilledBar = [&](float x0, float y0, float x1, float y1,
                            float r, float g, float b, float a) {
        PointVertex tl = {x0, y1, 0, r, g, b, a};
        PointVertex tr = {x1, y1, 0, r, g, b, a};
        PointVertex bl = {x0, y0, 0, r, g, b, a};
        PointVertex br = {x1, y0, 0, r, g, b, a};
        histVerts.push_back(bl); histVerts.push_back(br); histVerts.push_back(tr);
        histVerts.push_back(bl); histVerts.push_back(tr); histVerts.push_back(tl);
    };

    // Staircase histogram outline: a single polyline that steps up/down
    // avoiding double-thick lines where adjacent bars share an edge.
    // Each segment is a thin quad (line segment with thickness).
    float lineT = 0.003f;  // line thickness in clip space

    // Helper: draw a thin horizontal line segment
    auto addHLine = [&](float x0, float x1, float y,
                        float r, float g, float b, float a) {
        addFilledBar(x0, y - lineT * 0.5f, x1, y + lineT * 0.5f, r, g, b, a);
    };
    // Helper: draw a thin vertical line segment
    auto addVLine = [&](float x, float y0, float y1,
                        float r, float g, float b, float a) {
        addFilledBar(x - lineT * 0.5f, y0, x + lineT * 0.5f, y1, r, g, b, a);
    };

    // Draw a staircase histogram outline for a horizontal histogram (bars go up from base)
    auto addStaircaseX = [&](const std::vector<int>& bins, int maxBin, float base,
                             float r, float g, float b, float a) {
        float binW = 2.0f / numBins;
        float prevH = 0.0f;
        for (int i = 0; i < numBins; i++) {
            float h = (static_cast<float>(bins[i]) / maxBin) * HIST_HEIGHT;
            float x = -1.0f + i * binW;
            // Vertical step from previous height to this height
            if (std::abs(h - prevH) > 0.001f) {
                float lo = base + std::min(prevH, h);
                float hi = base + std::max(prevH, h);
                addVLine(x, lo, hi, r, g, b, a);
            }
            // Horizontal top of this bin
            if (h > 0.001f)
                addHLine(x, x + binW, base + h, r, g, b, a);
            prevH = h;
        }
        // Final right edge back to baseline
        if (prevH > 0.001f)
            addVLine(1.0f, base, base + prevH, r, g, b, a);
    };

    // Draw a staircase histogram outline for a vertical histogram (bars go right from base)
    auto addStaircaseY = [&](const std::vector<int>& bins, int maxBin, float base,
                             float r, float g, float b, float a) {
        float binW = 2.0f / numBins;
        float prevH = 0.0f;
        for (int i = 0; i < numBins; i++) {
            float h = (static_cast<float>(bins[i]) / maxBin) * HIST_HEIGHT;
            float y = -1.0f + i * binW;
            // Horizontal step from previous height to this height
            if (std::abs(h - prevH) > 0.001f) {
                float lo = base + std::min(prevH, h);
                float hi = base + std::max(prevH, h);
                addHLine(lo, hi, y, r, g, b, a);
            }
            // Vertical right edge of this bin
            if (h > 0.001f)
                addVLine(base + h, y, y + binW, r, g, b, a);
            prevH = h;
        }
        // Final top edge back to baseline
        if (prevH > 0.001f)
            addHLine(base, base + prevH, 1.0f, r, g, b, a);
    };

    // X histogram (bottom edge)
    addStaircaseX(xBinsAll, xMaxAll, -1.0f, 0.3f, 0.5f, 0.8f, 0.5f);
    addStaircaseX(xBinsSel, xMaxAll, -1.0f, 1.0f, 0.5f, 0.2f, 0.7f);

    // Y histogram (left edge)
    addStaircaseY(yBinsAll, yMaxAll, -1.0f, 0.3f, 0.5f, 0.8f, 0.5f);
    addStaircaseY(yBinsSel, yMaxAll, -1.0f, 1.0f, 0.5f, 0.2f, 0.7f);

    m_histVertexCount = histVerts.size();

    if (m_histVertexCount == 0)
        return;

    auto device = m_ctx->GetDevice();
    auto queue = m_ctx->GetQueue();

    if (m_histBuffer) {
        wgpuBufferRelease(m_histBuffer);
        m_histBuffer = nullptr;
    }

    size_t dataSize = m_histVertexCount * sizeof(PointVertex);
    WGPUBufferDescriptor hbDesc = {};
    hbDesc.label = {"histogram", WGPU_STRLEN};
    hbDesc.size = dataSize;
    hbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_histBuffer = wgpuDeviceCreateBuffer(device, &hbDesc);
    wgpuQueueWriteBuffer(queue, m_histBuffer, 0, histVerts.data(), dataSize);
}

static std::vector<float> computeNiceTicks(float rangeMin, float rangeMax, int approxCount) {
    float range = rangeMax - rangeMin;
    if (range <= 0.0f) return {};
    float roughStep = range / approxCount;
    float mag = std::pow(10.0f, std::floor(std::log10(roughStep)));
    float residual = roughStep / mag;
    float niceStep;
    if (residual <= 1.5f) niceStep = mag;
    else if (residual <= 3.5f) niceStep = 2.0f * mag;
    else if (residual <= 7.5f) niceStep = 5.0f * mag;
    else niceStep = 10.0f * mag;

    float start = std::ceil(rangeMin / niceStep) * niceStep;
    std::vector<float> ticks;
    for (float v = start; v <= rangeMax; v += niceStep)
        ticks.push_back(v);
    return ticks;
}

void WebGPUCanvas::UpdateGridLines() {
    if (!m_ctx || !m_showGridLines ||
        (m_gridXPositions.empty() && m_gridYPositions.empty())) {
        m_gridLineVertexCount = 0;
        return;
    }

    std::vector<PointVertex> lineVerts;
    float lineT = 0.002f;
    float r = 0.3f, g = 0.3f, b = 0.4f, a = 0.5f;

    auto addFilledBar = [&](float x0, float y0, float x1, float y1) {
        PointVertex tl = {x0, y1, 0, r, g, b, a};
        PointVertex tr = {x1, y1, 0, r, g, b, a};
        PointVertex bl = {x0, y0, 0, r, g, b, a};
        PointVertex br = {x1, y0, 0, r, g, b, a};
        lineVerts.push_back(bl); lineVerts.push_back(br); lineVerts.push_back(tr);
        lineVerts.push_back(bl); lineVerts.push_back(tr); lineVerts.push_back(tl);
    };

    // Grid lines at explicit clip-space positions (set by MainFrame)
    for (float xClip : m_gridXPositions) {
        if (xClip > -0.99f && xClip < 0.99f)
            addFilledBar(xClip - lineT, -1.0f, xClip + lineT, 1.0f);
    }
    for (float yClip : m_gridYPositions) {
        if (yClip > -0.99f && yClip < 0.99f)
            addFilledBar(-1.0f, yClip - lineT, 1.0f, yClip + lineT);
    }

    m_gridLineVertexCount = lineVerts.size();
    if (m_gridLineVertexCount == 0) return;

    auto device = m_ctx->GetDevice();
    auto queue = m_ctx->GetQueue();

    if (m_gridLineBuffer) {
        wgpuBufferRelease(m_gridLineBuffer);
        m_gridLineBuffer = nullptr;
    }

    size_t dataSize = m_gridLineVertexCount * sizeof(PointVertex);
    WGPUBufferDescriptor glDesc = {};
    glDesc.label = {"grid_lines", WGPU_STRLEN};
    glDesc.size = dataSize;
    glDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_gridLineBuffer = wgpuDeviceCreateBuffer(device, &glDesc);
    wgpuQueueWriteBuffer(queue, m_gridLineBuffer, 0, lineVerts.data(), dataSize);
}

void WebGPUCanvas::UpdateSelectionRect() {
    if (!m_ctx || !m_selecting) {
        m_selRectVertexCount = 0;
        return;
    }

    float wx0, wy0, wx1, wy1;
    ScreenToWorld(m_selectStart.x, m_selectStart.y, wx0, wy0);
    ScreenToWorld(m_selectEnd.x, m_selectEnd.y, wx1, wy1);

    // Fire coordinate callback
    if (onSelectionDrag)
        onSelectionDrag(m_plotIndex, std::min(wx0, wx1), std::min(wy0, wy1),
                        std::max(wx0, wx1), std::max(wy0, wy1));

    // Build outline as 4 thin quads in world space (rendered with main projection)
    float t = 0.003f / m_zoomX;  // scale thickness by zoom so it stays visible
    float tY = 0.003f / m_zoomY;
    float left = std::min(wx0, wx1), right = std::max(wx0, wx1);
    float bottom = std::min(wy0, wy1), top = std::max(wy0, wy1);

    std::vector<PointVertex> verts;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 0.8f;
    auto addBar = [&](float x0, float y0, float x1, float y1) {
        PointVertex tl = {x0, y1, 0, r, g, b, a, 1.0f, 1.0f};
        PointVertex tr = {x1, y1, 0, r, g, b, a, 1.0f, 1.0f};
        PointVertex bl = {x0, y0, 0, r, g, b, a, 1.0f, 1.0f};
        PointVertex br = {x1, y0, 0, r, g, b, a, 1.0f, 1.0f};
        verts.push_back(bl); verts.push_back(br); verts.push_back(tr);
        verts.push_back(bl); verts.push_back(tr); verts.push_back(tl);
    };

    addBar(left, bottom - tY, right, bottom + tY);   // bottom
    addBar(left, top - tY, right, top + tY);          // top
    addBar(left - t, bottom, left + t, top);           // left
    addBar(right - t, bottom, right + t, top);         // right

    m_selRectVertexCount = verts.size();

    auto device = m_ctx->GetDevice();
    auto queue = m_ctx->GetQueue();

    if (m_selRectBuffer) { wgpuBufferRelease(m_selRectBuffer); m_selRectBuffer = nullptr; }

    size_t dataSize = m_selRectVertexCount * sizeof(PointVertex);
    WGPUBufferDescriptor desc = {};
    desc.label = {"sel_rect", WGPU_STRLEN};
    desc.size = dataSize;
    desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_selRectBuffer = wgpuDeviceCreateBuffer(device, &desc);
    wgpuQueueWriteBuffer(queue, m_selRectBuffer, 0, verts.data(), dataSize);
}

void WebGPUCanvas::BuildSelRectFromWorld() {
    if (!m_ctx || !m_hasLastRect) {
        m_selRectVertexCount = 0;
        return;
    }

    float t = 0.003f / m_zoomX;
    float tY = 0.003f / m_zoomY;
    float left = m_lastRectX0, right = m_lastRectX1;
    float bottom = m_lastRectY0, top = m_lastRectY1;

    std::vector<PointVertex> verts;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 0.8f;
    auto addBar = [&](float x0, float y0, float x1, float y1) {
        PointVertex tl = {x0, y1, 0, r, g, b, a, 1.0f, 1.0f};
        PointVertex tr = {x1, y1, 0, r, g, b, a, 1.0f, 1.0f};
        PointVertex bl = {x0, y0, 0, r, g, b, a, 1.0f, 1.0f};
        PointVertex br = {x1, y0, 0, r, g, b, a, 1.0f, 1.0f};
        verts.push_back(bl); verts.push_back(br); verts.push_back(tr);
        verts.push_back(bl); verts.push_back(tr); verts.push_back(tl);
    };

    addBar(left, bottom - tY, right, bottom + tY);
    addBar(left, top - tY, right, top + tY);
    addBar(left - t, bottom, left + t, top);
    addBar(right - t, bottom, right + t, top);

    m_selRectVertexCount = verts.size();

    auto device = m_ctx->GetDevice();
    auto queue = m_ctx->GetQueue();

    if (m_selRectBuffer) { wgpuBufferRelease(m_selRectBuffer); m_selRectBuffer = nullptr; }

    size_t dataSize = m_selRectVertexCount * sizeof(PointVertex);
    WGPUBufferDescriptor desc = {};
    desc.label = {"sel_rect", WGPU_STRLEN};
    desc.size = dataSize;
    desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_selRectBuffer = wgpuDeviceCreateBuffer(device, &desc);
    wgpuQueueWriteBuffer(queue, m_selRectBuffer, 0, verts.data(), dataSize);
}

void WebGPUCanvas::RecomputeDensityColors() {
    if (m_colorMap == 0 || m_basePositions.empty()) return;
    // Variable-based coloring is set by UpdatePlot and doesn't change with zoom
    if (m_colorVariable > 0) return;

    constexpr int GRID_SIZE = 128;
    size_t numPoints = m_basePositions.size() / 2;

    // Use the current viewport for density binning
    float hw = 1.0f / m_zoomX;
    float hh = 1.0f / m_zoomY;
    float viewMinX = m_panX - hw;
    float viewMaxX = m_panX + hw;
    float viewMinY = m_panY - hh;
    float viewMaxY = m_panY + hh;
    float viewW = viewMaxX - viewMinX;
    float viewH = viewMaxY - viewMinY;
    if (viewW <= 0 || viewH <= 0) return;

    float cellW = viewW / GRID_SIZE;
    float cellH = viewH / GRID_SIZE;

    // Bin visible points into the density grid
    std::vector<int> grid(GRID_SIZE * GRID_SIZE, 0);
    for (size_t i = 0; i < numPoints; i++) {
        float px = m_basePositions[i * 2];
        float py = m_basePositions[i * 2 + 1];
        if (px < viewMinX || px > viewMaxX || py < viewMinY || py > viewMaxY)
            continue;
        int gx = static_cast<int>((px - viewMinX) / cellW);
        int gy = static_cast<int>((py - viewMinY) / cellH);
        gx = std::max(0, std::min(gx, GRID_SIZE - 1));
        gy = std::max(0, std::min(gy, GRID_SIZE - 1));
        grid[gy * GRID_SIZE + gx]++;
    }

    int maxDensity = *std::max_element(grid.begin(), grid.end());
    if (maxDensity == 0) maxDensity = 1;

    // Recolor unselected points based on viewport-relative density
    ColorMapType cmap = static_cast<ColorMapType>(m_colorMap);
    for (size_t i = 0; i < numPoints && i < m_points.size(); i++) {
        int brushIdx = (i < m_selection.size()) ? m_selection[i] : 0;
        if (brushIdx > 0) continue;  // don't touch selected points

        float px = m_basePositions[i * 2];
        float py = m_basePositions[i * 2 + 1];

        float density = 0.0f;
        if (px >= viewMinX && px <= viewMaxX && py >= viewMinY && py <= viewMaxY) {
            int gx = static_cast<int>((px - viewMinX) / cellW);
            int gy = static_cast<int>((py - viewMinY) / cellH);
            gx = std::max(0, std::min(gx, GRID_SIZE - 1));
            gy = std::max(0, std::min(gy, GRID_SIZE - 1));
            density = std::log(1.0f + grid[gy * GRID_SIZE + gx]) /
                      std::log(1.0f + maxDensity);
        }

        ColorMapLookup(cmap, density, m_points[i].r, m_points[i].g, m_points[i].b);
        // Also update base colors so UpdatePointColors preserves them
        if (i * 3 + 2 < m_baseColors.size()) {
            m_baseColors[i * 3] = m_points[i].r;
            m_baseColors[i * 3 + 1] = m_points[i].g;
            m_baseColors[i * 3 + 2] = m_points[i].b;
        }
    }

    if (m_initialized) {
        UpdateVertexBuffer();
        Refresh();
    }
}

void WebGPUCanvas::UpdateUniforms() {
    float hw = 1.0f / m_zoomX;
    float hh = 1.0f / m_zoomY;
    makeOrtho(m_uniforms.projection, m_panX - hw, m_panX + hw, m_panY - hh, m_panY + hh);

    wxSize size = GetClientSize();
    double scale = GetContentScaleFactor();
    float zoomMean = std::sqrt(m_zoomX * m_zoomY);
    float zoomScale = 1.0f + 0.5f * std::log2(std::max(1.0f, zoomMean));
    m_uniforms.pointSize = m_pointSize * static_cast<float>(scale) * zoomScale;
    m_uniforms.viewportW = static_cast<float>(size.GetWidth() * scale);
    m_uniforms.viewportH = static_cast<float>(size.GetHeight() * scale);
    m_uniforms.rotationY = m_rotationY * 3.14159265f / 180.0f;  // degrees to radians

    wgpuQueueWriteBuffer(m_ctx->GetQueue(), m_uniformBuffer, 0, &m_uniforms, sizeof(Uniforms));
}

void WebGPUCanvas::ConfigureSurface(int width, int height) {
    if (!m_surface || !m_ctx || width <= 0 || height <= 0)
        return;

    m_surfaceConfig = {};
    m_surfaceConfig.device = m_ctx->GetDevice();
    m_surfaceConfig.usage = WGPUTextureUsage_RenderAttachment;
    m_surfaceConfig.format = m_surfaceFormat;
    m_surfaceConfig.presentMode = WGPUPresentMode_Fifo;
    m_surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
    m_surfaceConfig.width = static_cast<uint32_t>(width);
    m_surfaceConfig.height = static_cast<uint32_t>(height);

    wgpuSurfaceConfigure(m_surface, &m_surfaceConfig);

#ifdef __WXMAC__
    CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)m_metalLayer;
    metalLayer.drawableSize = CGSizeMake(width, height);
#endif
}

void WebGPUCanvas::Render() {
    if (!m_initialized || !m_pipeline || !m_ctx)
        return;

    // Ensure Metal layer frame matches current view bounds
#ifdef __WXMAC__
    {
        NSView* nsView = (NSView*)GetHandle();
        CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)m_metalLayer;
        if (metalLayer && nsView) {
            CGRect bounds = nsView.bounds;
            if (!CGRectEqualToRect(metalLayer.frame, bounds)) {
                metalLayer.frame = bounds;
                double scale = GetContentScaleFactor();
                ConfigureSurface(static_cast<int>(bounds.size.width * scale),
                                 static_cast<int>(bounds.size.height * scale));
            }
        }
    }
#endif

    UpdateUniforms();
    UpdateHistograms();
    UpdateGridLines();
    UpdateSelectionRect();

    // Notify MainFrame of current viewport for tick value labels
    if (onViewportChanged) {
        float hw = 1.0f / m_zoomX;
        float hh = 1.0f / m_zoomY;
        onViewportChanged(m_plotIndex, m_panX - hw, m_panX + hw, m_panY - hh, m_panY + hh);
    }

    WGPUSurfaceTexture surfTex;
    wgpuSurfaceGetCurrentTexture(m_surface, &surfTex);

    if (surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        if (surfTex.texture) wgpuTextureRelease(surfTex.texture);
        wxSize size = GetClientSize();
        double scale = GetContentScaleFactor();
        ConfigureSurface(static_cast<int>(size.GetWidth() * scale),
                         static_cast<int>(size.GetHeight() * scale));
        return;
    }

    auto device = m_ctx->GetDevice();
    WGPUTextureView view = wgpuTextureCreateView(surfTex.texture, nullptr);

    WGPUCommandEncoderDescriptor encDesc = {};
    encDesc.label = {"cmd_enc", WGPU_STRLEN};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

    // Background brightness (0 = black for additive blending, higher = lighter)
    double bgR = m_bgBrightness, bgG = m_bgBrightness, bgB = m_bgBrightness;

    WGPURenderPassColorAttachment colorAtt = {};
    colorAtt.view = view;
    colorAtt.loadOp = WGPULoadOp_Clear;
    colorAtt.storeOp = WGPUStoreOp_Store;
    colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAtt.clearValue = {bgR, bgG, bgB, 1.0};

    WGPURenderPassDescriptor rpDesc = {};
    rpDesc.label = {"render_pass", WGPU_STRLEN};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments = &colorAtt;

    WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(encoder, &rpDesc);

    auto quadBuf = m_ctx->GetQuadBuffer();
    if (m_vertexBuffer && quadBuf && !m_points.empty()) {
        // Use additive or alpha blending based on colormap mode
        wgpuRenderPassEncoderSetPipeline(rp, m_useAdditive ? m_pipeline : m_selPipeline);
        wgpuRenderPassEncoderSetBindGroup(rp, 0, m_bindGroup, 0, nullptr);
        // Bind selection + brush data (group 1)
        if (m_selectionBindGroup)
            wgpuRenderPassEncoderSetBindGroup(rp, 1, m_selectionBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(rp, 0, quadBuf, 0, 6 * 2 * sizeof(float));
        wgpuRenderPassEncoderSetVertexBuffer(rp, 1, m_vertexBuffer, 0,
                                              m_points.size() * sizeof(PointVertex));
        wgpuRenderPassEncoderDraw(rp, 6, static_cast<uint32_t>(m_points.size()), 0, 0);
    }

    // Draw selected points on top with alpha blending (not additive).
    // Uses a zero-filled selection buffer so shader takes the brush-0
    // vertex-color path, reading baked color/symbol/size from vertices.
    if (m_selPipeline && m_selVertexBuffer && m_selVertexCount > 0) {
        wgpuRenderPassEncoderSetPipeline(rp, m_selPipeline);
        wgpuRenderPassEncoderSetBindGroup(rp, 0, m_bindGroup, 0, nullptr);
        if (m_overlayBindGroup)
            wgpuRenderPassEncoderSetBindGroup(rp, 1, m_overlayBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(rp, 0, quadBuf, 0, 6 * 2 * sizeof(float));
        wgpuRenderPassEncoderSetVertexBuffer(rp, 1, m_selVertexBuffer, 0,
                                              m_selVertexCount * sizeof(PointVertex));
        wgpuRenderPassEncoderDraw(rp, 6, static_cast<uint32_t>(m_selVertexCount), 0, 0);
    }

    // Draw selection rectangle outline (world space, main projection)
    if ((m_selecting || m_showLastRect) && m_histPipeline && m_selRectBuffer && m_selRectVertexCount > 0) {
        wgpuRenderPassEncoderSetPipeline(rp, m_histPipeline);
        wgpuRenderPassEncoderSetBindGroup(rp, 0, m_bindGroup, 0, nullptr);  // main projection
        wgpuRenderPassEncoderSetVertexBuffer(rp, 0, m_selRectBuffer, 0,
                                              m_selRectVertexCount * sizeof(PointVertex));
        wgpuRenderPassEncoderDraw(rp, static_cast<uint32_t>(m_selRectVertexCount), 1, 0, 0);
    }

    // Draw grid lines on top of data (using identity projection)
    if (m_showGridLines && m_histPipeline && m_gridLineBuffer && m_gridLineVertexCount > 0) {
        wgpuRenderPassEncoderSetPipeline(rp, m_histPipeline);
        wgpuRenderPassEncoderSetBindGroup(rp, 0, m_histBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(rp, 0, m_gridLineBuffer, 0,
                                              m_gridLineVertexCount * sizeof(PointVertex));
        wgpuRenderPassEncoderDraw(rp, static_cast<uint32_t>(m_gridLineVertexCount), 1, 0, 0);
    }

    // Draw histograms with identity projection (fixed to viewport edges)
    if (m_histPipeline && m_histBuffer && m_histVertexCount > 0 && m_showHistograms) {
        wgpuRenderPassEncoderSetPipeline(rp, m_histPipeline);
        wgpuRenderPassEncoderSetBindGroup(rp, 0, m_histBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(rp, 0, m_histBuffer, 0,
                                              m_histVertexCount * sizeof(PointVertex));
        wgpuRenderPassEncoderDraw(rp, static_cast<uint32_t>(m_histVertexCount), 1, 0, 0);
    }

    wgpuRenderPassEncoderEnd(rp);
    wgpuRenderPassEncoderRelease(rp);

    WGPUCommandBufferDescriptor cbDesc = {};
    cbDesc.label = {"cmd_buf", WGPU_STRLEN};
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cbDesc);

    wgpuQueueSubmit(m_ctx->GetQueue(), 1, &cmdBuf);
    wgpuSurfacePresent(m_surface);

    wgpuCommandBufferRelease(cmdBuf);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfTex.texture);
}

void WebGPUCanvas::OnPaint(wxPaintEvent& event) {
    wxPaintDC dc(this);
    Render();
}

void WebGPUCanvas::OnSize(wxSizeEvent& event) {
    wxSize size = event.GetSize();
    if (m_initialized && size.GetWidth() > 0 && size.GetHeight() > 0) {
        double scale = GetContentScaleFactor();
        ConfigureSurface(static_cast<int>(size.GetWidth() * scale),
                         static_cast<int>(size.GetHeight() * scale));
#ifdef __WXMAC__
        NSView* nsView = (NSView*)GetHandle();
        CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)m_metalLayer;
        metalLayer.frame = nsView.bounds;
#endif
        Refresh();
    }
    event.Skip();
}

void WebGPUCanvas::OnMouse(wxMouseEvent& event) {
    bool isPan = event.ShiftDown() || event.MiddleIsDown() || event.RightIsDown();
    bool isTranslate = event.AltDown() && m_hasLastRect;

    if (event.LeftDown() || event.MiddleDown() || event.RightDown()) {
        SetFocus();
        // Hide tooltip on any mouse button press
        if (m_showTooltip && onPointHover)
            onPointHover(m_plotIndex, -1, 0, 0);
        if (isPan || event.MiddleDown() || event.RightDown()) {
            m_panning = true;
        } else if (isTranslate) {
            m_translating = true;
        } else {
            m_selecting = true;
            m_showLastRect = false;
            m_selectStart = event.GetPosition();
            m_selectEnd = m_selectStart;
        }
        m_lastMouse = event.GetPosition();
        if (!HasCapture()) CaptureMouse();
    } else if (event.LeftUp() || event.MiddleUp() || event.RightUp()) {
        if (m_selecting) {
            float wx0, wy0, wx1, wy1;
            ScreenToWorld(m_selectStart.x, m_selectStart.y, wx0, wy0);
            ScreenToWorld(m_selectEnd.x, m_selectEnd.y, wx1, wy1);

            if (std::abs(m_selectEnd.x - m_selectStart.x) > 3 ||
                std::abs(m_selectEnd.y - m_selectStart.y) > 3) {
                bool extend = event.CmdDown() || event.ControlDown();
                if (onBrushRect)
                    onBrushRect(m_plotIndex, wx0, wy0, wx1, wy1, extend);
                // Save the rect for later translation
                m_lastRectX0 = std::min(wx0, wx1);
                m_lastRectY0 = std::min(wy0, wy1);
                m_lastRectX1 = std::max(wx0, wx1);
                m_lastRectY1 = std::max(wy0, wy1);
                m_hasLastRect = true;
                m_showLastRect = true;
                BuildSelRectFromWorld();
            }
            m_selecting = false;
        }
        if (m_translating && m_hasLastRect) {
            if (m_deferRedraws) {
                // Deferred translate: apply the final rect position on mouse-up
                bool extend = event.CmdDown() || event.ControlDown();
                if (onBrushRect)
                    onBrushRect(m_plotIndex, m_lastRectX0, m_lastRectY0, m_lastRectX1, m_lastRectY1, extend);
            }
            m_showLastRect = true;
            BuildSelRectFromWorld();
        }
        m_translating = false;
        m_panning = false;
        if (HasCapture()) ReleaseMouse();
        HideSelectionOverlay();
        if (m_colorMap != 0) RecomputeDensityColors();
        Refresh();
    } else if (event.Dragging()) {
        wxPoint pos = event.GetPosition();
        if (m_panning) {
            wxSize size = GetClientSize();
            float dx = static_cast<float>(pos.x - m_lastMouse.x) / size.GetWidth();
            float dy = static_cast<float>(pos.y - m_lastMouse.y) / size.GetHeight();
            m_panX -= dx * 2.0f / m_zoomX;
            m_panY += dy * 2.0f / m_zoomY;
            m_lastMouse = pos;
            if (onViewChanged) onViewChanged(m_plotIndex, m_panX, m_panY, m_zoomX, m_zoomY);
            Refresh();
        } else if (m_translating && m_hasLastRect) {
            // Option+drag: translate the existing selection rect
            wxSize size = GetClientSize();
            float hw = 1.0f / m_zoomX;
            float hh = 1.0f / m_zoomY;
            float dx = static_cast<float>(pos.x - m_lastMouse.x) / size.GetWidth() * 2.0f * hw;
            float dy = -static_cast<float>(pos.y - m_lastMouse.y) / size.GetHeight() * 2.0f * hh;
            m_lastRectX0 += dx;
            m_lastRectX1 += dx;
            m_lastRectY0 += dy;
            m_lastRectY1 += dy;
            m_lastMouse = pos;
            if (!m_deferRedraws) {
                bool extend = event.CmdDown() || event.ControlDown();
                if (onBrushRect)
                    onBrushRect(m_plotIndex, m_lastRectX0, m_lastRectY0, m_lastRectX1, m_lastRectY1, extend);
            }
            if (m_deferRedraws) {
                // Show lightweight native overlay rectangle
                int sx0, sy0, sx1, sy1;
                WorldToScreen(m_lastRectX0, m_lastRectY1, sx0, sy0);  // top-left
                WorldToScreen(m_lastRectX1, m_lastRectY0, sx1, sy1);  // bottom-right
                ShowSelectionOverlay(sx0, sy0, sx1, sy1);
            } else {
                Refresh();
            }
        } else if (m_selecting) {
            m_selectEnd = pos;
            float wx0, wy0, wx1, wy1;
            ScreenToWorld(m_selectStart.x, m_selectStart.y, wx0, wy0);
            ScreenToWorld(m_selectEnd.x, m_selectEnd.y, wx1, wy1);
            if (std::abs(m_selectEnd.x - m_selectStart.x) > 3 ||
                std::abs(m_selectEnd.y - m_selectStart.y) > 3) {
                if (!m_deferRedraws) {
                    bool extend = event.CmdDown() || event.ControlDown();
                    if (onBrushRect)
                        onBrushRect(m_plotIndex, wx0, wy0, wx1, wy1, extend);
                }
                // Update saved rect during drag too
                m_lastRectX0 = std::min(wx0, wx1);
                m_lastRectY0 = std::min(wy0, wy1);
                m_lastRectX1 = std::max(wx0, wx1);
                m_lastRectY1 = std::max(wy0, wy1);
                m_hasLastRect = true;
            }
            if (m_deferRedraws) {
                ShowSelectionOverlay(
                    std::min(m_selectStart.x, m_selectEnd.x),
                    std::min(m_selectStart.y, m_selectEnd.y),
                    std::max(m_selectStart.x, m_selectEnd.x),
                    std::max(m_selectStart.y, m_selectEnd.y));
            } else {
                Refresh();
            }
        }
    } else if (event.GetWheelRotation() != 0) {
        // Hide tooltip during scroll
        if (m_showTooltip && onPointHover)
            onPointHover(m_plotIndex, -1, 0, 0);
        // Two-finger scroll: pan the view
        wxSize sz = GetClientSize();
        float dx = 0.0f, dy = 0.0f;
        if (event.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL) {
            dy = (event.GetWheelRotation() / 120.0f) * 0.3f / m_zoomY;
        } else {
            dx = (event.GetWheelRotation() / 120.0f) * 0.3f / m_zoomX;
        }
        m_panX += dx;
        m_panY += dy;

        if (onViewChanged) onViewChanged(m_plotIndex, m_panX, m_panY, m_zoomX, m_zoomY);
        if (m_colorMap != 0) RecomputeDensityColors();
        Refresh();
    } else if (m_showTooltip && event.Moving()) {
        // Tooltip hover detection: find nearest point under cursor
        int idx = FindNearestPoint(event.GetX(), event.GetY());
        if (idx >= 0 && onPointHover) {
            size_t dataRow = GetOriginalDataRow(idx);
            onPointHover(m_plotIndex, static_cast<int>(dataRow), event.GetX(), event.GetY());
        } else if (onPointHover) {
            onPointHover(m_plotIndex, -1, 0, 0);
        }
    }
    event.Skip();
}

void WebGPUCanvas::OnMagnify(wxMouseEvent& event) {
    float magnification = event.GetMagnification();  // typically -0.1 to +0.1 per event
    if (magnification == 0.0f) return;
    if (m_showTooltip && onPointHover)
        onPointHover(m_plotIndex, -1, 0, 0);

    float factor = 1.0f + magnification;
    factor = std::max(0.5f, std::min(factor, 2.0f));

    // Get world position under the mouse cursor (zoom center)
    wxPoint mousePos = event.GetPosition();
    float wx, wy;
    ScreenToWorld(mousePos.x, mousePos.y, wx, wy);

    // Apply zoom
    float newZoomX = m_zoomX * factor;
    float newZoomY = m_zoomY * factor;
    newZoomX = std::max(0.1f, std::min(newZoomX, 100.0f));
    newZoomY = std::max(0.1f, std::min(newZoomY, 100.0f));

    // Adjust pan so the world point under the cursor stays fixed
    // Before zoom: wx = panX + ndcX / zoomX
    // After zoom:  wx = panX' + ndcX / zoomX'
    // So: panX' = wx - ndcX / zoomX' = wx - (wx - panX) * (zoomX / zoomX')
    wxSize size = GetClientSize();
    float ndcX = (static_cast<float>(mousePos.x) / size.GetWidth()) * 2.0f - 1.0f;
    float ndcY = 1.0f - (static_cast<float>(mousePos.y) / size.GetHeight()) * 2.0f;

    m_panX = wx - ndcX / newZoomX;
    m_panY = wy - ndcY / newZoomY;
    m_zoomX = newZoomX;
    m_zoomY = newZoomY;

    if (onViewChanged) onViewChanged(m_plotIndex, m_panX, m_panY, m_zoomX, m_zoomY);
    if (m_colorMap != 0) RecomputeDensityColors();
    Refresh();
}

void WebGPUCanvas::OnKeyDown(wxKeyEvent& event) {
    int key = event.GetKeyCode();
    // Normalize to uppercase
    if (key >= 'a' && key <= 'z') key -= 32;

    switch (key) {
        case 'C':
            if (onClearRequested) onClearRequested();
            break;
        case 'I':
            if (onInvertRequested) onInvertRequested();
            break;
        case 'R':
            if (event.ShiftDown()) {
                if (onResetAllViewsRequested) onResetAllViewsRequested();
            } else {
                if (onResetViewRequested) onResetViewRequested();
                else ResetView();
            }
            break;
        case 'D':
            if (onToggleUnselected) onToggleUnselected();
            break;
        case 'K':
            if (onKillRequested) onKillRequested();
            break;
        case 'T':
            m_showTooltip = !m_showTooltip;
            if (!m_showTooltip && onPointHover)
                onPointHover(m_plotIndex, -1, 0, 0);
            if (onTooltipToggled)
                onTooltipToggled(m_plotIndex, m_showTooltip);
            break;
        case 'Q':
            if (auto* frame = wxDynamicCast(wxGetTopLevelParent(this), wxFrame))
                frame->Close(true);
            break;
        case WXK_LEFT:
        case WXK_RIGHT:
        case WXK_UP:
        case WXK_DOWN:
            if (m_hasLastRect) {
                float w = m_lastRectX1 - m_lastRectX0;
                float h = m_lastRectY1 - m_lastRectY0;
                float dx = 0, dy = 0;
                if (key == WXK_LEFT)  dx = -w;
                if (key == WXK_RIGHT) dx =  w;
                if (key == WXK_UP)    dy =  h;
                if (key == WXK_DOWN)  dy = -h;
                m_lastRectX0 += dx;
                m_lastRectX1 += dx;
                m_lastRectY0 += dy;
                m_lastRectY1 += dy;

                // Update selection
                if (onBrushRect)
                    onBrushRect(m_plotIndex, m_lastRectX0, m_lastRectY0,
                                m_lastRectX1, m_lastRectY1, false);

                // Show the rect and refresh
                m_showLastRect = true;
                BuildSelRectFromWorld();
                Refresh();
            }
            break;
        default:
            event.Skip();
            break;
    }
}

void WebGPUCanvas::ShowSelectionOverlay(int x0, int y0, int x1, int y1) {
#ifdef __WXMAC__
    if (!m_metalLayer) return;
    CAMetalLayer* metalLayer = (CAMetalLayer*)m_metalLayer;

    // Disable implicit animations — without this, Core Animation
    // animates path changes over 0.25s, making the rect feel laggy.
    [CATransaction begin];
    [CATransaction setDisableActions:YES];

    if (!m_selectionOverlay) {
        CAShapeLayer* overlay = [CAShapeLayer layer];
        [overlay retain];
        overlay.fillColor = [NSColor clearColor].CGColor;
        overlay.strokeColor = [NSColor whiteColor].CGColor;
        overlay.lineWidth = 1.5;
        overlay.zPosition = 1000;
        // Add as sublayer of the metal layer itself so coordinates
        // are relative to this canvas, not a parent view.
        [metalLayer addSublayer:overlay];
        m_selectionOverlay = (void*)overlay;
    }

    CAShapeLayer* overlay = (CAShapeLayer*)m_selectionOverlay;

    // wxWidgets NSView is flipped (origin at top-left), so the layer
    // coordinate system matches wx screen coords directly.
    CGFloat left   = std::min(x0, x1);
    CGFloat right  = std::max(x0, x1);
    CGFloat top    = std::min(y0, y1);
    CGFloat bottom = std::max(y0, y1);
    CGRect rect = CGRectMake(left, top, right - left, bottom - top);

    CGPathRef path = CGPathCreateWithRect(rect, NULL);
    overlay.path = path;
    CGPathRelease(path);
    overlay.hidden = NO;

    [CATransaction commit];
#endif
}

void WebGPUCanvas::HideSelectionOverlay() {
#ifdef __WXMAC__
    if (m_selectionOverlay) {
        CAShapeLayer* overlay = (CAShapeLayer*)m_selectionOverlay;
        [overlay removeFromSuperlayer];
        [overlay release];
        m_selectionOverlay = nullptr;
    }
#endif
}

void WebGPUCanvas::Cleanup() {
    HideSelectionOverlay();
    if (m_selectionBindGroup) { wgpuBindGroupRelease(m_selectionBindGroup); m_selectionBindGroup = nullptr; }
    if (m_selectionGpuBuffer) { wgpuBufferRelease(m_selectionGpuBuffer); m_selectionGpuBuffer = nullptr; }
    if (m_brushColorGpuBuffer) { wgpuBufferRelease(m_brushColorGpuBuffer); m_brushColorGpuBuffer = nullptr; }
    if (m_brushParamsGpuBuffer) { wgpuBufferRelease(m_brushParamsGpuBuffer); m_brushParamsGpuBuffer = nullptr; }
    if (m_gridLineBuffer) { wgpuBufferRelease(m_gridLineBuffer); m_gridLineBuffer = nullptr; }
    if (m_histBuffer) { wgpuBufferRelease(m_histBuffer); m_histBuffer = nullptr; }
    if (m_histPipeline) { wgpuRenderPipelineRelease(m_histPipeline); m_histPipeline = nullptr; }
    if (m_histBindGroup) { wgpuBindGroupRelease(m_histBindGroup); m_histBindGroup = nullptr; }
    if (m_histUniformBuffer) { wgpuBufferRelease(m_histUniformBuffer); m_histUniformBuffer = nullptr; }
    if (m_selRectBuffer) { wgpuBufferRelease(m_selRectBuffer); m_selRectBuffer = nullptr; }
    if (m_selVertexBuffer) { wgpuBufferRelease(m_selVertexBuffer); m_selVertexBuffer = nullptr; }
    if (m_overlaySelBuffer) { wgpuBufferRelease(m_overlaySelBuffer); m_overlaySelBuffer = nullptr; }
    if (m_overlayBindGroup) { wgpuBindGroupRelease(m_overlayBindGroup); m_overlayBindGroup = nullptr; }
    if (m_selPipeline) { wgpuRenderPipelineRelease(m_selPipeline); m_selPipeline = nullptr; }
    if (m_bindGroup) { wgpuBindGroupRelease(m_bindGroup); m_bindGroup = nullptr; }
    if (m_pipeline) { wgpuRenderPipelineRelease(m_pipeline); m_pipeline = nullptr; }
    if (m_uniformBuffer) { wgpuBufferRelease(m_uniformBuffer); m_uniformBuffer = nullptr; }
    if (m_vertexBuffer) { wgpuBufferRelease(m_vertexBuffer); m_vertexBuffer = nullptr; }
    if (m_surface) { wgpuSurfaceRelease(m_surface); m_surface = nullptr; }
    m_initialized = false;
}
