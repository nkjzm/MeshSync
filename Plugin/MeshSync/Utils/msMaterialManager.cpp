#include "pch.h"
#include "msMaterialManager.h"

namespace ms {

MaterialManager::MaterialManager()
{
}

MaterialManager::~MaterialManager()
{
}

void MaterialManager::clear()
{
    m_records.clear();
}

bool MaterialManager::empty() const
{
    return m_records.empty();
}

bool MaterialManager::erase(int id)
{
    return m_records.erase(id) > 0;
}

MaterialPtr MaterialManager::find(int id) const
{
    auto it = m_records.find(id);
    return it != m_records.end() ? it->second.material : nullptr;
}

int MaterialManager::add(MaterialPtr material)
{
    if (!material || material->id == InvalidID)
        return InvalidID;

    auto& rec = lockAndGet(material->id);
    rec.material = material;
    rec.updated = true;

    auto csum = material->checksum();
    if (rec.checksum != csum) {
        rec.checksum = csum;
        rec.dirty = true;
    }
    return 0;
}

std::vector<MaterialPtr> MaterialManager::getAllMaterials()
{
    std::vector<MaterialPtr> ret;
    for (auto& v : m_records)
        ret.push_back(v.second.material);
    return ret;
}

std::vector<MaterialPtr> MaterialManager::getDirtyMaterials()
{
    std::vector<MaterialPtr> ret;
    for (auto& v : m_records)
        if (v.second.dirty)
            ret.push_back(v.second.material);
    return ret;
}

void MaterialManager::makeDirtyAll()
{
    for (auto& p : m_records) {
        p.second.dirty = true;
    }
}

void MaterialManager::clearDirtyFlags()
{
    for (auto& p : m_records) {
        auto& r = p.second;
        r.updated = r.dirty = false;
    }
}

MaterialManager::Record& MaterialManager::lockAndGet(int id)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_records[id];
}

} // namespace ms