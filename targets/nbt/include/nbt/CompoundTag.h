#pragma once
#include <unordered_map>
#include <memory>
#include <vector>
#include <string>

#include "ByteArrayTag.h"
#include "ByteTag.h"
#include "DoubleTag.h"
#include "FloatTag.h"
#include "IntArrayTag.h"
#include "IntTag.h"
#include "ListTag.h"
#include "LongTag.h"
#include "ShortTag.h"
#include "StringTag.h"
#include "Tag.h"

class CompoundTag : public Tag {
private:
    std::unordered_map<std::string, std::unique_ptr<Tag>> tags;

public:
    CompoundTag() : Tag("") {}
    CompoundTag(const std::string& name) : Tag(name) {}

    void write(DataOutput* dos) override {
        for (auto& [key, value] : tags) {
            Tag::writeNamedTag(value.get(), dos);
        }
        dos->writeByte(Tag::TAG_End);
    }

    void load(DataInput* dis, int tagDepth) override {
        if (tagDepth > MAX_DEPTH) {
#ifndef _CONTENT_PACKAGE
            printf("Tried to read NBT tag with too high complexity, depth > %d\n", MAX_DEPTH);
#endif
            return;
        }
        tags.clear();
        for (;;) {
            Tag* rawTag = Tag::readNamedTag(dis, tagDepth + 1);
            if (rawTag == nullptr) break; // Previene crash en EOF/corrupción
            if (rawTag->getId() == Tag::TAG_End) {
                delete rawTag; // Libera el EndTag sin meterlo al mapa
                break;
            }
            std::unique_ptr<Tag> tag(rawTag);
            tags[tag->getName()] = std::move(tag);
        }
    }

    std::vector<Tag*> getAllTags() {
        std::vector<Tag*> ret;
        ret.reserve(tags.size());
        for (auto& [key, value] : tags) {
            ret.push_back(value.get());
        }
        return ret;
    }

    uint8_t getId() override { return TAG_Compound; }

    void put(const std::string& name, Tag* tag) {
        if (!tag) return;
        tag->setName(name);
        tags[name] = std::unique_ptr<Tag>(tag);
    }

    void putByte(const std::string& name, uint8_t value) {
        tags[name] = std::make_unique<ByteTag>(name, value);
    }

    void putShort(const std::string& name, short value) {
        tags[name] = std::make_unique<ShortTag>(name, value);
    }

    void putInt(const std::string& name, int value) {
        tags[name] = std::make_unique<IntTag>(name, value);
    }

    void putLong(const std::string& name, int64_t value) {
        tags[name] = std::make_unique<LongTag>(name, value);
    }

    void putFloat(const std::string& name, float value) {
        tags[name] = std::make_unique<FloatTag>(name, value);
    }

    void putDouble(const std::string& name, double value) {
        tags[name] = std::make_unique<DoubleTag>(name, value);
    }

    void putString(const std::string& name, const std::string& value) {
        tags[name] = std::make_unique<StringTag>(name, value);
    }

    void putByteArray(const std::string& name, const std::vector<uint8_t>& value) {
        tags[name] = std::make_unique<ByteArrayTag>(name, const_cast<std::vector<uint8_t>&>(value));
    }

    void putIntArray(const std::string& name, const std::vector<int>& value) {
        tags[name] = std::make_unique<IntArrayTag>(name, const_cast<std::vector<int>&>(value));
    }

    void putCompound(const std::string& name, CompoundTag* value) {
        if (!value) return;
        value->setName(name);
        tags[name] = std::unique_ptr<Tag>(value);
    }

    void putBoolean(const std::string& name, bool val) {
        putByte(name, val ? (uint8_t)1 : 0);
    }

    Tag* get(const std::string& name) {
        auto it = tags.find(name);
        return (it != tags.end()) ? it->second.get() : nullptr;
    }

    bool contains(const std::string& name) {
        return tags.find(name) != tags.end();
    }

    uint8_t getByte(const std::string& name) {
        auto it = tags.find(name);
        if (it == tags.end() || it->second->getId() != TAG_Byte) return 0;
        return static_cast<ByteTag*>(it->second.get())->data;
    }

    short getShort(const std::string& name) {
        auto it = tags.find(name);
        if (it == tags.end() || it->second->getId() != TAG_Short) return 0;
        return static_cast<ShortTag*>(it->second.get())->data;
    }

    int getInt(const std::string& name) {
        auto it = tags.find(name);
        if (it == tags.end() || it->second->getId() != TAG_Int) return 0;
        return static_cast<IntTag*>(it->second.get())->data;
    }

    int64_t getLong(const std::string& name) {
        auto it = tags.find(name);
        if (it == tags.end() || it->second->getId() != TAG_Long) return 0;
        return static_cast<LongTag*>(it->second.get())->data;
    }

    float getFloat(const std::string& name) {
        auto it = tags.find(name);
        if (it == tags.end() || it->second->getId() != TAG_Float) return 0.0f;
        return static_cast<FloatTag*>(it->second.get())->data;
    }

    double getDouble(const std::string& name) {
        auto it = tags.find(name);
        if (it == tags.end() || it->second->getId() != TAG_Double) return 0.0;
        return static_cast<DoubleTag*>(it->second.get())->data;
    }

    // RETORNO POR REFERENCIA CONSTANTE: CERO COPIAS
    const std::string& getString(const std::string& name) {
        static const std::string emptyString = "";
        auto it = tags.find(name);
        if (it == tags.end() || it->second->getId() != TAG_String) return emptyString;
        return static_cast<StringTag*>(it->second.get())->data;
    }

    // RETORNO POR REFERENCIA CONSTANTE: CERO COPIAS DE BLOQUES
    const std::vector<uint8_t>& getByteArray(const std::string& name) {
        static const std::vector<uint8_t> emptyVec;
        auto it = tags.find(name);
        if (it == tags.end() || it->second->getId() != TAG_Byte_Array) return emptyVec;
        return static_cast<ByteArrayTag*>(it->second.get())->data;
    }

    const std::vector<int>& getIntArray(const std::string& name) {
        static const std::vector<int> emptyVec;
        auto it = tags.find(name);
        if (it == tags.end() || it->second->getId() != TAG_Int_Array) return emptyVec;
        return static_cast<IntArrayTag*>(it->second.get())->data;
    }

    CompoundTag* getCompound(const std::string& name) {
        auto it = tags.find(name);
        if (it == tags.end()) {
            auto [it2, inserted] = tags.emplace(name, std::make_unique<CompoundTag>(name));
            return static_cast<CompoundTag*>(it2->second.get());
        }
        if (it->second->getId() != TAG_Compound) return nullptr;
        return static_cast<CompoundTag*>(it->second.get());
    }

    ListTag<Tag>* getList(const std::string& name) {
        auto it = tags.find(name);
        if (it == tags.end()) {
            auto [it2, inserted] = tags.emplace(name, std::make_unique<ListTag<Tag>>(name));
            return static_cast<ListTag<Tag>*>(it2->second.get());
        }
        if (it->second->getId() != TAG_List) return nullptr;
        return static_cast<ListTag<Tag>*>(it->second.get());
    }

    bool getBoolean(const std::string& string) { return getByte(string) != 0; }

    void remove(const std::string& name) { tags.erase(name); }

    // Seguro para multihilo (reemplaza snprintf con búfer estático)
    std::string toString() override {
        return std::to_string(tags.size()) + " entries";
    }

    bool isEmpty() { return tags.empty(); }

    virtual ~CompoundTag() = default;

    Tag* copy() override {
        CompoundTag* tag = new CompoundTag(getName());
        for (auto& [key, value] : tags) {
            tag->put(key, value->copy());
        }
        return tag;
    }

    bool equals(Tag* obj) override {
        if (Tag::equals(obj)) {
            CompoundTag* o = static_cast<CompoundTag*>(obj);
            if (tags.size() == o->tags.size()) {
                for (auto& [key, value] : tags) {
                    auto itFind = o->tags.find(key);
                    if (itFind == o->tags.end() || !value->equals(itFind->second.get())) {
                        return false;
                    }
                }
                return true;
            }
        }
        return false;
    }
};