#pragma once

#include <webgpu.h>
#include <wgpu.h>

// Shared WebGPU resources used by all plot canvases.
// Created once by MainFrame, passed to each WebGPUCanvas.
class WebGPUContext {
public:
    WebGPUContext();
    ~WebGPUContext();

    bool Initialize();
    bool IsInitialized() const { return m_initialized; }

    // Shared resources
    WGPUInstance GetInstance() const { return m_instance; }
    WGPUAdapter GetAdapter() const { return m_adapter; }
    WGPUDevice GetDevice() const { return m_device; }
    WGPUQueue GetQueue() const { return m_queue; }

    // Shared GPU buffers & objects
    WGPUBuffer GetQuadBuffer() const { return m_quadBuffer; }
    WGPUShaderModule GetShaderModule() const { return m_shaderModule; }
    WGPUBindGroupLayout GetBindGroupLayout() const { return m_bindGroupLayout; }
    WGPUPipelineLayout GetPipelineLayout() const { return m_pipelineLayout; }

private:
    void CreateSharedResources();
    void Cleanup();

    WGPUInstance m_instance = nullptr;
    WGPUAdapter m_adapter = nullptr;
    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;

    WGPUBuffer m_quadBuffer = nullptr;
    WGPUShaderModule m_shaderModule = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUPipelineLayout m_pipelineLayout = nullptr;

    bool m_initialized = false;
};
