// Viewpoints (MIT License) - See LICENSE file
#include "WebGPUContext.h"
#include "WebGPUCanvas.h" // for Uniforms struct
#include <cstdio>
#include <cstddef>

// Point rendering shader using instanced quads
static const char* kPointShaderSource = R"(
struct Uniforms {
    projection: mat4x4f,
    point_size: f32,
    viewport_w: f32,
    viewport_h: f32,
    _pad: f32,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

// Selection data (bind group 1)
@group(1) @binding(0) var<storage, read> selection: array<u32>;
@group(1) @binding(1) var<uniform> brush_colors: array<vec4f, 8>;
@group(1) @binding(2) var<uniform> brush_params: array<vec4f, 8>; // x=symbol, y=sizeScale, z=showUnsel, w=0

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
    @location(2) symbol: f32,
}

@vertex
fn vs_main(
    @builtin(instance_index) instance_id: u32,
    @location(0) quad_pos: vec2f,
    @location(1) point_pos: vec2f,
    @location(2) point_color: vec4f,
    @location(3) point_symbol: f32,
    @location(4) point_size_scale: f32,
) -> VertexOutput {
    // Look up selection state from GPU buffer
    let sel = selection[instance_id];

    // All points use brush uniforms: brush 0 for unselected, brush 1-7 for selected
    var brush_idx = sel;
    if (brush_idx > 7u) { brush_idx = 0u; }

    // Color logic:
    // brush_params[i].z = useVertexColor flag (1.0 = use vertex/colormap color)
    var color: vec4f;
    let use_vertex = brush_params[brush_idx].z > 0.5;

    if (brush_idx == 0u) {
        // Unselected points
        if (use_vertex) {
            color = point_color;  // colormap/default
        } else {
            color = vec4f(brush_colors[0].rgb, point_color.a);  // custom brush 0
        }
    } else {
        // Selected points (brushes 1-7)
        // Modulate brush color by vertex colormap brightness
        // so density/variable structure shows through the selection color
        let vertex_lum = dot(point_color.rgb, vec3f(0.299, 0.587, 0.114));
        let base_lum = max(vertex_lum, 0.15);  // floor to avoid fully black
        color = vec4f(brush_colors[brush_idx].rgb * base_lum * 3.0,
                      brush_colors[brush_idx].a);
    }
    var sym = brush_params[brush_idx].x;
    var size_scale = brush_params[brush_idx].y;

    let clip = uniforms.projection * vec4f(point_pos, 0.0, 1.0);
    let effective_size = uniforms.point_size * size_scale;
    let pixel_offset = quad_pos * effective_size;
    let ndc_offset = vec2f(
        pixel_offset.x * 2.0 / uniforms.viewport_w,
        pixel_offset.y * 2.0 / uniforms.viewport_h,
    );

    var output: VertexOutput;
    output.position = vec4f(clip.xy + ndc_offset * clip.w, clip.z, clip.w);
    output.color = color;
    output.uv = quad_pos + 0.5;
    output.symbol = sym;
    return output;
}

// SDF shape functions — all work in centered UV space where (0,0) is center, range [-0.5, 0.5]
fn sdf_circle(p: vec2f) -> f32 {
    return length(p) * 2.0;
}

fn sdf_square(p: vec2f) -> f32 {
    let d = abs(p);
    return max(d.x, d.y) * 2.0;
}

fn sdf_diamond(p: vec2f) -> f32 {
    let d = abs(p);
    return (d.x + d.y) * 1.42;
}

fn sdf_triangle_up(p: vec2f) -> f32 {
    let q = vec2f(abs(p.x), p.y + 0.15);
    return max(q.x * 1.73 + q.y, -q.y * 2.0 + 0.5) * 1.3;
}

fn sdf_triangle_down(p: vec2f) -> f32 {
    let q = vec2f(abs(p.x), -p.y + 0.15);
    return max(q.x * 1.73 + q.y, -q.y * 2.0 + 0.5) * 1.3;
}

fn sdf_cross(p: vec2f) -> f32 {
    let d = abs(p);
    let arm = 0.14;
    if (d.x < arm || d.y < arm) { return max(d.x, d.y) * 2.0; }
    return 2.0;  // outside
}

fn sdf_plus(p: vec2f) -> f32 {
    let d = abs(p);
    let arm = 0.1;
    if (d.x < arm || d.y < arm) { return max(d.x, d.y) * 2.0; }
    return 2.0;
}

fn sdf_star(p: vec2f) -> f32 {
    let d = abs(p);
    let diag = (d.x + d.y) * 0.707;
    let arm = 0.1;
    if (d.x < arm || d.y < arm || abs(d.x - d.y) < arm * 1.4) {
        return max(d.x, d.y) * 2.0;
    }
    return 2.0;
}

fn sdf_ring(p: vec2f) -> f32 {
    let dist = length(p) * 2.0;
    if (abs(dist - 0.7) < 0.2) { return dist; }
    return 2.0;
}

fn sdf_square_outline(p: vec2f) -> f32 {
    let d = abs(p);
    let outer = max(d.x, d.y) * 2.0;
    let inner = max(d.x, d.y) * 2.0;
    if (outer < 1.0 && inner > 0.65) { return outer; }
    if (outer >= 1.0) { return 2.0; }
    return 2.0;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let p = input.uv - vec2f(0.5, 0.5);
    let sym = i32(input.symbol + 0.5);

    var dist: f32;
    switch (sym) {
        case 1:  { dist = sdf_square(p); }
        case 2:  { dist = sdf_diamond(p); }
        case 3:  { dist = sdf_triangle_up(p); }
        case 4:  { dist = sdf_triangle_down(p); }
        case 5:  { dist = sdf_cross(p); }
        case 6:  { dist = sdf_plus(p); }
        case 7:  { dist = sdf_star(p); }
        case 8:  { dist = sdf_ring(p); }
        case 9:  { dist = sdf_square_outline(p); }
        default: { dist = sdf_circle(p); }
    }

    if (dist > 1.0) {
        discard;
    }
    let alpha = input.color.a * smoothstep(1.0, 0.7, dist);
    return vec4f(input.color.rgb, alpha);
}
)";

static void onAdapterReady(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                            WGPUStringView message, void* ud1, void* ud2) {
    (void)ud2;
    if (status == WGPURequestAdapterStatus_Success)
        *static_cast<WGPUAdapter*>(ud1) = adapter;
    else
        fprintf(stderr, "Adapter request failed: %.*s\n", (int)message.length, message.data);
}

static void onDeviceReady(WGPURequestDeviceStatus status, WGPUDevice device,
                           WGPUStringView message, void* ud1, void* ud2) {
    (void)ud2;
    if (status == WGPURequestDeviceStatus_Success)
        *static_cast<WGPUDevice*>(ud1) = device;
    else
        fprintf(stderr, "Device request failed: %.*s\n", (int)message.length, message.data);
}

WebGPUContext::WebGPUContext() = default;

WebGPUContext::~WebGPUContext() {
    Cleanup();
}

bool WebGPUContext::Initialize() {
    m_instance = wgpuCreateInstance(nullptr);
    if (!m_instance) {
        fprintf(stderr, "Failed to create WebGPU instance\n");
        return false;
    }

    // Request adapter (no surface needed at this point)
    WGPURequestAdapterOptions opts = {};
    WGPURequestAdapterCallbackInfo cb = {};
    cb.callback = onAdapterReady;
    cb.userdata1 = &m_adapter;
    wgpuInstanceRequestAdapter(m_instance, &opts, cb);
    if (!m_adapter) { fprintf(stderr, "Failed to get adapter\n"); return false; }

    // Request device
    WGPURequestDeviceCallbackInfo dcb = {};
    dcb.callback = onDeviceReady;
    dcb.userdata1 = &m_device;
    wgpuAdapterRequestDevice(m_adapter, nullptr, dcb);
    if (!m_device) { fprintf(stderr, "Failed to get device\n"); return false; }

    m_queue = wgpuDeviceGetQueue(m_device);

    CreateSharedResources();
    m_initialized = true;
    fprintf(stderr, "WebGPU context initialized\n");
    return true;
}

void WebGPUContext::CreateSharedResources() {
    // Quad vertex buffer (unit square, 6 vertices for 2 triangles)
    float quadVerts[] = {
        -0.5f, -0.5f,  0.5f, -0.5f,  0.5f, 0.5f,
        -0.5f, -0.5f,  0.5f,  0.5f, -0.5f, 0.5f,
    };
    WGPUBufferDescriptor qbDesc = {};
    qbDesc.label = {"quad_vertices", WGPU_STRLEN};
    qbDesc.size = sizeof(quadVerts);
    qbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_quadBuffer = wgpuDeviceCreateBuffer(m_device, &qbDesc);
    wgpuQueueWriteBuffer(m_queue, m_quadBuffer, 0, quadVerts, sizeof(quadVerts));

    // Shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = {kPointShaderSource, WGPU_STRLEN};
    WGPUShaderModuleDescriptor smDesc = {};
    smDesc.nextInChain = &wgslDesc.chain;
    m_shaderModule = wgpuDeviceCreateShaderModule(m_device, &smDesc);

    // Bind group layout 0: uniforms (used by all pipelines)
    WGPUBindGroupLayoutEntry bglEntry0 = {};
    bglEntry0.binding = 0;
    bglEntry0.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bglEntry0.buffer.type = WGPUBufferBindingType_Uniform;
    bglEntry0.buffer.minBindingSize = sizeof(Uniforms);

    WGPUBindGroupLayoutDescriptor bglDesc0 = {};
    bglDesc0.entryCount = 1;
    bglDesc0.entries = &bglEntry0;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device, &bglDesc0);

    // Bind group layout 1: selection + brush data (point pipelines only)
    WGPUBindGroupLayoutEntry selEntries[3] = {};
    // Binding 0: selection buffer (read-only storage)
    selEntries[0].binding = 0;
    selEntries[0].visibility = WGPUShaderStage_Vertex;
    selEntries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    selEntries[0].buffer.minBindingSize = 4;  // at least 1 u32
    // Binding 1: brush colors (uniform, 8 × vec4f = 128 bytes)
    selEntries[1].binding = 1;
    selEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    selEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
    selEntries[1].buffer.minBindingSize = 8 * 16;  // 8 vec4f
    // Binding 2: brush params (uniform, 8 × vec4f for symbol/sizeScale/showUnsel/pad)
    selEntries[2].binding = 2;
    selEntries[2].visibility = WGPUShaderStage_Vertex;
    selEntries[2].buffer.type = WGPUBufferBindingType_Uniform;
    selEntries[2].buffer.minBindingSize = 8 * 16;

    WGPUBindGroupLayoutDescriptor selBglDesc = {};
    selBglDesc.entryCount = 3;
    selBglDesc.entries = selEntries;
    m_selectionBindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device, &selBglDesc);

    // Pipeline layout for points: 2 bind groups
    WGPUBindGroupLayout layouts[2] = {m_bindGroupLayout, m_selectionBindGroupLayout};
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 2;
    plDesc.bindGroupLayouts = layouts;
    m_pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &plDesc);

    // Pipeline layout for histograms: 1 bind group (uniforms only)
    WGPUPipelineLayoutDescriptor histPlDesc = {};
    histPlDesc.bindGroupLayoutCount = 1;
    histPlDesc.bindGroupLayouts = &m_bindGroupLayout;
    m_histPipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &histPlDesc);
}

void WebGPUContext::Cleanup() {
    if (m_histPipelineLayout) { wgpuPipelineLayoutRelease(m_histPipelineLayout); m_histPipelineLayout = nullptr; }
    if (m_pipelineLayout) { wgpuPipelineLayoutRelease(m_pipelineLayout); m_pipelineLayout = nullptr; }
    if (m_selectionBindGroupLayout) { wgpuBindGroupLayoutRelease(m_selectionBindGroupLayout); m_selectionBindGroupLayout = nullptr; }
    if (m_bindGroupLayout) { wgpuBindGroupLayoutRelease(m_bindGroupLayout); m_bindGroupLayout = nullptr; }
    if (m_shaderModule) { wgpuShaderModuleRelease(m_shaderModule); m_shaderModule = nullptr; }
    if (m_quadBuffer) { wgpuBufferRelease(m_quadBuffer); m_quadBuffer = nullptr; }
    if (m_queue) { wgpuQueueRelease(m_queue); m_queue = nullptr; }
    if (m_device) { wgpuDeviceRelease(m_device); m_device = nullptr; }
    if (m_adapter) { wgpuAdapterRelease(m_adapter); m_adapter = nullptr; }
    if (m_instance) { wgpuInstanceRelease(m_instance); m_instance = nullptr; }
    m_initialized = false;
}
