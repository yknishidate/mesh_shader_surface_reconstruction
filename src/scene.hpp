#pragma once
#include <Alembic/AbcCoreFactory/All.h>
#include <Alembic/AbcGeom/All.h>
#include <glm/glm.hpp>
#include <vector>

using namespace Alembic::Abc;
using namespace Alembic::AbcGeom;

class Scene {
public:
    void load(const std::string& filepath)
    {
        if (!std::filesystem::exists(filepath)) {
            std::cout << "ERROR: file not found: " << filepath << std::endl;
            return;
        }
        Alembic::AbcCoreFactory::IFactory factory;
        Alembic::AbcCoreFactory::IFactory::CoreType coreType;

        // アーカイブを開く
        IArchive archive = factory.getArchive(filepath, coreType);

        // ルートオブジェクトから開始
        visitObject(archive.getTop());
    }

    // トランスフォームを取得する関数
    glm::mat4 getTransform(const IObject& obj) const
    {
        if (IXform::matches(obj.getMetaData())) {
            IXform xform(obj, kWrapExisting);
            XformSample sample;
            xform.getSchema().get(sample);

            const auto& alembicMatrix = sample.getMatrix();
            glm::mat4 transform = glm::mat4(1.0f);
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    transform[i][j] = static_cast<float>(alembicMatrix[i][j]);
                }
            }

            std::cout << glm::to_string(transform) << "\n";
            return transform;
        }
        std::cout << "ERROR: parent is not IXform\n";
        return glm::mat4{1.0f};  // 単位行列を返す
    }

    void processMesh(const IPolyMesh& meshObj)
    {
        if (meshObj.getFullName().find("Effector") == std::string::npos) {
            return;
        }

        const IPolyMeshSchema& schema = meshObj.getSchema();

        // サンプル数を取得
        size_t numSamples = schema.getNumSamples();
        if (numSamples == 0)
            return;

        // 最初のサンプルのみを処理（複数サンプルの処理が必要な場合はループを使用）
        IPolyMeshSchema::Sample sample;
        schema.get(sample, ISampleSelector(static_cast<index_t>(0)));

        // 頂点の位置を取得
        P3fArraySamplePtr positions = sample.getPositions();
        // 法線を取得（存在する場合）
        IN3fGeomParam normalsParam = schema.getNormalsParam();
        IN3fGeomParam::Sample normalsSample;
        if (normalsParam.valid()) {
            normalsSample = normalsParam.getIndexedValue();
        } else {
            std::cout << "This mesh does not have normals\n";
        }

        // Mesh構造体を作成
        glm::mat4 transform = getTransform(meshObj.getParent());
        std::vector<Vertex> outVertices;
        std::vector<uint32_t> outIndices;

        if (positions) {
            for (size_t i = 0; i < positions->size(); ++i) {
                Vertex vertex;
                const Imath::V3f& pos = (*positions)[i];
                glm::vec4 localPos = glm::vec4(pos.x, pos.y, pos.z, 1.0f);
                vertex.pos = glm::vec3(transform * localPos);
                outVertices.push_back(vertex);
            }
        }

        // インデックスを取得
        if (Int32ArraySamplePtr indices = sample.getFaceIndices()) {
            for (int i = 0; i < indices->size(); i++) {
                const int32_t* data = static_cast<const int32_t*>(indices->getData());
                outIndices.push_back(static_cast<uint32_t>(data[i]));
            }
        }

        // Meshを追加
        Mesh mesh;
        if (normalsParam.valid()) {
            // TODO: support indexed normal
            assert(!normalsSample.isIndexed());
            for (uint32_t i = 0; i < outIndices.size(); i++) {
                const Imath::V3f& normal = normalsSample.getVals()->get()[i];
                Vertex vertex{};
                vertex.pos = outVertices[outIndices[i]].pos;
                vertex.normal = glm::vec3(normal.x, normal.y, normal.z);
                mesh.vertices.push_back(vertex);
                mesh.indices.push_back(i);
            }
        } else {
            mesh.vertices = outVertices;
            mesh.indices = outIndices;
        }

        meshes.push_back(mesh);
    }

    void processPoints(const IPoints& points)
    {
        // IPointsSchemaを取得
        IPointsSchema schema = points.getSchema();

        // サンプル数を取得
        frameCount = static_cast<int>(schema.getNumSamples());
        if (frameCount == 0)
            return;
        std::cout << "  frames: " << frameCount << std::endl;
        particleCounts.resize(frameCount);
        particleOffsets.resize(frameCount);

        glm::mat4 transform = getTransform(points.getParent());

        // 各サンプルを処理
        for (int i = 0; i < frameCount; ++i) {
            IPointsSchema::Sample sample;
            schema.get(sample, ISampleSelector((index_t)i));

            // パーティクルの位置を取得
            P3fArraySamplePtr positions = sample.getPositions();
            particleCounts[i] = static_cast<uint32_t>(positions->size());
            particleOffsets[i] = i == 0 ? 0 : particleOffsets[i - 1] + particleCounts[i - 1];
            std::cout << "  particles: " << positions->size() << std::endl;
            if (positions) {
                for (size_t j = 0; j < particleCounts[i]; ++j) {
                    const Imath::V3f& pos = (*positions)[j];
                    particles.push_back(transform * glm::vec4(pos.x, pos.y, pos.z, 0.0f));
                }
            }
        }
        maxParticleCount = std::ranges::max(particleCounts);
    }

    void visitObject(const IObject& obj, size_t level = 0)
    {
        // オブジェクトの名前とタイプを表示
        std::cout << std::string(level * 4, ' ') << obj.getFullName() << " ("
                  << obj.getMetaData().get("schema") << ")" << std::endl;

        if (IPolyMesh::matches(obj.getMetaData())) {
            processMesh(IPolyMesh(obj));
        } else if (IPoints::matches(obj.getMetaData())) {
            processPoints(IPoints(obj));
        }

        // 子オブジェクトを処理
        for (size_t i = 0; i < obj.getNumChildren(); ++i) {
            visitObject(obj.getChild(i), level + 1);
        }
    }

    void update() { frame = (frame + 1) % frameCount; }

    uint32_t getParticleCount() const { return particleCounts[frame]; }

    uint32_t getSize() const { return sizeof(glm::vec4) * particleCounts[frame]; }

    const glm::vec4* getData() const { return particles.data() + particleOffsets[frame]; }

    int frame = 0;
    int frameCount = 0;
    uint32_t maxParticleCount = 0;
    std::vector<uint32_t> particleCounts;
    std::vector<uint32_t> particleOffsets;
    std::vector<glm::vec4> particles;

    struct Vertex
    {
        glm::vec3 pos;
        glm::vec3 normal;
    };

    struct Mesh
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        rv::BufferHandle vertexBuffer;
        rv::BufferHandle indexBuffer;

        void allocate(const rv::Context& context)
        {
            vertexBuffer = context.createBuffer({
                .usage = rv::BufferUsage::Vertex,
                .memory = rv::MemoryUsage::Device,
                .size = sizeof(Vertex) * vertices.size(),
            });

            indexBuffer = context.createBuffer({
                .usage = rv::BufferUsage::Index,
                .memory = rv::MemoryUsage::Device,
                .size = sizeof(uint32_t) * indices.size(),
            });

            context.oneTimeSubmit([&](rv::CommandBufferHandle commandBuffer) {
                commandBuffer->copyBuffer(vertexBuffer, vertices.data());
                commandBuffer->copyBuffer(indexBuffer, indices.data());
            });
        }
    };

    std::vector<Mesh> meshes;
};
