#include "nbt/Tag.h"

#include <cstdio>

#include "java/InputOutputStream/DataInput.h"
#include "java/InputOutputStream/DataOutput.h"
#include "nbt/ByteArrayTag.h"
#include "nbt/ByteTag.h"
#include "nbt/CompoundTag.h"
#include "nbt/DoubleTag.h"
#include "nbt/EndTag.h"
#include "nbt/FloatTag.h"
#include "nbt/IntArrayTag.h"
#include "nbt/IntTag.h"
#include "nbt/ListTag.h"
#include "nbt/LongTag.h"
#include "nbt/ShortTag.h"
#include "nbt/StringTag.h"

Tag::Tag(const std::string& name) : name(name) {}

bool Tag::equals(Tag* obj) {
    if (obj == nullptr) return false;
    Tag* o = (Tag*)obj;
    if (getId() != o->getId()) return false;
    if (name != o->name) return false;
    return true;
}

void Tag::print(std::ostream& out) { out << ""; }

void Tag::print(char* prefix, std::ostream& out) {
    const std::string& n = getName();
    out << prefix << getTagName(getId());
    if (!n.empty()) {
        out << "(\"" << n << "\")";
    }
    out << ": " << toString() << std::endl;
}

// CERO COPIAS AL PEDIR EL NOMBRE
const std::string& Tag::getName() const { return name; }

Tag* Tag::setName(const std::string& name) {
    this->name = name;
    return this;
}

Tag* Tag::readNamedTag(DataInput* dis) { return readNamedTag(dis, 0); }

Tag* Tag::readNamedTag(DataInput* dis, int tagDepth) {
    if (!dis) return nullptr;

    uint8_t type = dis->readByte();
    if (type == TAG_End || type == 255) {
        return new EndTag();
    }

    std::string name = dis->readUTF();
    Tag* tag = newTag(type, name);
    if (!tag) {
        fprintf(stderr, "Unknown NBT Tag type: %u\n", type);
        return nullptr;
    }

    tag->load(dis, tagDepth);
    return tag;
}

void Tag::writeNamedTag(Tag* tag, DataOutput* dos) {
    if (!tag || !dos) return;
    dos->writeByte(tag->getId());
    if (tag->getId() == Tag::TAG_End) return;

    dos->writeUTF(tag->getName());
    tag->write(dos);
}

Tag* Tag::newTag(uint8_t type, const std::string& name) {
    switch (type) {
        case TAG_End:        return new EndTag(name);
        case TAG_Byte:       return new ByteTag(name);
        case TAG_Short:      return new ShortTag(name);
        case TAG_Int:        return new IntTag(name);
        case TAG_Long:       return new LongTag(name);
        case TAG_Float:      return new FloatTag(name);
        case TAG_Double:     return new DoubleTag(name);
        case TAG_Byte_Array: return new ByteArrayTag(name);
        case TAG_Int_Array:  return new IntArrayTag(name);
        case TAG_String:     return new StringTag(name);
        case TAG_List:       return new ListTag<Tag>(name);
        case TAG_Compound:   return new CompoundTag(name);
        default:             return nullptr;
    }
}

const char* Tag::getTagName(uint8_t type) {
    switch (type) {
        case TAG_End:        return "TAG_End";
        case TAG_Byte:       return "TAG_Byte";
        case TAG_Short:      return "TAG_Short";
        case TAG_Int:        return "TAG_Int";
        case TAG_Long:       return "TAG_Long";
        case TAG_Float:      return "TAG_Float";
        case TAG_Double:     return "TAG_Double";
        case TAG_Byte_Array: return "TAG_Byte_Array";
        case TAG_Int_Array:  return "TAG_Int_Array";
        case TAG_String:     return "TAG_String";
        case TAG_List:       return "TAG_List";
        case TAG_Compound:   return "TAG_Compound";
        default:             return "UNKNOWN";
    }
}