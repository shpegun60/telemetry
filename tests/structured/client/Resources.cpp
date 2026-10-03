/* Existing Stage 10 bytes consumed by the Stage 12 client. MIT.
 * Authors: Ruslan Kovtun (shpegun60), codexAi.
 */
#include "../resources/Fixture.hpp"
#include <filesystem>
#include <fstream>
#include <vector>

static void save(const std::filesystem::path& path, std::span<const std::byte> data)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file) throw std::runtime_error("Cannot write fixture");
}

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    const std::filesystem::path directory{argv[1]};
    save(directory / "resources-descriptor.bin", fixture::packed);
    std::vector<std::byte> bytes(fixture::values.size());
    const auto read = fixture::values.read(0, bytes);
    if (read.status != resource::Status::Ok || !read.eof || read.written != bytes.size()) return 2;
    save(directory / "resources-values.bin", bytes);
    return 0;
}
