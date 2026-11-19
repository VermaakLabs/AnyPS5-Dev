#include "prx/libc/include/Api.hpp"
#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/FileStream.hpp"
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

static void Require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

static void InitializeDefaultHeap() {
    alignas(16) static std::array<std::byte, 0x40> process{};
    alignas(16) static std::array<std::byte, 0x38> libc{};
    alignas(16) static std::array<std::byte, 0x78> replacement{};
    const auto write = [](auto& data, std::size_t offset, auto value) {
        std::memcpy(data.data() + offset, &value, sizeof(value));
    };
    write(process, 0, std::uint64_t{0x40});
    write(process, 8, std::uint32_t{0x4942524f});
    write(process, 0x38, libc.data());
    write(libc, 0, std::uint64_t{0x38});
    write(libc, 0x30, replacement.data());
    write(replacement, 0, std::uint64_t{0x78});
    write(replacement, 8, std::uint64_t{2});
    ApplicationHeapInitialize_nid_no_patch(process.data());
}

static void WriteFile(const char* path, const char* data, std::size_t size) {
    auto* stream = fopen_nid_postfix(path, "wb");
    Require(stream != nullptr, "cannot create the test file");
    Require(fwrite_nid_postfix(data, 1, size, stream) == size, "cannot write the test file");
    Require(fclose_nid_postfix(stream) == 0, "cannot close the test file");
}

static FileStream* OpenForReading(const char* path) {
    auto* stream = fopen_nid_postfix(path, "rb");
    Require(stream != nullptr, "cannot open the test file");
    return stream;
}

static void CheckLines(const char* path) {
    constexpr char data[] = "ab\ncd\n\nlast";
    WriteFile(path, data, sizeof(data) - 1);
    auto* stream = OpenForReading(path);
    char* line = nullptr;
    std::size_t capacity = 1000;
    errno = 35;
    Require(getline_nid_postfix(&line, &capacity, stream) == 3, "getline returned the wrong first length");
    Require(std::strcmp(line, "ab\n") == 0 && capacity == 4, "getline gave the wrong first line or capacity");
    char* const first = line;
    Require(getline_nid_postfix(&line, &capacity, stream) == 3, "getline returned the wrong second length");
    Require(std::strcmp(line, "cd\n") == 0 && capacity == 4 && line == first, "getline reallocated a buffer that fitted");
    Require(getline_nid_postfix(&line, &capacity, stream) == 1 && std::strcmp(line, "\n") == 0, "getline lost an empty line");
    Require(getline_nid_postfix(&line, &capacity, stream) == 4, "getline returned the wrong length for a final line without a newline");
    Require(std::strcmp(line, "last") == 0 && capacity == 8, "getline gave the wrong final line or capacity");
    std::memset(line, 'x', capacity);
    Require(getline_nid_postfix(&line, &capacity, stream) == -1, "getline read past the end of the file");
    Require(line[0] == '\0' && capacity == 8, "getline at the end of the file did not leave an empty string");
    Require(feof_nid_postfix(stream) != 0 && ferror_nid_postfix(stream) == 0, "getline at the end of the file set the wrong flags");
    Require(errno == 35, "getline changed errno without failing");
    free_nid_postfix(line);
    Require(fclose_nid_postfix(stream) == 0, "cannot close the test file");
}

static void CheckDelimiters(const char* path) {
    constexpr char data[] = "one,two\0three,";
    WriteFile(path, data, sizeof(data) - 1);
    auto* stream = OpenForReading(path);
    char* line = nullptr;
    std::size_t capacity = 0;
    Require(getdelim_nid_postfix(&line, &capacity, ',' | 0x100, stream) == 4, "getdelim compared more than the low byte of the delimiter");
    Require(std::strcmp(line, "one,") == 0, "getdelim gave the wrong field");
    Require(getdelim_nid_postfix(&line, &capacity, '\0', stream) == 4, "getdelim did not stop at a null delimiter");
    Require(std::memcmp(line, "two\0", 5) == 0, "getdelim gave the wrong field before a null delimiter");
    Require(getdelim_nid_postfix(&line, &capacity, ',', stream) == 6 && std::strcmp(line, "three,") == 0, "getdelim gave the wrong last field");
    Require(getdelim_nid_postfix(&line, &capacity, ',', stream) == -1 && line[0] == '\0', "getdelim read past the end of the file");
    free_nid_postfix(line);
    Require(fclose_nid_postfix(stream) == 0, "cannot close the test file");
}

static void CheckGrowth(const char* path) {
    std::string data(5000, 'q');
    data += '\n';
    WriteFile(path, data.data(), data.size());
    auto* stream = OpenForReading(path);
    auto* line = static_cast<char*>(malloc_nid_postfix(100));
    Require(line != nullptr, "cannot allocate the caller's buffer");
    std::size_t capacity = 100;
    Require(getline_nid_postfix(&line, &capacity, stream) == 5001, "getline returned the wrong length for a long line");
    Require(capacity == 8192 && std::string(line) == data, "getline gave the wrong long line or capacity");
    free_nid_postfix(line);
    Require(fclose_nid_postfix(stream) == 0, "cannot close the test file");
}

static void CheckEmptyFile(const char* path) {
    WriteFile(path, "", 0);
    auto* stream = OpenForReading(path);
    char* line = nullptr;
    std::size_t capacity = 77;
    errno = 35;
    Require(getline_nid_postfix(&line, &capacity, stream) == -1, "getline read from an empty file");
    Require(line != nullptr && line[0] == '\0' && capacity == 1, "getline did not allocate an empty string at the end of the file");
    Require(errno == 35 && ferror_nid_postfix(stream) == 0, "getline at the end of an empty file reported an error");
    free_nid_postfix(line);
    Require(fclose_nid_postfix(stream) == 0, "cannot close the test file");
}

static void CheckFailures(const char* path) {
    WriteFile(path, "data\n", 5);
    auto* stream = OpenForReading(path);
    std::size_t capacity = 0;
    char* line = nullptr;
    errno = 0;
    Require(getline_nid_postfix(nullptr, &capacity, stream) == -1 && errno == 22, "getline accepted a null line pointer");
    Require(ferror_nid_postfix(stream) != 0, "getline did not set the stream error for a null line pointer");
    clearerr_nid_postfix(stream);
    errno = 0;
    Require(getdelim_nid_postfix(&line, nullptr, '\n', stream) == -1 && errno == 22, "getdelim accepted a null capacity");
    Require(ferror_nid_postfix(stream) != 0 && line == nullptr, "getdelim did not set the stream error for a null capacity");
    Require(fclose_nid_postfix(stream) == 0, "cannot close the test file");

    auto* output = fopen_nid_postfix(path, "wb");
    Require(output != nullptr, "cannot open the test file for writing");
    Require(getline_nid_postfix(&line, &capacity, output) == -1, "getline read from a write-only stream");
    Require(ferror_nid_postfix(output) != 0, "getline did not set the error of a write-only stream");
    free_nid_postfix(line);
    Require(fclose_nid_postfix(output) == 0, "cannot close the test file");

    bool threw = false;
    try {
        getline_nid_postfix(&line, &capacity, nullptr);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Require(threw, "getline accepted a null stream");
}

int main(int argc, char** argv) {
    Require(argc == 2, "usage: guest_getdelim_tests <temporary file>");
    InitializeDefaultHeap();
    CheckLines(argv[1]);
    CheckDelimiters(argv[1]);
    CheckGrowth(argv[1]);
    CheckEmptyFile(argv[1]);
    CheckFailures(argv[1]);
    std::remove(argv[1]);
    return 0;
}
