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

void WebGPUCanvas::SetOpacity(float alpha) {
    m_opacity = alpha;
    UpdatePointColors();
}

void WebGPUCanvas::SetBrushColors(const std::vector<BrushColor>& colors) {
    m_brushColors = colors;
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
    // All points get the unselected color for the additive pass
    for (size_t i = 0; i < m_points.size(); i++) {
        m_points[i].r = 0.15f;
        m_points[i].g = 0.4f;
        m_points[i].b = 1.0f;
        m_points[i].a = m_opacity;
    }

    // Build a separate buffer of only selected points for the overlay pass
    std::vector<PointVertex> selPoints;
    for (size_t i = 0; i < m_points.size(); i++) {
        int brushIdx = (i < m_selection.size()) ? m_selection[i] : 0;
        if (brushIdx > 0 && brushIdx <= (int)m_brushColors.size()) {
            const auto& bc = m_brushColors[brushIdx - 1];
            PointVertex v = m_points[i];  // same position
            v.r = bc.r;
            v.g = bc.g;
            v.b = bc.b;
            v.a = 0.85f;  // high opacity for clear visibility
            selPoints.push_back(v);
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
        if (!selPoints.empty()) {
            size_t dataSize = selPoints.size() * sizeof(PointVertex);
            WGPUBufferDescriptor sbDesc = {};
            sbDesc.label = {"sel_instances", WGPU_STRLEN};
            sbDesc.size = dataSize;
            sbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            m_selVertexBuffer = wgpuDeviceCreateBuffer(device, &sbDesc);
            wgpuQueueWriteBuffer(queue, m_selVertexBuffer, 0, selPoints.data(), dataSize);
        }

        UpdateHistograms();
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
    histRpDesc.layout = pipelineLayout;
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

    float hw = 1.0f / m_zoom;
    float hh = 1.0f / m_zoom;
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
        PointVertex tl = {x0, y1, r, g, b, a};
        PointVertex tr = {x1, y1, r, g, b, a};
        PointVertex bl = {x0, y0, r, g, b, a};
        PointVertex br = {x1, y0, r, g, b, a};
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
    UpdateHistograms();

    // Notify MainFrame of current viewport for tick value labels
    if (onViewportChanged) {
        float hw = 1.0f / m_zoom;
        float hh = 1.0f / m_zoom;
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

    // Black background for additive blending (points accumulate brightness)
    double bgR = m_isActive ? 0.03 : 0.0;
    double bgG = m_isActive ? 0.03 : 0.0;
    double bgB = m_isActive ? 0.05 : 0.0;

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

    // Draw selected points on top with alpha blending (not additive)
    // so they stand out clearly against dense overplotted regions
    if (m_selPipeline && m_selVertexBuffer && m_selVertexCount > 0) {
        wgpuRenderPassEncoderSetPipeline(rp, m_selPipeline);
        wgpuRenderPassEncoderSetBindGroup(rp, 0, m_bindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(rp, 0, quadBuf, 0, 6 * 2 * sizeof(float));
        wgpuRenderPassEncoderSetVertexBuffer(rp, 1, m_selVertexBuffer, 0,
                                              m_selVertexCount * sizeof(PointVertex));
        wgpuRenderPassEncoderDraw(rp, 6, static_cast<uint32_t>(m_selVertexCount), 0, 0);
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
    if (m_histBuffer) { wgpuBufferRelease(m_histBuffer); m_histBuffer = nullptr; }
    if (m_histPipeline) { wgpuRenderPipelineRelease(m_histPipeline); m_histPipeline = nullptr; }
    if (m_histBindGroup) { wgpuBindGroupRelease(m_histBindGroup); m_histBindGroup = nullptr; }
    if (m_histUniformBuffer) { wgpuBufferRelease(m_histUniformBuffer); m_histUniformBuffer = nullptr; }
    if (m_selVertexBuffer) { wgpuBufferRelease(m_selVertexBuffer); m_selVertexBuffer = nullptr; }
    if (m_selPipeline) { wgpuRenderPipelineRelease(m_selPipeline); m_selPipeline = nullptr; }
    if (m_bindGroup) { wgpuBindGroupRelease(m_bindGroup); m_bindGroup = nullptr; }
    if (m_pipeline) { wgpuRenderPipelineRelease(m_pipeline); m_pipeline = nullptr; }
    if (m_uniformBuffer) { wgpuBufferRelease(m_uniformBuffer); m_uniformBuffer = nullptr; }
    if (m_vertexBuffer) { wgpuBufferRelease(m_vertexBuffer); m_vertexBuffer = nullptr; }
    if (m_surface) { wgpuSurfaceRelease(m_surface); m_surface = nullptr; }
    m_initialized = false;
}
