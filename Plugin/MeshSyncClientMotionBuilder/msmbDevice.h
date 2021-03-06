#pragma once

#include "MeshSync/MeshSync.h"

class msmbDevice : public FBDevice
{
    FBDeviceDeclare(msmbDevice, FBDevice);
public:
    bool FBCreate() override;
    void FBDestroy() override;

    bool DeviceOperation(kDeviceOperations pOperation) override;
    void DeviceTransportNotify(kTransportMode pMode, FBTime pTime, FBTime pSystem) override;

    bool sendScene(bool force_all);
    bool exportMaterials();
    bool sendAnimations();

private:
    void onSceneChange(HIRegister pCaller, HKEventBase pEvent);
    void onDataUpdate(HIRegister pCaller, HKEventBase pEvent);
    void onRender(HIRegister pCaller, HKEventBase pEvent);
    void onSynchronization(HIRegister pCaller, HKEventBase pEvent);

    void update();
    bool isSending() const;
    void waitAsyncSend();
    void kickAsyncSend();

    bool exportObject(FBModel* src, bool force);
    void extractTransform(ms::Transform& dst, FBModel* src);
    void extractCamera(ms::Camera& dst, FBCamera* src);
    void extractLight(ms::Light& dst, FBLight* src);
    void extractMeshSimple(ms::Mesh& dst, FBModel* src);
    void extractMesh(ms::Mesh& dst, FBModel* src);
    void doExtractMesh(ms::Mesh& dst, FBModel* src);

    int exportTexture(FBTexture* src, FBMaterialTextureType type);
    bool exportMaterial(FBMaterial* src);

    bool exportAnimations();
    bool exportAnimation(FBModel* src, bool force);
    void extractTransformAnimation(ms::Animation& dst, FBModel* src);
    void extractCameraAnimation(ms::Animation& dst, FBModel* src);
    void extractLightAnimation(ms::Animation& dst, FBModel* src);
    void extractMeshAnimation(ms::Animation& dst, FBModel* src);

    struct NodeRecord
    {
        std::string name;
        std::string path;
        int index = 0;
        ms::Transform *dst = nullptr;
        bool exist = false;
    };
    using NodeRecords = std::map<FBModel*, NodeRecord>;
    using ExtractTasks = std::vector<std::function<void()>>;

    struct TextureRecord : public mu::noncopyable
    {
        int id = -1;
        ms::Texture *dst = nullptr;
    };
    using TextureRecords = std::map<FBTexture*, TextureRecord>;

    struct MaterialRecord : public mu::noncopyable
    {
        int id = -1;
        ms::Material *dst = nullptr;
    };
    using MaterialRecords = std::map<FBMaterial*, MaterialRecord>;

    struct AnimationRecord : public mu::noncopyable
    {
        using extractor_t = void (msmbDevice::*)(ms::Animation& dst, FBModel *src);

        ms::Animation *dst = nullptr;
        FBModel *src = nullptr;
        extractor_t extractor = nullptr;

        void operator()(msmbDevice *_this);
    };
    using AnimationRecords = std::map<FBModel*, AnimationRecord>;

private:
    bool m_data_updated = false;
    bool m_dirty_meshes = true;
    bool m_dirty_textures = true;
    bool m_pending = false;

    float m_anim_time = 0.0f;
    int m_texture_id_seed = 0;
    int m_node_index_seed = 0;

    NodeRecords m_node_records;
    TextureRecords m_texture_records;
    MaterialRecords m_material_records;
    ExtractTasks m_extract_tasks;
    AnimationRecords m_anim_records;

    std::vector<ms::TransformPtr>       m_objects;
    std::vector<ms::MeshPtr>            m_meshes;
    std::vector<ms::TexturePtr>         m_textures;
    std::vector<ms::MaterialPtr>        m_materials;
    std::vector<ms::AnimationClipPtr>   m_animations;
    std::vector<ms::ConstraintPtr>      m_constraints;
    std::vector<std::string>            m_deleted;
    std::future<void>                   m_future_send;

public:
    ms::ClientSettings client_settings;
    int  timeout_ms = 5000;
    float scale_factor = 100.0f;
    float time_scale = 1.0f;
    float samples_per_second = 3.0f;

    bool auto_sync = false;
    bool sync_cameras = true;
    bool sync_lights = true;
    bool sync_bones = true;
    bool sync_meshes = true;
    bool sync_textures = true;
    bool sync_materials = true;
    bool parallel_extraction = true;
};
