#include "pch.h"
#include "MeshSyncClientBlender.h"
#include "msbContext.h"
#include "msbBlenderUtils.h"
using namespace mu;


msbContext::msbContext()
{
    m_settings.scene_settings.handedness = ms::Handedness::LeftZUp;
}

msbContext::~msbContext()
{
    if (m_send_future.valid()) {
        m_send_future.wait();
    }
}

void msbContext::setup(py::object bcontext)
{
    blender::setup(bcontext);
}

msbSettings& msbContext::getSettings() { return m_settings; }
const msbSettings& msbContext::getSettings() const { return m_settings; }
ms::ScenePtr msbContext::getCurrentScene() const { return m_scene; }

template<class T>
std::shared_ptr<T> msbContext::getCacheOrCreate(std::vector<std::shared_ptr<T>>& cache)
{
    std::shared_ptr<T> ret;
    {
        if (cache.empty()) {
            ret.reset(new T());
        }
        else {
            ret = cache.back();
            cache.pop_back();
        }
    }
    ret->clear();
    return ret;
}

ms::TransformPtr msbContext::addTransform(const std::string& path)
{
    auto ret = getCacheOrCreate(m_transform_cache);
    ret->path = path;
    m_scene->transforms.push_back(ret);
    return ret;
}

ms::CameraPtr msbContext::addCamera(const std::string& path)
{
    auto ret = getCacheOrCreate(m_camera_cache);
    ret->path = path;
    m_scene->cameras.push_back(ret);
    return ret;
}

ms::LightPtr msbContext::addLight(const std::string& path)
{
    auto ret = getCacheOrCreate(m_light_cache);
    ret->path = path;
    m_scene->lights.push_back(ret);
    return ret;
}

ms::MeshPtr msbContext::addMesh(const std::string& path)
{
    auto ret = getCacheOrCreate(m_mesh_cache);
    ret->path = path;
    m_scene->meshes.push_back(ret);
    return ret;
}

void msbContext::addDeleted(const std::string& path)
{
    m_deleted.push_back(path);
}



// src_: bpy.Material
ms::MaterialPtr msbContext::addMaterial(py::object src)
{
    auto rna = (BPy_StructRNA*)src.ptr();
    auto& mat = *(Material*)rna->ptr.id.data;

    auto ret = ms::MaterialPtr(new ms::Material());
    ret->name = mat.id.name;
    ret->id = (int)m_scene->materials.size();
    ret->color = float4{ mat.r, mat.g, mat.b, 1.0f };
    m_scene->materials.emplace_back(ret);
    return ret;
}

int msbContext::getMaterialIndex(const Material *mat)
{
    if (mat == nullptr)
        return 0;

    int i = 0;
    for (auto& m : m_scene->materials) {
        if (m->name == mat->id.name)
            return i;
        ++i;
    }
    return 0;
}

// src: bpy.Object
void msbContext::extractTransformData(ms::TransformPtr dst, py::object src)
{
    auto rna = (BPy_StructRNA*)src.ptr();
    auto *obj = (Object*)rna->ptr.id.data;

    extract_local_TRS(obj, dst->position, dst->rotation, dst->scale);
}

// src: bpy.Object
void msbContext::extractCameraData(ms::CameraPtr dst, py::object src)
{
    extractTransformData(dst, src);

    auto rna = (BPy_StructRNA*)src.ptr();
    auto *obj = (Object*)rna->ptr.id.data;
    // todo
}

// src: bpy.Object
void msbContext::extractLightData(ms::LightPtr dst, py::object src)
{
    extractTransformData(dst, src);

    auto rna = (BPy_StructRNA*)src.ptr();
    auto *obj = (Object*)rna->ptr.id.data;
    // todo
}

// src: bpy.Object
void msbContext::extractMeshData(ms::MeshPtr dst, py::object src)
{
    extractTransformData(dst, src);

    auto rna = (BPy_StructRNA*)src.ptr();
    auto obj = (Object*)rna->ptr.id.data;

    // ignore particles
    if (find_modofier(obj, eModifierType_ParticleSystem) || find_modofier(obj, eModifierType_ParticleInstance))
        return;
    // ignore if already added
    if (m_added.find(obj) != m_added.end())
        return;
    m_added.insert(obj);

    auto task = [this, dst, obj]() {
        doExtractMeshData(*dst, obj);
    };
#ifdef msDebug
    // force single-threaded
    task();
#else
    // add async task
    m_extract_tasks.push_back(task);
#endif
}

void msbContext::doExtractMeshData(ms::Mesh& dst, Object *obj)
{
    auto& src = *(Mesh*)obj->data;

    int num_polygons = src.totpoly;
    int num_vertices = src.totvert;
    int num_loops = src.totloop;

    std::vector<int> material_ids(src.totcol);
    for (int mi = 0; mi < src.totcol; ++mi)
        material_ids[mi] = getMaterialIndex(src.mat[mi]);
    if (material_ids.empty())
        material_ids.push_back(0);

    // vertices
    dst.points.resize_discard(num_vertices);
    for (int vi = 0; vi < num_vertices; ++vi) {
        dst.points[vi] = (float3&)src.mvert[vi].co;
    }

    // faces
    dst.indices.reserve(num_loops);
    dst.counts.resize_discard(num_polygons);
    dst.material_ids.resize_discard(num_polygons);
    {
        int ti = 0;
        for (int pi = 0; pi < num_polygons; ++pi) {
            int material_index = src.mpoly[pi].mat_nr;
            int count = src.mpoly[pi].totloop;
            dst.counts[pi] = count;
            dst.material_ids[pi] = material_ids[material_index];
            dst.indices.resize(dst.indices.size() + count);

            auto *loops = src.mloop + src.mpoly[pi].loopstart;
            for (int li = 0; li < count; ++li) {
                dst.indices[ti++] = loops[li].v;
            }
        }
    }

    // normals
    if (m_settings.sync_normals == msbNormalSyncMode::PerVertex) {
        // per-vertex
        dst.normals.resize_discard(num_vertices);
        for (int vi = 0; vi < num_vertices; ++vi) {
            dst.normals[vi] = to_float3(src.mvert[vi].no);
        }
    }
    else if (m_settings.sync_normals == msbNormalSyncMode::PerIndex) {
        // per-index
        auto normals = (float3*)blender::CustomData_get(src.ldata, CD_NORMAL);
        if (normals == nullptr) {
            dst.normals.resize_zeroclear(num_loops);
        }
        else {
            dst.normals.resize_discard(num_loops);
            for (int li = 0; li < num_loops; ++li) {
                dst.normals[li] = normals[li];
            }
        }
    }

    // uvs
    if (m_settings.sync_uvs && blender::CustomData_number_of_layers(src.ldata, CD_MLOOPUV) > 0) {
        auto data = (const float2*)blender::CustomData_get(src.ldata, CD_MLOOPUV);
        if (data != nullptr) {
            dst.uv0.resize_discard(num_loops);
            for (int li = 0; li < num_loops; ++li) {
                dst.uv0[li] = data[li];
            }
        }
    }

    // colors
    if (m_settings.sync_colors && blender::CustomData_number_of_layers(src.ldata, CD_MLOOPCOL) > 0) {
        auto data = (const MLoopCol*)blender::CustomData_get(src.ldata, CD_MLOOPCOL);
        if (data != nullptr) {
            dst.colors.resize_discard(num_loops);
            for (int li = 0; li < num_loops; ++li) {
                dst.colors[li] = to_float4(data[li]);
            }
        }
    }

    // bones
    if (m_settings.sync_bones) {
        auto *arm_mod = (const ArmatureModifierData*)find_modofier(obj, eModifierType_Armature);
        if (arm_mod) {
            auto *arm_obj = arm_mod->object;
            int group_index = 0;
            each_vertex_groups(obj, [&](const bDeformGroup *g) {
                auto bone = find_bone(arm_obj, g->name);
                if (bone) {
                    auto trans = findOrAddBone(arm_obj, bone);
                    auto b = dst.addBone(trans->path);
                    b->bindpose = extract_bindpose(arm_obj, bone);
                    b->weights.resize_zeroclear(num_vertices);

                    for (int vi = 0; vi < num_vertices; ++vi) {
                        int num_weights = src.dvert[vi].totweight;
                        auto& dvert = src.dvert[vi];
                        for (int wi = 0; wi < num_weights; ++wi) {
                            if (dvert.dw[wi].def_nr == group_index) {
                                b->weights[vi] = dvert.dw[wi].weight;
                            }
                        }
                    }
                }
                ++group_index;
            });
        }
    }

    // blend shapes
    if (m_settings.sync_blendshapes && src.key) {
        RawVector<float3> basis;
        int bi = 0;
        each_keys(&src, [&](const KeyBlock *kb) {
            if (bi == 0) { // Basis
                basis.resize_discard(kb->totelem);
                memcpy(basis.data(), kb->data, basis.size() * sizeof(float3));
            }
            else {
                auto bsd = dst.addBlendShape(kb->name);
                bsd->weight = kb->curval * 100.0f;
                bsd->frames.resize(1);
                auto& frame = bsd->frames.back();
                frame.weight = 100.0f;
                frame.points.resize_discard(kb->totelem);
                memcpy(frame.points.data(), kb->data, basis.size() * sizeof(float3));

                size_t len = frame.points.size();
                for (size_t i = 0; i < len; ++i) {
                    frame.points[i] -= basis[i];
                }
            }
            ++bi;
        });
    }
}

ms::TransformPtr msbContext::findOrAddBone(const Object *armature, const Bone *bone)
{
    std::unique_lock<std::mutex> lock(m_extract_mutex);
    auto& ret = m_bones[bone];
    if (ret) { return ret; }

    auto *pose = m_settings.sync_poses ? find_pose(armature, bone->name) : nullptr;
    ret = addTransform(get_path(armature) + get_path(bone));
    if(pose)
        extract_local_TRS(armature, pose, ret->position, ret->rotation, ret->scale);
    else
        extract_local_TRS(armature, bone, ret->position, ret->rotation, ret->scale);
    return ret;
}


bool msbContext::isSending() const
{
    return m_send_future.valid() && m_send_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
}

void msbContext::send()
{
    if (isSending())
    {
        // previous request is not completed yet. skip
        m_extract_tasks.clear();
        return;
    }

    // get vertex data in parallel
    parallel_for_each(m_extract_tasks.begin(), m_extract_tasks.end(), [](task_t& task) {
        task();
    });
    m_extract_tasks.clear();

    // todo
    //for (auto& v : m_scene.cameras)
    //    v->transform.rotation = rotateX(-90.0f * Deg2Rad) * swap_yz(v->transform.rotation);
    //for (auto& v : m_scene.lights)
    //    v->transform.rotation = rotateX(-90.0f * Deg2Rad) * swap_yz(v->transform.rotation);


    // return objects to cache
    for (auto& v : m_message.scene.transforms) { m_transform_cache.push_back(v); }
    for (auto& v : m_message.scene.cameras) { m_camera_cache.push_back(v); }
    for (auto& v : m_message.scene.lights) { m_light_cache.push_back(v); }
    for (auto& v : m_message.scene.meshes) { m_mesh_cache.push_back(v); }

    // setup send data
    std::swap(m_scene->meshes, m_mesh_send);
    m_message.scene = *m_scene;
    m_scene->clear();
    m_bones.clear();
    m_added.clear();

    // kick async send
    m_send_future = std::async(std::launch::async, [this]() {
        ms::Client client(m_settings.client_settings);

        // notify scene begin
        {
            ms::FenceMessage fence;
            fence.type = ms::FenceMessage::FenceType::SceneBegin;
            client.send(fence);
        }

        // send transform, camera, etc

        auto scene_settings = m_settings.scene_settings;
        float scale_factor = 1.0f / m_settings.scene_settings.scale_factor;
        scene_settings.scale_factor = 1.0f;
        scene_settings.handedness = ms::Handedness::Left;

        m_message.scene.settings = scene_settings;

        auto& scene = m_message.scene;
        for (auto& obj : scene.transforms) { obj->convertHandedness(false, true); }
        for (auto& obj : scene.cameras) { obj->convertHandedness(false, true); }
        for (auto& obj : scene.lights) { obj->convertHandedness(false, true); }
        if (scale_factor != 1.0f) {
            for (auto& obj : scene.transforms) { obj->applyScaleFactor(scale_factor); }
            for (auto& obj : scene.cameras) { obj->applyScaleFactor(scale_factor); }
            for (auto& obj : scene.lights) { obj->applyScaleFactor(scale_factor); }
        }
        client.send(m_message);

        // send deleted
        if (!m_deleted.empty()) {
            ms::DeleteMessage del;
            for (auto& path : m_deleted) {
                del.targets.push_back({path, 0});
            }
            client.send(del);
        }
        m_deleted.clear();

        // convert and send meshes
        parallel_for_each(m_mesh_send.begin(), m_mesh_send.end(), [&](ms::MeshPtr& pmesh) {
            auto& mesh = *pmesh;
            mesh.setupFlags();
            mesh.flags.apply_trs = true;
            mesh.flags.has_refine_settings = true;
            if (!mesh.flags.has_normals)
                mesh.refine_settings.flags.gen_normals = true;
            if (!mesh.flags.has_tangents)
                mesh.refine_settings.flags.gen_tangents = true;
            if (scale_factor != 1.0f)
                mesh.applyScaleFactor(scale_factor);
            mesh.convertHandedness(false, true);
        });
        for(auto& pmesh : m_mesh_send) {
            ms::SetMessage set;
            set.scene.settings = scene_settings;
            set.scene.meshes = { pmesh };
            client.send(set);
        }
        m_mesh_send.clear();

        // notify scene end
        {
            ms::FenceMessage fence;
            fence.type = ms::FenceMessage::FenceType::SceneEnd;
            client.send(fence);
        }
    });
}
