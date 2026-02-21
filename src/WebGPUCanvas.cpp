#include "WebGPUCanvas.h"
#include "WebGPUContext.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>

#ifdef __WXMAC__
#include <Cocoa/Cocoa.h>
#include <QuartzCore/CAMetalLayer.h>
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
    Bind(wxEVT_KEY_DOWN, &WebGPUCanvas::OnKeyDown, this);
    Bind(wxEVT_CHAR, &WebGPUCanvas::OnKeyDown, this);

    CallAfter([this]() { InitSurface(); });
}

WebGPUCanvas::~WebGPUCanvas() {
    Cleanup();
}

void WebGPUCanvas::SetPoints(std::vector<PointVertex> points) {
    m_basePositions.resize(points.size() * 2);
    for (size_t i = 0; i < points.size(); i++) {
        m_basePositions[i * 2] = points[i].x;
        m_basePositions[i * 2 + 1] = points[i].y;
    }
    m_selection.assign(points.size(), 0);
    m_points = std::move(points);
    if (m_initialized) {
        UpdateVertexBuffer();
        Refresh();
    }
}

void WebGPUCanvas::SetPointSize(float size) {
    m_pointSize = size;
    Refresh();
}

void WebGPUCanvas::SetOpacity(float alpha) {
    m_opacity = alpha;
    UpdatePointColors();
}

void WebGPUCanvas::SetBrushColor(const BrushColor& color) {
    m_brushColor = color;
    UpdatePointColors();
}

void WebGPUCanvas::SetSelection(const std::vector<int>& sel) {
    if (sel.size() == m_selection.size()) {
        m_selection = sel;
        UpdatePointColors();
    }
}

void WebGPUCanvas::ClearSelection() {
    std::fill(m_selection.begin(), m_selection.end(), 0);
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
    m_zoom = 1.0f;
    if (m_initialized) {
        Refresh();
        Update();  // force immediate repaint
    }
}

void WebGPUCanvas::UpdatePointColors() {
    for (size_t i = 0; i < m_points.size(); i++) {
        if (i < m_selection.size() && m_selection[i] > 0) {
            m_points[i].r = m_brushColor.r;
            m_points[i].g = m_brushColor.g;
            m_points[i].b = m_brushColor.b;
            m_points[i].a = m_opacity * 3.0f;
        } else {
            m_points[i].r = 0.4f;
            m_points[i].g = 0.7f;
            m_points[i].b = 1.0f;
            m_points[i].a = m_opacity;
        }
    }
    if (m_initialized) {
        UpdateVertexBuffer();
        Refresh();
    }
}

void WebGPUCanvas::ScreenToWorld(int sx, int sy, float& wx, float& wy) {
    wxSize size = GetClientSize();
    float ndcX = (static_cast<float>(sx) / size.GetWidth()) * 2.0f - 1.0f;
    float ndcY = 1.0f - (static_cast<float>(sy) / size.GetHeight()) * 2.0f;
    float hw = 1.0f / m_zoom;
    float hh = 1.0f / m_zoom;
    wx = m_panX + ndcX * hw;
    wy = m_panY + ndcY * hh;
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

    // Create bind group
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

    if (!m_points.empty())
        UpdateVertexBuffer();

    wxSize size = GetClientSize();
    double scale = GetContentScaleFactor();
    ConfigureSurface(static_cast<int>(size.GetWidth() * scale),
                     static_cast<int>(size.GetHeight() * scale));

    m_initialized = true;
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

    WGPUVertexAttribute instanceAttrs[2] = {};
    instanceAttrs[0].format = WGPUVertexFormat_Float32x2;
    instanceAttrs[0].offset = offsetof(PointVertex, x);
    instanceAttrs[0].shaderLocation = 1;
    instanceAttrs[1].format = WGPUVertexFormat_Float32x4;
    instanceAttrs[1].offset = offsetof(PointVertex, r);
    instanceAttrs[1].shaderLocation = 2;

    WGPUVertexBufferLayout vbLayouts[2] = {};
    vbLayouts[0].arrayStride = 2 * sizeof(float);
    vbLayouts[0].stepMode = WGPUVertexStepMode_Vertex;
    vbLayouts[0].attributeCount = 1;
    vbLayouts[0].attributes = &quadAttr;
    vbLayouts[1].arrayStride = sizeof(PointVertex);
    vbLayouts[1].stepMode = WGPUVertexStepMode_Instance;
    vbLayouts[1].attributeCount = 2;
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

void WebGPUCanvas::UpdateUniforms() {
    float hw = 1.0f / m_zoom;
    float hh = 1.0f / m_zoom;
    makeOrtho(m_uniforms.projection, m_panX - hw, m_panX + hw, m_panY - hh, m_panY + hh);

    wxSize size = GetClientSize();
    double scale = GetContentScaleFactor();
    m_uniforms.pointSize = m_pointSize * static_cast<float>(scale);
    m_uniforms.viewportW = static_cast<float>(size.GetWidth() * scale);
    m_uniforms.viewportH = static_cast<float>(size.GetHeight() * scale);

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

    UpdateUniforms();

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

    double bgR = m_isActive ? 0.12 : 0.08;
    double bgG = m_isActive ? 0.12 : 0.08;
    double bgB = m_isActive ? 0.18 : 0.12;

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
        wgpuRenderPassEncoderSetPipeline(rp, m_pipeline);
        wgpuRenderPassEncoderSetBindGroup(rp, 0, m_bindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(rp, 0, quadBuf, 0, 6 * 2 * sizeof(float));
        wgpuRenderPassEncoderSetVertexBuffer(rp, 1, m_vertexBuffer, 0,
                                              m_points.size() * sizeof(PointVertex));
        wgpuRenderPassEncoderDraw(rp, 6, static_cast<uint32_t>(m_points.size()), 0, 0);
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

    if (m_selecting) {
        dc.SetPen(wxPen(*wxWHITE, 1, wxPENSTYLE_DOT));
        dc.SetBrush(wxBrush(wxColour(255, 255, 255, 30)));
        int x = std::min(m_selectStart.x, m_selectEnd.x);
        int y = std::min(m_selectStart.y, m_selectEnd.y);
        int w = std::abs(m_selectEnd.x - m_selectStart.x);
        int h = std::abs(m_selectEnd.y - m_selectStart.y);
        dc.DrawRectangle(x, y, w, h);
    }

    if (m_isActive) {
        dc.SetPen(wxPen(wxColour(100, 150, 255), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        wxSize sz = GetClientSize();
        dc.DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());
    }
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

    if (event.LeftDown() || event.MiddleDown() || event.RightDown()) {
        SetFocus();
        if (isPan || event.MiddleDown() || event.RightDown()) {
            m_panning = true;
        } else {
            m_selecting = true;
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
            }
            m_selecting = false;
        }
        m_panning = false;
        if (HasCapture()) ReleaseMouse();
        Refresh();
    } else if (event.Dragging()) {
        wxPoint pos = event.GetPosition();
        if (m_panning) {
            wxSize size = GetClientSize();
            float dx = static_cast<float>(pos.x - m_lastMouse.x) / size.GetWidth();
            float dy = static_cast<float>(pos.y - m_lastMouse.y) / size.GetHeight();
            m_panX -= dx * 2.0f / m_zoom;
            m_panY += dy * 2.0f / m_zoom;
            m_lastMouse = pos;
            Refresh();
        } else if (m_selecting) {
            m_selectEnd = pos;
            Refresh();
        }
    } else if (event.GetWheelRotation() != 0) {
        float factor = event.GetWheelRotation() > 0 ? 1.1f : 1.0f / 1.1f;
        m_zoom *= factor;
        m_zoom = std::max(0.1f, std::min(m_zoom, 100.0f));
        Refresh();
    }
    event.Skip();
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
            if (onResetViewRequested) onResetViewRequested();
            else ResetView();
            break;
        default:
            event.Skip();
            break;
    }
}

void WebGPUCanvas::Cleanup() {
    if (m_bindGroup) { wgpuBindGroupRelease(m_bindGroup); m_bindGroup = nullptr; }
    if (m_pipeline) { wgpuRenderPipelineRelease(m_pipeline); m_pipeline = nullptr; }
    if (m_uniformBuffer) { wgpuBufferRelease(m_uniformBuffer); m_uniformBuffer = nullptr; }
    if (m_vertexBuffer) { wgpuBufferRelease(m_vertexBuffer); m_vertexBuffer = nullptr; }
    if (m_surface) { wgpuSurfaceRelease(m_surface); m_surface = nullptr; }
    m_initialized = false;
}
