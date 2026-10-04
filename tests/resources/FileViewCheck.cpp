// Borrowed file iteration and explicit provider operations. MIT.
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include <resource/Resource.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utility>

using namespace resource;

static_assert(std::forward_iterator<FileIterator>);
static_assert(std::same_as<std::iter_value_t<FileIterator>, FileView>);
static_assert(std::same_as<std::iter_reference_t<FileIterator>, FileView>);
static_assert(std::ranges::forward_range<FileSystemView>);
static_assert(std::ranges::forward_range<const FileSystemView>);
static_assert(std::ranges::borrowed_range<FileSystemView>);
static_assert(std::ranges::forward_range<FileSystem<3>>);
static_assert(std::ranges::forward_range<const FileSystem<3>>);
static_assert(!std::ranges::borrowed_range<FileSystem<3>>);
static_assert(std::same_as<std::ranges::borrowed_iterator_t<FileSystemView>, FileIterator>);
static_assert(std::is_trivially_copyable_v<FileView> && std::is_trivially_copyable_v<FileIterator>);
static_assert(!std::is_polymorphic_v<FileView> && !std::is_polymorphic_v<FileIterator>);
static_assert(sizeof(FileView) <= 2 * sizeof(void*) && sizeof(FileIterator) <= 2 * sizeof(void*));
static_assert(!std::is_convertible_v<FileView, bool>);
static_assert(!std::is_constructible_v<FileView, const FileEntry*, FileIndex>);
static_assert(!std::is_constructible_v<FileIterator, const FileEntry*, FileIndex>);

template <class T>
concept BorrowedAccess = requires(T&& value) {
    std::forward<T>(value)[0];
    std::forward<T>(value).begin();
    std::forward<T>(value).end();
};
static_assert(BorrowedAccess<FileSystem<3>&> && BorrowedAccess<const FileSystem<3>&>);
static_assert(!BorrowedAccess<FileSystem<3>> && !BorrowedAccess<const FileSystem<3>>);
static_assert(BorrowedAccess<FileSystemView> && BorrowedAccess<const FileSystemView>);

constexpr FileSystemView emptyView;
static_assert(emptyView.empty() && emptyView.size() == 0 && emptyView.begin() == emptyView.end());
static_assert(emptyView.begin() == FileIterator{});
static_assert(!emptyView[42] && emptyView[42].index() == 42 && emptyView[42].path().empty());
static_assert(!emptyView[42].readable() && !emptyView[42].writable());
static_assert(!FileView{}.readable() && !FileView{}.writable());
static_assert(filesystem().empty() && filesystem().size() == 0);

// None of the public operations tested below may use C++ heap allocation.
void* operator new(std::size_t) { std::abort(); }
void* operator new[](std::size_t) { std::abort(); }
void operator delete(void*) noexcept { std::abort(); }
void operator delete[](void*) noexcept { std::abort(); }
void operator delete(void*, std::size_t) noexcept { std::abort(); }
void operator delete[](void*, std::size_t) noexcept { std::abort(); }

namespace
{
unsigned checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #__VA_ARGS__); std::abort(); } } while (false)

struct ReadWrite
{
    FileSize extent = 1;
    std::byte value{42};
    mutable unsigned stats = 0;
    mutable unsigned reads = 0;
    unsigned writes = 0;
    mutable Cursor readCursor = 0;
    Cursor writeCursor = 0;
    bool final = false;

    FileSize size() const noexcept { ++stats; return extent; }
    ReadResult read(Cursor cursor, Output out) const noexcept
    {
        ++reads;
        readCursor = cursor;
        if (out.empty()) return {Status::BufferTooSmall, cursor};
        out.front() = value;
        return {Status::Ok, cursor + 1, 1, true};
    }
    WriteResult write(Cursor cursor, Input in, bool finished) noexcept
    {
        ++writes;
        writeCursor = cursor;
        final = finished;
        if (!in.empty()) value = in.front();
        return {Status::Ok, cursor + in.size(), static_cast<std::uint32_t>(in.size()), finished};
    }
};

struct ReadOnly
{
    mutable unsigned stats = 0;
    mutable unsigned reads = 0;
    FileSize size() const noexcept { ++stats; return 1; }
    ReadResult read(Cursor cursor, Output out) const noexcept
    {
        ++reads;
        if (out.empty()) return {Status::BufferTooSmall, cursor};
        out.front() = std::byte{17};
        return {Status::Ok, cursor + 1, 1, true};
    }
};

struct WriteOnly
{
    mutable unsigned stats = 0;
    unsigned writes = 0;
    FileSize size() const noexcept { ++stats; return 0; }
    WriteResult write(Cursor cursor, Input in, bool final) noexcept
    {
        ++writes;
        return {Status::Ok, cursor + in.size(), static_cast<std::uint32_t>(in.size()), final};
    }
};

ReadWrite capabilityReadWrite;
const ReadOnly capabilityReadOnly;
WriteOnly capabilityWriteOnly;
constexpr auto capabilityFiles = filesystem(file("/rw", capabilityReadWrite),
    file("/source", capabilityReadOnly), file("/sink", capabilityWriteOnly));
static_assert(capabilityFiles[0].readable() && capabilityFiles[0].writable());
static_assert(capabilityFiles[1].readable() && !capabilityFiles[1].writable());
static_assert(!capabilityFiles[2].readable() && capabilityFiles[2].writable());

void checkInvalid(FileView file, FileIndex index)
{
    CHECK(file.index() == index);
    CHECK(!file.valid() && !static_cast<bool>(file) && file.path().empty());
    CHECK(!file.readable() && !file.writable());
    const auto stat = file.stat();
    CHECK(stat.status == Status::InvalidFile && stat.size == 0 && stat.flags == FileFlag::None);
    std::array output{std::byte{91}, std::byte{92}};
    const auto before = output;
    constexpr auto cursor = std::numeric_limits<Cursor>::max();
    const auto read = file.read(cursor, output);
    CHECK(read.status == Status::InvalidFile && read.next == cursor && read.written == 0 && !read.eof);
    CHECK(output == before);
    const auto write = file.write(cursor, output, true);
    CHECK(write.status == Status::InvalidFile && write.next == cursor && write.consumed == 0 && !write.complete);
}
} // namespace

int main()
{
    ReadWrite provider;
    const ReadOnly source;
    WriteOnly sink;
    const auto files = filesystem(file("/rw", provider), file("/source", source), file("/sink", sink));
    const auto view = files.view();
    CHECK(files.size() == 3 && files.fileCount() == 3 && !files.empty());
    CHECK(view.size() == 3 && view.fileCount() == 3 && !view.empty());
    CHECK(provider.stats == 0 && source.stats == 0 && sink.stats == 0);
    CHECK(view[0].readable() && view[0].writable());
    CHECK(view[1].readable() && !view[1].writable());
    CHECK(!view[2].readable() && view[2].writable());
    CHECK(provider.stats == 0 && provider.reads == 0 && provider.writes == 0);
    CHECK(source.stats == 0 && source.reads == 0 && sink.stats == 0 && sink.writes == 0);

    const std::array<std::string_view, 3> paths{"/rw", "/source", "/sink"};
    const std::array readable{true, true, false};
    const std::array writable{true, false, true};
    unsigned seen = 0;
    for (auto current : files)
    {
        CHECK(current.valid() && current.index() == seen && current.path() == paths[seen]);
        CHECK(current.readable() == readable[seen] && current.writable() == writable[seen]);
        ++seen;
    }
    CHECK(seen == 3);
    CHECK(provider.stats == 0 && provider.reads == 0 && provider.writes == 0);
    CHECK(source.stats == 0 && source.reads == 0 && sink.stats == 0 && sink.writes == 0);

    auto first = files.begin();
    auto independent = first;
    CHECK(first == independent && (*first).index() == 0);
    ++first;
    CHECK(first != independent && (*first).index() == 1 && (*independent).index() == 0);
    const auto previous = independent++;
    CHECK((*previous).index() == 0 && independent == first);
    ++first;
    CHECK((*first).index() == 2 && (*independent).index() == 1);
    ++first;
    CHECK(first == files.end());
    CHECK(std::ranges::distance(view) == 3);
    CHECK(std::ranges::distance(files) == 3);
    const auto found = std::ranges::find_if(files.view(), [](FileView current) {
        return current.path() == "/sink";
    });
    static_assert(std::same_as<std::remove_cv_t<decltype(found)>, FileIterator>);
    CHECK(found != files.end() && (*found).index() == 2);
    CHECK(provider.stats == 0 && source.stats == 0 && sink.stats == 0);

    // These objects remain valid after the temporary facade has disappeared.
    const auto returnedView = [&files] { return files.view(); };
    auto stable = returnedView()[0];
    auto stableIterator = std::ranges::begin(returnedView());
    CHECK(stable.index() == 0 && stable.path() == "/rw");
    CHECK((*stableIterator).path() == stable.path());
    const auto copiedFacade = stable;
    const auto copiedTable = files;
    CHECK(copiedTable.begin() != files.begin());
    CHECK(copiedTable[0].path() == stable.path());

    provider.extent = 7;
    const auto stat = copiedFacade.stat();
    CHECK(stat.status == Status::Ok && stat.size == 7 &&
          stat.flags == (FileFlag::Readable | FileFlag::Writable));
    CHECK(provider.stats == 1 && provider.reads == 0 && provider.writes == 0);
    provider.extent = 9;
    CHECK(stable.stat().size == 9 && provider.stats == 2);
    constexpr Cursor cursor = UINT64_C(0x1234567800000000);
    std::array<std::byte, 2> output{std::byte{91}, std::byte{92}};
    const auto read = copiedFacade.read(cursor, output);
    CHECK(read.status == Status::Ok && read.next == cursor + 1 && read.written == 1 && read.eof);
    CHECK(output[0] == std::byte{42} && output[1] == std::byte{92});
    CHECK(provider.reads == 1 && provider.readCursor == cursor && provider.stats == 2);
    const std::array input{std::byte{77}};
    const auto write = stable.write(cursor, input, true);
    CHECK(write.status == Status::Ok && write.next == cursor + 1 && write.consumed == 1 && write.complete);
    CHECK(provider.writes == 1 && provider.writeCursor == cursor && provider.final && provider.stats == 2);
    const auto sharedRead = copiedTable[0].read(0, output);
    CHECK(sharedRead.status == Status::Ok && output[0] == std::byte{77} && provider.reads == 2);
    const auto defaultFinal = copiedFacade.write(5, {});
    CHECK(defaultFinal.status == Status::Ok && defaultFinal.next == 5 && defaultFinal.consumed == 0 &&
          !defaultFinal.complete && !provider.final && provider.writes == 2);

    CHECK(files[1].stat().flags == FileFlag::Readable && source.stats == 1);
    const auto deniedWrite = files[1].write(cursor, input, true);
    CHECK(deniedWrite.status == Status::NotWritable && deniedWrite.next == cursor &&
          deniedWrite.consumed == 0 && !deniedWrite.complete);
    CHECK(files[1].read(0, output).status == Status::Ok && source.reads == 1 && output[0] == std::byte{17});
    CHECK(files[2].stat().flags == FileFlag::Writable && sink.stats == 1);
    const auto beforeDenied = output;
    const auto deniedRead = files[2].read(cursor, output);
    CHECK(deniedRead.status == Status::NotReadable && deniedRead.next == cursor &&
          deniedRead.written == 0 && !deniedRead.eof && output == beforeDenied);
    CHECK(files[2].write(0, input, true).status == Status::Ok && sink.writes == 1);

    checkInvalid(FileView{}, 0);
    checkInvalid(FileSystemView{}[17], 17);
    for (const FileIndex index : {FileIndex{3}, FileIndex{4}, std::numeric_limits<FileIndex>::max()})
    {
        checkInvalid(files[index], index);
    }
    CHECK(provider.stats == 2 && provider.reads == 2 && provider.writes == 2);
    CHECK(source.stats == 1 && source.reads == 1 && sink.stats == 1 && sink.writes == 1);

    const auto noFiles = filesystem();
    CHECK(noFiles.empty() && noFiles.size() == 0 && noFiles.begin() == noFiles.end());
    CHECK(noFiles.view().begin() == noFiles.view().end());
    CHECK(std::ranges::distance(noFiles) == 0 && std::ranges::distance(FileSystemView{}) == 0);
    for ([[maybe_unused]] auto current : noFiles) CHECK(false);
    unsigned temporaryCount = 0;
    // Range-for extends the owning table's lifetime for the complete loop.
    for (auto current : filesystem(file("/loop", provider)))
    {
        CHECK(current.path() == "/loop");
        ++temporaryCount;
    }
    CHECK(temporaryCount == 1 && provider.stats == 2);
    std::printf("FileView: %u checks passed\n", checks);
}
