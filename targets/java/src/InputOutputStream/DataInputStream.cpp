#include "java/InputOutputStream/DataInputStream.h"

#include <cstdio>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

#include "java/InputOutputStream/InputStream.h"

DataInputStream::DataInputStream(InputStream* in) : stream(in) {}

int DataInputStream::read() {
    if (!stream) [[unlikely]] return -1;
    return stream->read();
}

int DataInputStream::read(std::vector<uint8_t>& b) {
    if (!stream) [[unlikely]] return -1;
    return stream->read(b, 0, static_cast<unsigned int>(b.size()));
}

int DataInputStream::read(std::vector<uint8_t>& b, unsigned int offset, unsigned int length) {
    if (!stream) [[unlikely]] return -1;
    return stream->read(b, offset, length);
}

void DataInputStream::close() {
    if (stream) [[likely]] {
        stream->close();
    }
}

bool DataInputStream::readBoolean() {
    return readByte() != 0;
}

uint8_t DataInputStream::readByte() {
    if (!stream) [[unlikely]] return 0;
    int b = stream->read();
    return (b != -1) ? static_cast<uint8_t>(b) : 0;
}

unsigned char DataInputStream::readUnsignedByte() {
    return static_cast<unsigned char>(readByte());
}

char DataInputStream::readChar() {
    return static_cast<char>(readShort());
}

// LECTURA EN BLOQUE: Pasa de 65,536 llamadas virtuales a 1 sola llamada en el 99% de los casos
bool DataInputStream::readFully(std::vector<uint8_t>& b) {
    if (!stream) [[unlikely]] return false;
    if (b.empty()) return true;

    unsigned int totalRead = 0;
    const unsigned int targetSize = static_cast<unsigned int>(b.size());

    while (totalRead < targetSize) {
        int bytesRead = stream->read(b, totalRead, targetSize - totalRead);
        if (bytesRead <= 0) {
            return false; // EOF prematuro o error
        }
        totalRead += static_cast<unsigned int>(bytesRead);
    }
    return true;
}

// LECTURA EN BLOQUE SEGURA: Sin Type-Punning ni Undefined Behavior
bool DataInputStream::readFully(std::vector<char>& b) {
    if (!stream) [[unlikely]] return false;
    if (b.empty()) return true;

    // Buffer temporal de 4KB para leer en ráfagas sin cast ilegal de std::vector
    constexpr size_t CHUNK_SIZE = 4096;
    std::vector<uint8_t> chunkBuf(std::min<size_t>(b.size(), CHUNK_SIZE));

    size_t totalRead = 0;
    const size_t targetSize = b.size();

    while (totalRead < targetSize) {
        unsigned int toRead = static_cast<unsigned int>(std::min<size_t>(chunkBuf.size(), targetSize - totalRead));
        int bytesRead = stream->read(chunkBuf, 0, toRead);
        if (bytesRead <= 0) {
            return false;
        }
        for (int i = 0; i < bytesRead; ++i) {
            b[totalRead + i] = static_cast<char>(chunkBuf[i]);
        }
        totalRead += static_cast<size_t>(bytesRead);
    }
    return true;
}

double DataInputStream::readDouble() {
    int64_t bits = readLong();
    return std::bit_cast<double>(bits);
}

float DataInputStream::readFloat() {
    int bits = readInt();
    return std::bit_cast<float>(bits);
}

// Lectura de 4 bytes con validación de EOF rápida
int DataInputStream::readInt() {
    if (!stream) [[unlikely]] return 0;

    int a = stream->read();
    int b = stream->read();
    int c = stream->read();
    int d = stream->read();

    if ((a | b | c | d) < 0) [[unlikely]] {
        return 0; // EOF
    }

    return (a << 24) | (b << 16) | (c << 8) | d;
}

// Lectura de 8 bytes con unificación de desplazamientos
int64_t DataInputStream::readLong() {
    if (!stream) [[unlikely]] return 0;

    int64_t a = stream->read();
    int64_t b = stream->read();
    int64_t c = stream->read();
    int64_t d = stream->read();
    int64_t e = stream->read();
    int64_t f = stream->read();
    int64_t g = stream->read();
    int64_t h = stream->read();

    if ((a | b | c | d | e | f | g | h) < 0) [[unlikely]] {
        return 0; // EOF
    }

    return (a << 56) | (b << 48) | (c << 40) | (d << 32) |
           (e << 24) | (f << 16) | (g <<  8) | h;
}

short DataInputStream::readShort() {
    if (!stream) [[unlikely]] return 0;

    int a = stream->read();
    int b = stream->read();

    if ((a | b) < 0) [[unlikely]] {
        return 0; // EOF
    }

    return static_cast<short>((a << 8) | b);
}

unsigned short DataInputStream::readUnsignedShort() {
    return static_cast<unsigned short>(readShort());
}

std::string DataInputStream::readUTF() {
    std::string outputString;
    if (!stream) [[unlikely]] return outputString;

    int a = stream->read();
    int b = stream->read();
    if ((a | b) < 0) [[unlikely]] return outputString;

    unsigned short UTFLength = static_cast<unsigned short>((a << 8) | b);
    if (UTFLength == 0) return outputString;

    // PRE-RESERVA DE MEMORIA: Elimina reallocs en el heap
    outputString.reserve(UTFLength);

    unsigned short currentByteIndex = 0;
    while (currentByteIndex < UTFLength) {
        int firstByte = stream->read();
        currentByteIndex++;

        if (firstByte == -1) break;

        // Comprobación de formato UTF-8 (1, 2 o 3 bytes)
        if (((firstByte & 0xC0) == 0x80) || ((firstByte & 0xF0) == 0xF0)) {
            break;
        } else if ((firstByte & 0x80) == 0x00) {
            outputString.push_back(static_cast<char>(firstByte));
        } else if ((firstByte & 0xE0) == 0xC0) {
            if (currentByteIndex >= UTFLength) break;
            int secondByte = stream->read();
            currentByteIndex++;

            if (secondByte == -1 || ((secondByte & 0xC0) != 0x80)) break;

            outputString.push_back(static_cast<char>(firstByte));
            outputString.push_back(static_cast<char>(secondByte));
        } else if ((firstByte & 0xF0) == 0xE0) {
            if (currentByteIndex >= UTFLength) break;
            int secondByte = stream->read();
            currentByteIndex++;
            if (secondByte == -1) break;

            if (currentByteIndex >= UTFLength) break;
            int thirdByte = stream->read();
            currentByteIndex++;
            if (thirdByte == -1) break;

            if (((secondByte & 0xC0) != 0x80) || ((thirdByte & 0xC0) != 0x80)) break;

            outputString.push_back(static_cast<char>(firstByte));
            outputString.push_back(static_cast<char>(secondByte));
            outputString.push_back(static_cast<char>(thirdByte));
        }
    }

    return outputString;
}

int DataInputStream::readUTFChar() {
    if (!stream) [[unlikely]] return -1;

    int firstByte = stream->read();
    if (firstByte == -1) return -1;

    if (((firstByte & 0xC0) == 0x80) || ((firstByte & 0xF0) == 0xF0)) {
        return -1;
    } else if ((firstByte & 0x80) == 0x00) {
        return firstByte;
    } else if ((firstByte & 0xE0) == 0xC0) {
        int secondByte = stream->read();
        if (secondByte == -1 || ((secondByte & 0xC0) != 0x80)) return -1;
        return ((firstByte & 0x1F) << 6) | (secondByte & 0x3F);
    } else if ((firstByte & 0xF0) == 0xE0) {
        int secondByte = stream->read();
        if (secondByte == -1) return -1;

        int thirdByte = stream->read();
        if (thirdByte == -1 || ((secondByte & 0xC0) != 0x80) || ((thirdByte & 0xC0) != 0x80)) return -1;

        return (((firstByte & 0x0F) << 12) | ((secondByte & 0x3F) << 6) | (thirdByte & 0x3F));
    }
    return -1;
}

unsigned long long DataInputStream::readPlayerUID() {
    return static_cast<unsigned long long>(readLong());
}

void DataInputStream::deleteChildStream() {
    delete stream;
    stream = nullptr;
}

int64_t DataInputStream::skip(int64_t n) {
    if (!stream) [[unlikely]] return 0;
    return stream->skip(n);
}

int DataInputStream::skipBytes(int n) {
    return static_cast<int>(skip(n));
}