#include "nbt/NbtIo.h"

#include <utility>

#include "java/InputOutputStream/BufferedOutputStream.h"
#include "java/InputOutputStream/ByteArrayInputStream.h"
#include "java/InputOutputStream/ByteArrayOutputStream.h"
#include "java/InputOutputStream/DataInputStream.h"
#include "java/InputOutputStream/DataOutputStream.h"
#include "nbt/CompoundTag.h"
#include "nbt/Tag.h"

class DataInput;
class DataOutput;
class OutputStream;

CompoundTag* NbtIo::readCompressed(InputStream* in) {
    DataInputStream dis(in);
    CompoundTag* ret = NbtIo::read((DataInput*)&dis);
    dis.close();
    return ret;
}

void NbtIo::writeCompressed(CompoundTag* tag, OutputStream* out) {
    // Aumentamos el buffer a 16KB para evitar llamadas de I/O fragmentadas
    BufferedOutputStream bos(out, 16384);
    DataOutputStream dos(&bos);
    NbtIo::write(tag, &dos);
    dos.close();
}

// CERO COPIAS: 'const std::vector<uint8_t>&' en lugar de paso por valor
CompoundTag* NbtIo::decompress(const std::vector<uint8_t>& buffer) {
    ByteArrayInputStream bais(const_cast<std::vector<uint8_t>&>(buffer));
    DataInputStream in(&bais);
    CompoundTag* ret = NbtIo::read((DataInput*)&in);
    bais.reset();
    in.close();
    return ret;
}

std::vector<uint8_t> NbtIo::compress(CompoundTag* tag) {
    ByteArrayOutputStream baos;
    DataOutputStream dos(&baos);
    NbtIo::write(tag, &dos);
    dos.close();
    // Movimiento directo sin pasar por System::arraycopy
    return std::move(baos.buf);
}

CompoundTag* NbtIo::read(DataInput* dis) {
    Tag* tag = Tag::readNamedTag(dis);
    if (tag == nullptr) return nullptr; // PROTECCIÓN CONTRA SEGFAULT

    if (tag->getId() == Tag::TAG_Compound) {
        return static_cast<CompoundTag*>(tag);
    }

    delete tag;
    return nullptr;
}

void NbtIo::write(CompoundTag* tag, DataOutput* dos) {
    Tag::writeNamedTag(tag, dos);
}