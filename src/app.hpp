#pragma once

#include <imgui.h>
#include <glm/glm.hpp>
#include <ranges>
#include <reactive/Window.hpp>
#include <reactive/reactive.hpp>
#include <string>

#include "../shader/shared.inc"
#include "pass.hpp"
#include "scene.hpp"

struct ShaderInfo
{
    std::string fileName;
    std::string entryPoint;
    int32_t shaderIndex = -1;
};

struct GraphicsPipeline
{
    ShaderInfo vertexShaderInfo;
    ShaderInfo fragmentShaderInfo;
    rv::GraphicsPipelineHandle pipeline;
};

struct MeshShaderPipeline
{
    ShaderInfo taskShaderInfo;
    ShaderInfo meshShaderInfo;
    ShaderInfo fragmentShaderInfo;
    rv::MeshShaderPipelineHandle pipeline;
};

struct ComputePipeline
{
    ShaderInfo computeShaderInfo;
    rv::ComputePipelineHandle pipeline;
};

class FluidApp final : public rv::App {
public:
    FluidApp()
        : rv::App({
            .width = 1920,
            .height = 1080,
            .title = "Mesh Shader Surface Reconstruction",
            .windowResizable = false,
            .vsync = true,
            .layers = {rv::Layer::Validation, rv::Layer::FPSMonitor},
            .extensions = {rv::Extension::MeshShader, rv::Extension::ExtendedDynamicState},
        })
    {
    }

    void onStart() override
    {
        createGpuTimers();
        createScene();
        createBuffers();
        createImages();
        createPipelines();
    }

    void onUpdate(float dt) override
    {
        context.getQueue().waitIdle();

        numParticles = scene.getParticleCount();

        // Update camera
        camera.processKey();
        glm::vec2 _mouseDragLeft = rv::Window::getMouseDragLeft();
        glm::vec2 _mouseDragRight = rv::Window::getMouseDragRight();
        camera.processMouseDragLeft(glm::vec2{_mouseDragLeft.x, -_mouseDragLeft.y} * 0.5f);
        camera.processMouseDragRight(glm::vec2{_mouseDragRight.x, -_mouseDragRight.y} * 0.5f);
        camera.processMouseScroll(rv::Window::getMouseScroll());

        pushConstants.viewProj = camera.getProj() * camera.getView();
        pushConstants.cameraPos = glm::vec4(camera.getPosition(), 1.0);
        pushConstants.resolution = {rv::Window::getWidth(), rv::Window::getHeight()};
        pushConstants.maxParticleCount = numParticles;

        std::memcpy(particleBuffer->map(), scene.getData(), scene.getSize());

        if (runPhysics) {
            scene.update();
        }
    }

    void onRender(const rv::CommandBufferHandle& commandBuffer) override
    {
        renderGUI();

        // Clear images
        rv::ImageHandle colorImage = getCurrentColorImage();
        commandBuffer->clearColorImage(colorImage, {0.1f, 0.1f, 0.1f, 1.0f});
        commandBuffer->clearColorImage(opaquePosImage, {0.0f, 0.0f, 0.0f, 0.0f});
        commandBuffer->clearColorImage(opaqueColorImage, {0.0f, 0.0f, 0.0f, 0.0f});
        commandBuffer->clearDepthStencilImage(depthImage, 1.0f, 0);
        commandBuffer->transitionLayout(colorImage, vk::ImageLayout::eColorAttachmentOptimal);
        commandBuffer->transitionLayout(opaquePosImage, vk::ImageLayout::eColorAttachmentOptimal);
        commandBuffer->transitionLayout(opaqueColorImage, vk::ImageLayout::eColorAttachmentOptimal);

        // Render background
        const uint32_t width = rv::Window::getWidth();
        const uint32_t height = rv::Window::getHeight();
        commandBuffer->beginDebugLabel("Background");
        backgroundPass.run(commandBuffer, colorImage, camera.getInvView(), camera.getInvProj(),
                           width, height);
        commandBuffer->endDebugLabel();

        // Render opaque meshes
        if (showMeshes) {
            commandBuffer->beginDebugLabel("Opaque mesh");
            commandBuffer->beginRendering({colorImage, opaqueColorImage, opaquePosImage},
                                          depthImage,  //
                                          {0, 0}, {width, height});
            commandBuffer->setViewport(width, height);
            commandBuffer->setScissor(width, height);

            // Draw meshes
            commandBuffer->bindDescriptorSet(descSet, graphicsPipelines["Mesh"].pipeline);
            commandBuffer->bindPipeline(graphicsPipelines["Mesh"].pipeline);
            commandBuffer->pushConstants(graphicsPipelines["Mesh"].pipeline, &pushConstants);

            for (auto& mesh : scene.meshes) {
                commandBuffer->bindVertexBuffer(mesh.vertexBuffer);
                commandBuffer->bindIndexBuffer(mesh.indexBuffer);
                commandBuffer->drawIndexed(static_cast<uint32_t>(mesh.indices.size()), 1);
            }

            commandBuffer->endRendering();
            commandBuffer->endDebugLabel();
        }
        commandBuffer->transitionLayout(opaquePosImage, vk::ImageLayout::eShaderReadOnlyOptimal);
        commandBuffer->transitionLayout(opaqueColorImage, vk::ImageLayout::eShaderReadOnlyOptimal);

        // Clear buffers
        clearBuffers(commandBuffer);

        // Render surface
        renderSurface(commandBuffer);

        // Render debug elements
        {
            commandBuffer->beginDebugLabel("Debug elements");
            commandBuffer->beginRendering(colorImage, depthImage, {0, 0}, {width, height});
            commandBuffer->setViewport(width, height);
            commandBuffer->setScissor(width, height);

            // Draw particles
            if (showParticles) {
                draw(commandBuffer, "Particle", numParticles);
            }

            // Draw surface vertex
            if (showSurfaceVertex) {
                draw(commandBuffer, "SurfaceVertex", numVertices);
            }

            // Draw bottom grid
            if (showBottomGrid) {
                drawBottomGrid(commandBuffer);
            }

            // Draw top grid
            if (showTopGrid) {
                commandBuffer->bindDescriptorSet(descSet, graphicsPipelines["TopGrid"].pipeline);
                commandBuffer->bindPipeline(graphicsPipelines["TopGrid"].pipeline);
                commandBuffer->pushConstants(graphicsPipelines["TopGrid"].pipeline, &pushConstants);
                commandBuffer->setLineWidth(lineWidth);

                commandBuffer->bindVertexBuffer(cubeLineMesh.vertexBuffer);
                commandBuffer->bindIndexBuffer(cubeLineMesh.indexBuffer);
                commandBuffer->drawIndexed(static_cast<uint32_t>(cubeLineMesh.indices.size()),
                                           numBlocks);
            }

            commandBuffer->endRendering();
            commandBuffer->endDebugLabel();
        }
        frame++;
    }

private:
    void createGpuTimers()
    {
        auto limits = context.getPhysicalDeviceLimits();
        if (limits.timestampPeriod == 0.0f) {
            throw std::runtime_error{"The selected device does not support timestamp queries!"};
        }

        if (!limits.timestampComputeAndGraphics) {
            // Check if the graphics queue used in this sample supports time stamps
            auto properties = context.getPhysicalDevice().getQueueFamilyProperties();
            if (properties[context.getQueueFamily()].timestampValidBits == 0) {
                throw std::runtime_error{"The queue family does not support timestamp queries!"};
            }
        }

        gpuTimers[0] = context.createGPUTimer({});
        gpuTimers[1] = context.createGPUTimer({});
    }

    void createScene()
    {
        scene.load(ASSET_DIR + "FluidBeach.abc");
        for (auto& mesh : scene.meshes) {
            mesh.allocate(context);
        }

        cubeLineMesh = rv::Mesh::createCubeLineMesh(context, {});

        const uint32_t width = rv::Window::getWidth();
        const uint32_t height = rv::Window::getHeight();
        const float aspect = static_cast<float>(width) / height;
        camera = {rv::Camera::Type::Orbital, aspect};
        camera.setDistance(30.0f);
        camera.setTheta(-20.0f);
        camera.setPhi(30.0f);
        camera.setTarget({0.0f, -5.0f, 0.0f});
        camera.setFovY(glm::radians(30.0f));
    }

    void createBuffers()
    {
        // Particle
        particleBuffer = context.createBuffer({
            .usage = rv::BufferUsage::Storage,
            .memory = rv::MemoryUsage::DeviceHost,
            .size = sizeof(glm::vec4) * scene.maxParticleCount,
        });

        // Grid
        bottomGridParticleCounts = context.createBuffer({
            .usage = rv::BufferUsage::Storage,
            .memory = rv::MemoryUsage::Device,
            .size = sizeof(uint32_t) * numCells,
        });
        bottomGridParticleIndices = context.createBuffer({
            .usage = rv::BufferUsage::Storage,
            .memory = rv::MemoryUsage::Device,
            .size = sizeof(uint32_t) * numCells * maxParticlesPerCell,
        });
        topGridValidCellCounts = context.createBuffer({
            .usage = rv::BufferUsage::Storage,
            .memory = rv::MemoryUsage::Device,
            .size = sizeof(uint32_t) * numBlocks,
        });

        // Surface cell & particle & vertex
        surfaceCellBuffer = context.createBuffer({
            .usage = rv::BufferUsage::Storage,
            .memory = rv::MemoryUsage::Device,
            .size = sizeof(uint32_t) * numCells,
        });
        surfaceVertexBuffer = context.createBuffer({
            .usage = rv::BufferUsage::Storage,
            .memory = rv::MemoryUsage::Device,
            .size = sizeof(uint32_t) * numVertices,
        });
        compressedVertexBuffer = context.createBuffer({
            .usage = rv::BufferUsage::Storage,
            .memory = rv::MemoryUsage::Device,
            .size = sizeof(uint32_t) * numVertices,
        });
        densityBuffer = context.createBuffer({
            .usage = rv::BufferUsage::Storage,
            .memory = rv::MemoryUsage::Device,
            .size = sizeof(float) * numVertices,
        });

        // Normal
        cellVertexNormalBuffer = context.createBuffer({
            .usage = rv::BufferUsage::Storage,
            .memory = rv::MemoryUsage::Device,
            .size = sizeof(glm::vec4) * numVertices,
        });

        // Counter
        surfaceCountBuffer = context.createBuffer({
            .usage = rv::BufferUsage::Storage,
            .memory = rv::MemoryUsage::Host,
            .size = sizeof(uint32_t) * 6,
        });

        // Surface block
        surfaceBlockBuffer = context.createBuffer({
            .usage = rv::BufferUsage::Storage,
            .memory = rv::MemoryUsage::Device,
            .size = sizeof(uint32_t) * numBlocks,
        });

        // Indirect dispatch command
        uint32_t indirectDispatchCommandCount = 3;
        indirectDispatchCommandBuffer = context.createBuffer({
            .usage = rv::BufferUsage::Indirect,
            .memory = rv::MemoryUsage::Host,
            .size = sizeof(glm::uvec4) * indirectDispatchCommandCount,
        });

        uint64_t memorySize = particleBuffer->getSize()               //
                              + bottomGridParticleCounts->getSize()   //
                              + bottomGridParticleIndices->getSize()  //
                              + surfaceVertexBuffer->getSize()        //
                              + compressedVertexBuffer->getSize()     //
                              + densityBuffer->getSize()              //
                              + cellVertexNormalBuffer->getSize()     //
                              + topGridValidCellCounts->getSize()     //
                              + surfaceBlockBuffer->getSize();
        spdlog::info("Shared buffer size: {} MB", memorySize / 1024.0 / 1024.0);
    }

    void createImages()
    {
        const uint32_t width = rv::Window::getWidth();
        const uint32_t height = rv::Window::getHeight();

        depthImage = context.createImage({
            .usage = rv::ImageUsage::DepthAttachment,
            .extent = {rv::Window::getWidth(), rv::Window::getHeight(), 1},
            .format = depthFormat,
            .debugName = "depthImage",
        });

        envIrradianceImage
            = rv::Image::loadFromKTX(context, ASSET_DIR + "environments/papermill_irradiance.ktx");

        envRadianceImage
            = rv::Image::loadFromKTX(context, ASSET_DIR + "environments/papermill_radiance.ktx");

        opaquePosImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eColorAttachment |  //
                     vk::ImageUsageFlagBits::eSampled |          //
                     vk::ImageUsageFlagBits::eTransferSrc |      //
                     vk::ImageUsageFlagBits::eTransferDst,
            .extent = {width, height, 1},
            .format = vk::Format::eR32G32B32A32Sfloat,
            .debugName = "opaquePosImage",
        });

        opaqueColorImage = context.createImage({
            .usage = vk::ImageUsageFlagBits::eColorAttachment |  //
                     vk::ImageUsageFlagBits::eSampled |          //
                     vk::ImageUsageFlagBits::eTransferSrc |      //
                     vk::ImageUsageFlagBits::eTransferDst,
            .extent = {width, height, 1},
            .format = colorFormat,
            .debugName = "opaqueColorImage",
        });

        depthImage->createImageView(vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eDepth);
        opaquePosImage->createImageView(vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor);
        opaqueColorImage->createImageView(vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor);
        opaquePosImage->createSampler();
        opaqueColorImage->createSampler();

        context.oneTimeSubmit([this](rv::CommandBufferHandle commandBuffer) {
            commandBuffer->transitionLayout(opaquePosImage,
                                            vk::ImageLayout::eShaderReadOnlyOptimal);
            commandBuffer->transitionLayout(opaqueColorImage,
                                            vk::ImageLayout::eShaderReadOnlyOptimal);
        });
    }

    void createPipelines()
    {
        // Create shaders
        std::vector<rv::ShaderHandle> shaders;
        for (auto& graphicsPipeline : graphicsPipelines | std::views::values) {
            graphicsPipeline.vertexShaderInfo.shaderIndex = static_cast<int32_t>(shaders.size());
            shaders.push_back(createShader(graphicsPipeline.vertexShaderInfo));

            graphicsPipeline.fragmentShaderInfo.shaderIndex = static_cast<int32_t>(shaders.size());
            shaders.push_back(createShader(graphicsPipeline.fragmentShaderInfo));
        }
        for (auto& computePipeline : computePipelines | std::views::values) {
            computePipeline.computeShaderInfo.shaderIndex = static_cast<int32_t>(shaders.size());
            shaders.push_back(createShader(computePipeline.computeShaderInfo));
        }

        for (auto& meshShaderPipeline : meshShaderPipelines | std::views::values) {
            if (!meshShaderPipeline.taskShaderInfo.fileName.empty()) {
                meshShaderPipeline.taskShaderInfo.shaderIndex
                    = static_cast<int32_t>(shaders.size());
                shaders.push_back(createShader(meshShaderPipeline.taskShaderInfo));
            }

            meshShaderPipeline.meshShaderInfo.shaderIndex = static_cast<int32_t>(shaders.size());
            shaders.push_back(createShader(meshShaderPipeline.meshShaderInfo));

            meshShaderPipeline.fragmentShaderInfo.shaderIndex
                = static_cast<int32_t>(shaders.size());
            shaders.push_back(createShader(meshShaderPipeline.fragmentShaderInfo));
        }

        // Create descriptor set
        descSet = context.createDescriptorSet({
            .shaders = shaders,
            .buffers = {{"ParticlePositions", particleBuffer},
                        // Counter
                        {"SurfaceCounts", surfaceCountBuffer},
                        // Surface cell & particle
                        {"SurfaceCells", surfaceCellBuffer},
                        {"SurfaceVertices", surfaceVertexBuffer},
                        {"CompressedVertices", compressedVertexBuffer},
                        {"Density", densityBuffer},
                        {"CellVertexNormals", cellVertexNormalBuffer},
                        {"DispatchIndirectCommands", indirectDispatchCommandBuffer},
                        {"BottomGridParticleCounts", bottomGridParticleCounts},
                        {"BottomGridParticleIndices", bottomGridParticleIndices},
                        {"TopGridValidCellCounts", topGridValidCellCounts},
                        {"SurfaceBlocks", surfaceBlockBuffer},
            },
            .images = {
                {"envIrradianceImage", envIrradianceImage},
                {"posImage", opaquePosImage},
                {"colorImage", opaqueColorImage},
                {"envRadianceImage", envRadianceImage},
            },
        });
        descSet->update();

        // Create pipelines
        graphicsPipelines["Mesh"].pipeline = context.createGraphicsPipeline({
            .descSetLayout = descSet->getLayout(),
            .pushSize = sizeof(PushConstants),
            .vertexShader = shaders[graphicsPipelines["Mesh"].vertexShaderInfo.shaderIndex],
            .fragmentShader = shaders[graphicsPipelines["Mesh"].fragmentShaderInfo.shaderIndex],
            .vertexStride = sizeof(Scene::Vertex),
            .vertexAttributes = {{
                {.offset = offsetof(Scene::Vertex, pos), .format = vk::Format::eR32G32B32Sfloat},
                {.offset = offsetof(Scene::Vertex, normal), .format = vk::Format::eR32G32B32Sfloat},
            }},
            .colorFormats = {
                colorFormat,
                colorFormat,
                vk::Format::eR32G32B32A32Sfloat,
            },
            .depthFormat = depthFormat,
        });

        graphicsPipelines["Particle"].pipeline = context.createGraphicsPipeline({
            .descSetLayout = descSet->getLayout(),
            .pushSize = sizeof(PushConstants),
            .vertexShader = shaders[graphicsPipelines["Particle"].vertexShaderInfo.shaderIndex],
            .fragmentShader = shaders[graphicsPipelines["Particle"].fragmentShaderInfo.shaderIndex],
            .colorFormats = {colorFormat},
            .depthFormat = depthFormat,
            .topology = vk::PrimitiveTopology::ePointList,
            .polygonMode = vk::PolygonMode::ePoint,
        });

        graphicsPipelines["SurfaceVertex"].pipeline = context.createGraphicsPipeline({
            .descSetLayout = descSet->getLayout(),
            .pushSize = sizeof(PushConstants),
            .vertexShader
            = shaders[graphicsPipelines["SurfaceVertex"].vertexShaderInfo.shaderIndex],
            .fragmentShader
            = shaders[graphicsPipelines["SurfaceVertex"].fragmentShaderInfo.shaderIndex],
            .colorFormats = {colorFormat},
            .depthFormat = depthFormat,
            .topology = vk::PrimitiveTopology::ePointList,
            .polygonMode = vk::PolygonMode::ePoint,
        });

        for (auto& [name, computePipeline] : computePipelines) {
            computePipelines[name].pipeline = context.createComputePipeline({
                .computeShader = shaders[computePipeline.computeShaderInfo.shaderIndex],
                .descSetLayout = descSet->getLayout(),
                .pushSize = sizeof(PushConstants),
            });
        }

        graphicsPipelines["BottomGrid"].pipeline = context.createGraphicsPipeline({
            .descSetLayout = descSet->getLayout(),
            .pushSize = sizeof(PushConstants),
            .vertexShader = shaders[graphicsPipelines["BottomGrid"].vertexShaderInfo.shaderIndex],
            .fragmentShader
            = shaders[graphicsPipelines["BottomGrid"].fragmentShaderInfo.shaderIndex],
            .vertexStride = sizeof(rv::Vertex),
            .vertexAttributes = rv::Vertex::getAttributeDescriptions(),
            .colorFormats = {colorFormat},
            .depthFormat = depthFormat,
            .topology = vk::PrimitiveTopology::eLineList,
            .polygonMode = vk::PolygonMode::eLine,
            .lineWidth = "dynamic",
        });

        graphicsPipelines["TopGrid"].pipeline = context.createGraphicsPipeline({
            .descSetLayout = descSet->getLayout(),
            .pushSize = sizeof(PushConstants),
            .vertexShader = shaders[graphicsPipelines["TopGrid"].vertexShaderInfo.shaderIndex],
            .fragmentShader = shaders[graphicsPipelines["TopGrid"].fragmentShaderInfo.shaderIndex],
            .vertexStride = sizeof(rv::Vertex),
            .vertexAttributes = rv::Vertex::getAttributeDescriptions(),
            .colorFormats = {colorFormat},
            .depthFormat = depthFormat,
            .topology = vk::PrimitiveTopology::eLineList,
            .polygonMode = vk::PolygonMode::eLine,
            .lineWidth = "dynamic",
        });

        meshShaderPipelines["SurfacePerBlock"].pipeline = context.createMeshShaderPipeline({
            .descSetLayout = descSet->getLayout(),
            .pushSize = sizeof(PushConstants),
            .taskShader = {},
            .meshShader
            = shaders[meshShaderPipelines["SurfacePerBlock"].meshShaderInfo.shaderIndex],
            .fragmentShader
            = shaders[meshShaderPipelines["SurfacePerBlock"].fragmentShaderInfo.shaderIndex],
            .colorFormats = {colorFormat},
            .depthFormat = depthFormat,
            .lineWidth = "dynamic",
        });
        backgroundPass.init(context, envRadianceImage);
    }

    void showTimeline(float frameTime)
    {
        for (int i = 0; i < TIME_BUFFER_SIZE - 1; i++) {
            times[i] = times[i + 1];
        }
        times[TIME_BUFFER_SIZE - 1] = frameTime;
        ImGui::PlotLines("Times", times, TIME_BUFFER_SIZE, 0, nullptr,  //
                         FLT_MAX, FLT_MAX, {300, 150});
    }

    void displayComputedCounts() const
    {
        if (ImGui::TreeNode("Computed counts")) {
            uint32_t* counts = static_cast<uint32_t*>(surfaceCountBuffer->map());
            ImGui::Text("Surface cells: %d", counts[1]);
            ImGui::Text("Surface particles: %d", counts[2]);
            ImGui::Text("Surface vertices: %d", counts[3]);
            ImGui::Text("Densities: %d", counts[4]);
            ImGui::TreePop();
        }
    }

    void displayDispatchCommandsInfo() const
    {
        if (ImGui::TreeNode("Dispatch commands")) {
            auto dispatchCommands = static_cast<glm::uvec4*>(indirectDispatchCommandBuffer->map());
            ImGui::Text("Dispatch[density]: %d", dispatchCommands[densityCommandIndex].x);
            ImGui::Text("Dispatch[marchingCubes]: %d",
                        dispatchCommands[marchingCubesCommandIndex].x);
            ImGui::Text("Dispatch[surfaceCellWithBlock]: %d",
                        dispatchCommands[surfaceCellWithBlockCommandIndex].x);
            ImGui::TreePop();
        }
    }

    void renderSurface(const rv::CommandBufferHandle& commandBuffer)
    {
        // Compute
        {
            commandBuffer->beginTimestamp(gpuTimers[0]);

            commandBuffer->beginDebugLabel("BuildGrids");
            fillTwoGrids(commandBuffer);  // changed
            commandBuffer->endDebugLabel();

            commandBuffer->beginDebugLabel("DetectSurface");
            computeSurfaceBlock(commandBuffer);  // added
            computeSurfaceCell(commandBuffer);   // changed
            compressSurfaceVertex(commandBuffer);
            commandBuffer->endDebugLabel();

            commandBuffer->beginDebugLabel("ComputeDensity");
            computeDensity(commandBuffer);
            computeCellVertexNormal(commandBuffer);
            commandBuffer->endDebugLabel();

            commandBuffer->endTimestamp(gpuTimers[0]);
        }

        // Rendering
        {
            const uint32_t width = rv::Window::getWidth();
            const uint32_t height = rv::Window::getHeight();
            displayComputedCounts();
            displayDispatchCommandsInfo();
            commandBuffer->beginTimestamp(gpuTimers[1]);
            commandBuffer->beginDebugLabel("MC and Draw");
            commandBuffer->beginRendering(getCurrentColorImage(), depthImage, {0, 0},
                                          {width, height});
            commandBuffer->setViewport(width, height);
            commandBuffer->setScissor(width, height);
            commandBuffer->setLineWidth(lineWidth);

            // Draw Surface
            if (showSurface) {
                commandBuffer->bindDescriptorSet(descSet,
                                                 meshShaderPipelines["SurfacePerBlock"].pipeline);
                commandBuffer->bindPipeline(meshShaderPipelines["SurfacePerBlock"].pipeline);
                commandBuffer->pushConstants(meshShaderPipelines["SurfacePerBlock"].pipeline,
                                             &pushConstants);
                commandBuffer->drawMeshTasksIndirect(
                    indirectDispatchCommandBuffer,
                    sizeof(glm::uvec4) * surfaceCellWithBlockCommandIndex, 1,
                    sizeof(vk::DrawMeshTasksIndirectCommandEXT));
            }

            commandBuffer->endRendering();
            commandBuffer->endDebugLabel();
            commandBuffer->endTimestamp(gpuTimers[1]);
        }
    }

    void renderGUI()
    {
        // Parameters
        ImGui::SliderFloat("Kernel radius", &pushConstants.kernelRadius, 0.05f, 0.2f);
        ImGui::SliderFloat("Kernel scale", &pushConstants.kernelScale, 0.05f, 20.0f);
        ImGui::SliderFloat("Iso value", &pushConstants.isoValue, 0.001f, 0.1f);

        // Frame
        ImGui::SliderInt("Scene frame", &scene.frame, 0, scene.frameCount - 1);

        // Physics
        ImGui::Checkbox("Run physics", &runPhysics);

        if (frame > 0) {
            float computeTime = gpuTimers[0]->elapsedInMilli();
            float renderingTime = gpuTimers[1]->elapsedInMilli();
            float frameTime = computeTime + renderingTime;

            ImGui::Text("Frame time: %.3f ms", frameTime);
            showTimeline(frameTime);
        }

        // Recompile shaders
        if (ImGui::Button("Recompile")) {
            try {
                rv::CPUTimer timer;
                createPipelines();
                spdlog::info("Recreate: {}ms", timer.elapsedInMilli());
            } catch (const std::exception& e) {
                spdlog::error(e.what());
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        // Debug view
        if (ImGui::TreeNode("Debug view")) {
            ImGui::SliderFloat("Point size", &pushConstants.pointSize, 1.0f, 10.0f);
            ImGui::SliderFloat("Line width", &lineWidth, 1.0f, 5.0f);
            ImGui::Checkbox("Draw colliders", &showMeshes);
            ImGui::Checkbox("Draw particles", &showParticles);
            ImGui::Checkbox("Draw surface vertices", &showSurfaceVertex);
            ImGui::Checkbox("Draw bottom grid", &showBottomGrid);
            ImGui::Checkbox("Draw top grid", &showTopGrid);
            ImGui::Checkbox("Draw surface", &showSurface);
            ImGui::Checkbox("Draw line", &surfaceDrawLine);

            ImGui::TreePop();
        }
    }

    void dispatch(const rv::CommandBufferHandle& commandBuffer,
                  const std::string& name,
                  uint32_t x,
                  uint32_t y,
                  uint32_t z)
    {
        const auto& pipeline = computePipelines[name].pipeline;
        commandBuffer->bindDescriptorSet(descSet, pipeline);
        commandBuffer->bindPipeline(pipeline);
        commandBuffer->pushConstants(pipeline, &pushConstants);
        commandBuffer->dispatch(x, y, z);
    }

    void draw(const rv::CommandBufferHandle& commandBuffer,
              const std::string& name,
              uint32_t vertexCount)
    {
        const auto& pipeline = graphicsPipelines[name].pipeline;
        commandBuffer->beginDebugLabel(name.c_str());
        commandBuffer->bindDescriptorSet(descSet, pipeline);
        commandBuffer->bindPipeline(pipeline);
        commandBuffer->pushConstants(pipeline, &pushConstants);
        commandBuffer->draw(vertexCount, 1, 0, 0);
        commandBuffer->endDebugLabel();
    }

    void fillTwoGrids(const rv::CommandBufferHandle& commandBuffer)
    {
        dispatch(commandBuffer, "FillTwoGrids", divRoundUp(numParticles, 32), 1, 1);
        commandBuffer->bufferBarrier(
            {bottomGridParticleCounts, bottomGridParticleIndices, topGridValidCellCounts},
            vk::PipelineStageFlagBits::eComputeShader,  //
            vk::PipelineStageFlagBits::eComputeShader,  //
            vk::AccessFlagBits::eShaderWrite,           //
            vk::AccessFlagBits::eShaderRead);
    }

    void computeSurfaceBlock(const rv::CommandBufferHandle& commandBuffer)
    {
        dispatch(commandBuffer, "SurfaceBlock", divRoundUp(numBlocks, 32), 1, 1);
        commandBuffer->bufferBarrier(surfaceBlockBuffer,
                                     vk::PipelineStageFlagBits::eComputeShader,  //
                                     vk::PipelineStageFlagBits::eComputeShader,  //
                                     vk::AccessFlagBits::eShaderWrite,           //
                                     vk::AccessFlagBits::eShaderRead);
        commandBuffer->bufferBarrier(indirectDispatchCommandBuffer,
                                     vk::PipelineStageFlagBits::eComputeShader,  //
                                     vk::PipelineStageFlagBits::eDrawIndirect,   //
                                     vk::AccessFlagBits::eShaderWrite,           //
                                     vk::AccessFlagBits::eIndirectCommandRead);
    }

    void computeSurfaceCell(const rv::CommandBufferHandle& commandBuffer)
    {
        auto& pipeline = computePipelines.at("SurfaceCell").pipeline;
        commandBuffer->bindDescriptorSet(descSet, pipeline);
        commandBuffer->bindPipeline(pipeline);
        commandBuffer->pushConstants(pipeline, &pushConstants);
        commandBuffer->dispatchIndirect(indirectDispatchCommandBuffer,
                                        sizeof(glm::uvec4) * surfaceCellWithBlockCommandIndex);

        commandBuffer->bufferBarrier(indirectDispatchCommandBuffer,
                                     vk::PipelineStageFlagBits::eComputeShader,  //
                                     vk::PipelineStageFlagBits::eDrawIndirect,   //
                                     vk::AccessFlagBits::eShaderWrite,           //
                                     vk::AccessFlagBits::eIndirectCommandRead);
    }

    void compressSurfaceVertex(const rv::CommandBufferHandle& commandBuffer)
    {
        dispatch(commandBuffer, "CompressVertex", divRoundUp(numVertices, 32), 1, 1);
        commandBuffer->bufferBarrier({surfaceCountBuffer, compressedVertexBuffer},
                                     vk::PipelineStageFlagBits::eComputeShader,  //
                                     vk::PipelineStageFlagBits::eComputeShader,  //
                                     vk::AccessFlagBits::eShaderWrite,           //
                                     vk::AccessFlagBits::eShaderRead);
        commandBuffer->bufferBarrier(indirectDispatchCommandBuffer,
                                     vk::PipelineStageFlagBits::eComputeShader,  //
                                     vk::PipelineStageFlagBits::eDrawIndirect,   //
                                     vk::AccessFlagBits::eShaderWrite,           //
                                     vk::AccessFlagBits::eIndirectCommandRead);
    }

    void computeDensity(const rv::CommandBufferHandle& commandBuffer)
    {
        auto& pipeline = computePipelines.at("Density").pipeline;
        commandBuffer->bindDescriptorSet(descSet, pipeline);
        commandBuffer->bindPipeline(pipeline);
        commandBuffer->pushConstants(pipeline, &pushConstants);
        commandBuffer->dispatchIndirect(indirectDispatchCommandBuffer,
                                        sizeof(glm::uvec4) * densityCommandIndex);
        commandBuffer->bufferBarrier({surfaceCountBuffer, densityBuffer},
                                     vk::PipelineStageFlagBits::eComputeShader,  //
                                     vk::PipelineStageFlagBits::eComputeShader,  //
                                     vk::AccessFlagBits::eShaderWrite,           //
                                     vk::AccessFlagBits::eShaderRead);
    }

    void computeCellVertexNormal(const rv::CommandBufferHandle& commandBuffer)
    {
        auto& pipeline = computePipelines.at("CellVertexNormal").pipeline;
        commandBuffer->bindDescriptorSet(descSet, pipeline);
        commandBuffer->bindPipeline(pipeline);
        commandBuffer->pushConstants(pipeline, &pushConstants);
        commandBuffer->dispatchIndirect(indirectDispatchCommandBuffer,
                                        sizeof(glm::uvec4) * densityCommandIndex);
        commandBuffer->bufferBarrier(cellVertexNormalBuffer,
                                     vk::PipelineStageFlagBits::eComputeShader,  //
                                     vk::PipelineStageFlagBits::eComputeShader,  //
                                     vk::AccessFlagBits::eShaderWrite,           //
                                     vk::AccessFlagBits::eShaderRead);
    }

    void drawBottomGrid(const rv::CommandBufferHandle& commandBuffer)
    {
        commandBuffer->bindDescriptorSet(descSet, graphicsPipelines["BottomGrid"].pipeline);
        commandBuffer->bindPipeline(graphicsPipelines["BottomGrid"].pipeline);
        commandBuffer->pushConstants(graphicsPipelines["BottomGrid"].pipeline, &pushConstants);
        commandBuffer->setLineWidth(lineWidth);
        commandBuffer->bindVertexBuffer(cubeLineMesh.vertexBuffer);
        commandBuffer->bindIndexBuffer(cubeLineMesh.indexBuffer);
        commandBuffer->drawIndexed(cubeLineMesh.getIndicesCount(), numCells, 0, 0, 0);
    }

    rv::ShaderHandle createShader(const ShaderInfo& shaderInfo) const
    {
        return context.createShader({
            .code = compileOrLoadShader(shaderInfo.fileName, shaderInfo.entryPoint),
            .stage = rv::Compiler::getShaderStage(shaderInfo.fileName),
        });
    }

    void clearBuffers(const rv::CommandBufferHandle& commandBuffer) const
    {
        commandBuffer->beginDebugLabel("ClearBuffers");
        commandBuffer->fillBuffer(topGridValidCellCounts, 0);
        commandBuffer->fillBuffer(surfaceBlockBuffer, 0);
        commandBuffer->fillBuffer(bottomGridParticleCounts, 0);
        commandBuffer->fillBuffer(bottomGridParticleIndices, 0);
        commandBuffer->fillBuffer(surfaceCountBuffer, 0);
        commandBuffer->fillBuffer(surfaceCellBuffer, 0);
        commandBuffer->fillBuffer(surfaceVertexBuffer, 0);
        commandBuffer->fillBuffer(compressedVertexBuffer, 0);
        commandBuffer->fillBuffer(densityBuffer, 0);
        commandBuffer->fillBuffer(indirectDispatchCommandBuffer, 0);
        commandBuffer->fillBuffer(cellVertexNormalBuffer, 0);
        commandBuffer->endDebugLabel();
    }

private:
    // Common
    rv::BufferHandle particleBuffer;

    // Surface cell & particle & vertex
    rv::BufferHandle surfaceCellBuffer;
    rv::BufferHandle surfaceVertexBuffer;
    rv::BufferHandle compressedVertexBuffer;
    rv::BufferHandle densityBuffer;
    rv::BufferHandle surfaceBlockBuffer;

    // Normal
    rv::BufferHandle cellVertexNormalBuffer;

    // MC surface
    rv::BufferHandle surfaceCountBuffer;

    // Indirect command
    rv::BufferHandle indirectDispatchCommandBuffer;

    rv::BufferHandle bottomGridParticleCounts;
    rv::BufferHandle bottomGridParticleIndices;
    rv::BufferHandle topGridValidCellCounts;

    // Images
    vk::Format colorFormat = vk::Format::eB8G8R8A8Unorm;
    vk::Format depthFormat = vk::Format::eD32Sfloat;
    rv::ImageHandle depthImage;
    rv::ImageHandle opaqueColorImage;
    rv::ImageHandle opaquePosImage;
    rv::ImageHandle envIrradianceImage;
    rv::ImageHandle envRadianceImage;

    rv::DescriptorSetHandle descSet;

    std::unordered_map<std::string, ComputePipeline> computePipelines = {
        {"CompressVertex", {{"compute.comp", "main_vertex_compress"}}},
        {"Density", {{"compute.comp", "main_density"}}},
        {"FillTwoGrids", {{"compute.comp", "main_fill_grids"}}},
        {"SurfaceBlock", {{"compute.comp", "main_surface_block"}}},
        {"SurfaceCell", {{"compute.comp", "main_surface_cell"}}},
        {"CellVertexNormal", {{"compute.comp", "main_normal"}}},
    };

    std::unordered_map<std::string, GraphicsPipeline> graphicsPipelines = {
        {"BottomGrid", {{"bottom_grid.vert", "main"}, {"basic.frag", "main"}}},
        {"TopGrid", {{"top_grid.vert", "main"}, {"basic.frag", "main"}}},
        {"Particle", {{"particle.vert", "main"}, {"basic.frag", "main"}}},
        {"SurfaceVertex", {{"surface_vertex.vert", "main"}, {"basic.frag", "main"}}},
        {"Mesh", {{"mesh.vert", "main"}, {"mesh.frag", "main"}}},
    };

    std::unordered_map<std::string, MeshShaderPipeline> meshShaderPipelines = {
        {"SurfacePerBlock",
         {{"", ""},
          {"surface.mesh", "main_subgroup_per_block"},
          {"surface.frag", "main_mesh_shader"}}},
    };

    // Mesh
    rv::Mesh cubeLineMesh;

    rv::Camera camera;

    PushConstants pushConstants;

    int frame = 0;

    uint32_t numParticles = 0;

    // ImGui parameters
    float lineWidth = 2.0f;
    bool showParticles = false;
    bool showSurfaceVertex = false;
    bool showMeshes = true;
    bool showBottomGrid = false;
    bool showTopGrid = false;
    bool showSurface = true;
    bool runPhysics = true;
    bool surfaceDrawLine = false;

    static constexpr int TIME_BUFFER_SIZE = 300;
    float times[TIME_BUFFER_SIZE] = {0};

    std::array<rv::GPUTimerHandle, 2> gpuTimers;

    Scene scene;
    BackgroundPass backgroundPass;
};
