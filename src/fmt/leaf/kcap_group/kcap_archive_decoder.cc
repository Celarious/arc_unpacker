#include "fmt/leaf/kcap_group/kcap_archive_decoder.h"
#include "err.h"
#include "log.h"
#include "util/encoding.h"
#include "util/format.h"
#include "util/pack/lzss.h"
#include "util/range.h"

using namespace au;
using namespace au::fmt::leaf;

static const bstr magic = "KCAP"_b;

namespace
{
    enum class EntryType : u32
    {
        RegularFile    = 0x00000000,
        CompressedFile = 0x00000001,
    };

    struct ArchiveEntryImpl final : fmt::ArchiveEntry
    {
        size_t offset;
        size_t size;
        bool compressed;
    };
}

static size_t detect_version(io::File &input_file, const size_t file_count)
{
    size_t version = 0;
    input_file.stream.peek(input_file.stream.tell(), [&]()
    {
        input_file.stream.skip((file_count - 1) * (24 + 8));
        input_file.stream.skip(24);
        const auto last_entry_offset = input_file.stream.read_u32_le();
        const auto last_entry_size = input_file.stream.read_u32_le();
        if (last_entry_offset + last_entry_size == input_file.stream.size())
            version = 1;
    });
    input_file.stream.peek(input_file.stream.tell(), [&]()
    {
        input_file.stream.skip((file_count - 1) * (4 + 24 + 8));
        input_file.stream.skip(4 + 24);
        const auto last_entry_offset = input_file.stream.read_u32_le();
        const auto last_entry_size = input_file.stream.read_u32_le();
        if (last_entry_offset + last_entry_size == input_file.stream.size())
            version = 2;
    });
    return version;
}

static std::unique_ptr<fmt::ArchiveMeta> read_meta_v1(
    io::File &input_file, const size_t file_count)
{
    auto meta = std::make_unique<fmt::ArchiveMeta>();
    for (auto i : util::range(file_count))
    {
        auto entry = std::make_unique<ArchiveEntryImpl>();
        entry->compressed = true;
        entry->name = util::sjis_to_utf8(
            input_file.stream.read_to_zero(24)).str();
        entry->offset = input_file.stream.read_u32_le();
        entry->size = input_file.stream.read_u32_le();
        meta->entries.push_back(std::move(entry));
    }
    return meta;
}

static std::unique_ptr<fmt::ArchiveMeta> read_meta_v2(
    io::File &input_file, const size_t file_count)
{
    auto meta = std::make_unique<fmt::ArchiveMeta>();
    for (auto i : util::range(file_count))
    {
        auto entry = std::make_unique<ArchiveEntryImpl>();
        const auto type = static_cast<EntryType>(
            input_file.stream.read_u32_le());
        entry->name = util::sjis_to_utf8(
            input_file.stream.read_to_zero(24)).str();
        entry->offset = input_file.stream.read_u32_le();
        entry->size = input_file.stream.read_u32_le();
        if (type == EntryType::RegularFile)
            entry->compressed = false;
        else if (type == EntryType::CompressedFile)
            entry->compressed = true;
        else
        {
            if (!entry->size)
                continue;
            Log.warn("Unknown entry type: %08x\n", type);
        }
        meta->entries.push_back(std::move(entry));
    }
    return meta;
}

bool KcapArchiveDecoder::is_recognized_impl(io::File &input_file) const
{
    return input_file.stream.read(magic.size()) == magic;
}

std::unique_ptr<fmt::ArchiveMeta>
    KcapArchiveDecoder::read_meta_impl(io::File &input_file) const
{
    input_file.stream.seek(magic.size());
    const auto file_count = input_file.stream.read_u32_le();
    const auto version = detect_version(input_file, file_count);
    if (version == 1)
        return read_meta_v1(input_file, file_count);
    else if (version == 2)
        return read_meta_v2(input_file, file_count);
    else
        throw err::UnsupportedVersionError(version);
}

std::unique_ptr<io::File> KcapArchiveDecoder::read_file_impl(
    io::File &input_file, const ArchiveMeta &m, const ArchiveEntry &e) const
{
    const auto entry = static_cast<const ArchiveEntryImpl*>(&e);
    input_file.stream.seek(entry->offset);
    bstr data;
    if (entry->compressed)
    {
        auto size_comp = input_file.stream.read_u32_le();
        auto size_orig = input_file.stream.read_u32_le();
        data = input_file.stream.read(size_comp - 8);
        data = util::pack::lzss_decompress_bytewise(data, size_orig);
    }
    else
        data = input_file.stream.read(entry->size);
    return std::make_unique<io::File>(entry->name, data);
}

std::vector<std::string> KcapArchiveDecoder::get_linked_formats() const
{
    return {"truevision/tga", "leaf/bbm", "leaf/bjr"};
}

static auto dummy = fmt::register_fmt<KcapArchiveDecoder>("leaf/kcap");