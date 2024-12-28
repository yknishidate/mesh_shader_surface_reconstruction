#pragma once
#include <reactive/reactive.hpp>

namespace fs = std::filesystem;

// Constants
const std::string INCLUDE_PREFIX = "#include \"";
const std::string INCLUDE_SUFFIX = "\"";
const std::string SHADER_SPV_DIR = SHADER_DIR + "spv/";

// Extract all included files from shader code
inline std::vector<std::string> extractIncludedFiles(const std::string& code)
{
    std::vector<std::string> includes;
    std::string::size_type startPos = 0;

    while (true) {
        auto includeStartPos = code.find(INCLUDE_PREFIX, startPos);
        if (includeStartPos == std::string::npos)
            break;

        auto includeEndPos = code.find(INCLUDE_SUFFIX, includeStartPos + INCLUDE_PREFIX.length());
        if (includeEndPos == std::string::npos)
            break;

        std::string include
            = code.substr(includeStartPos + INCLUDE_PREFIX.length(),
                          includeEndPos - (includeStartPos + INCLUDE_PREFIX.length()));
        includes.push_back(include);
        startPos = includeEndPos + INCLUDE_SUFFIX.length();
    }

    return includes;
}

// Generate SPV file path from shader file name and entry point
inline fs::path generateSpvFilePath(const std::string& shaderFileName,
                                    const std::string& entryPoint = "main")
{
    auto glslFile = fs::path{SHADER_DIR + shaderFileName};
    return fs::path{SHADER_SPV_DIR + glslFile.stem().string() + "_" + entryPoint
                    + glslFile.extension().string() + ".spv"};
}

// Get the latest write time considering all included files
inline fs::file_time_type getLatestWriteTime(const std::string& fileName)
{
    fs::path filePath = SHADER_DIR + fileName;
    if (!fs::exists(filePath)) {
        throw std::runtime_error("File does not exist: " + filePath.string());
    }
    auto writeTime = fs::last_write_time(filePath);

    std::string code = rv::File::readFile(filePath.string());
    for (const auto& include : extractIncludedFiles(code)) {
        auto includeWriteTime = getLatestWriteTime(include);
        if (includeWriteTime > writeTime) {
            writeTime = includeWriteTime;
        }
    }

    return writeTime;
}

// Determine if the shader needs recompilation
inline bool isRecompilationNeeded(const std::string& shaderFileName,
                                  const std::string& entryPoint = "main")
{
    if (shaderFileName.empty()) {
        return false;
    }

    fs::create_directory(SHADER_SPV_DIR);
    auto spvFile = generateSpvFilePath(shaderFileName, entryPoint);
    auto glslWriteTime = getLatestWriteTime(shaderFileName);

    return !fs::exists(spvFile) || glslWriteTime > fs::last_write_time(spvFile);
}

// Compile or read the shader based on its modification time
inline std::vector<uint32_t> compileOrLoadShader(const std::string& shaderFileName,
                                                 const std::string& entryPoint = "main")
{
    auto spvFile = generateSpvFilePath(shaderFileName, entryPoint);
    std::vector<uint32_t> spvCode;

    if (isRecompilationNeeded(shaderFileName, entryPoint)) {
        spdlog::info("Compile shader: {}", spvFile.string());
        spvCode = rv::Compiler::compileToSPV(SHADER_DIR + shaderFileName, {{entryPoint, "main"}});
        rv::File::writeBinary(spvFile.string(), spvCode);
    } else {
        rv::File::readBinary(spvFile.string(), spvCode);
    }

    return spvCode;
}

inline uint32_t divRoundUp(uint32_t num, uint32_t den)
{
    return (num + den - 1) / den;
}

class BackgroundPass {
public:
    struct Constants
    {
        glm::mat4 invView{1};
        glm::mat4 invProj{1};
        glm::ivec2 resolution{0, 0};
    };

    void init(const rv::Context& context, rv::ImageHandle envRadianceImage)
    {
        rv::ShaderHandle vertShader = context.createShader({
            .code = compileOrLoadShader("full_screen.vert"),
            .stage = vk::ShaderStageFlagBits::eVertex,
        });

        rv::ShaderHandle fragShader = context.createShader({
            .code = compileOrLoadShader("background.frag"),
            .stage = vk::ShaderStageFlagBits::eFragment,
        });

        descSet = context.createDescriptorSet({
            .shaders = {vertShader, fragShader},
            .images = {{"envRadianceImage", envRadianceImage}},
        });
        descSet->update();

        pipeline = context.createGraphicsPipeline({
            .descSetLayout = descSet->getLayout(),
            .pushSize = sizeof(Constants),
            .vertexShader = vertShader,
            .fragmentShader = fragShader,
            .depthFormat = vk::Format::eUndefined,
        });
    }

    void run(const rv::CommandBufferHandle& commandBuffer,
             const rv::ImageHandle& outputImage,
             const glm::mat4& invView,
             const glm::mat4& invProj,
             uint32_t width,
             uint32_t height)
    {
        constants.invView = invView;
        constants.invProj = invProj;
        constants.resolution = {width, height};

        commandBuffer->beginRendering(outputImage, nullptr, {0, 0}, {width, height});
        commandBuffer->setViewport(width, height);
        commandBuffer->setScissor(width, height);
        commandBuffer->bindDescriptorSet(descSet, pipeline);
        commandBuffer->bindPipeline(pipeline);
        commandBuffer->pushConstants(pipeline, &constants);
        commandBuffer->draw(3, 1, 0, 0);
        commandBuffer->endRendering();
    }

private:
    rv::DescriptorSetHandle descSet;
    rv::GraphicsPipelineHandle pipeline;
    Constants constants;
};
