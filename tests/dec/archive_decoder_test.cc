#include "algo/format.h"
#include "algo/range.h"
#include "dec/base_archive_decoder.h"
#include "test_support/catch.h"
#include "test_support/decoder_support.h"
#include "test_support/file_support.h"

using namespace au;
using namespace au::dec;

namespace
{
    struct ArchiveEntryImpl final : ArchiveEntry
    {
        size_t offset;
        size_t size;
    };

    class TestArchiveDecoder final : public BaseArchiveDecoder
    {
    public:
        TestArchiveDecoder(const algo::NamingStrategy strategy);

    protected:
        bool is_recognized_impl(io::File &input_file) const override;

        std::unique_ptr<ArchiveMeta> read_meta_impl(
            const Logger &logger, io::File &input_file) const override;

        std::unique_ptr<io::File> read_file_impl(
            const Logger &logger,
            io::File &input_file,
            const ArchiveMeta &m,
            const ArchiveEntry &e) const override;

        algo::NamingStrategy naming_strategy() const override;

    private:
        algo::NamingStrategy strategy;
    };
}

TestArchiveDecoder::TestArchiveDecoder(const algo::NamingStrategy strategy)
    : strategy(strategy)
{
}

algo::NamingStrategy TestArchiveDecoder::naming_strategy() const
{
    return strategy;
}

bool TestArchiveDecoder::is_recognized_impl(io::File &input_file) const
{
    return input_file.path.has_extension("archive");
}

std::unique_ptr<ArchiveMeta> TestArchiveDecoder::read_meta_impl(
    const Logger &logger, io::File &input_file) const
{
    input_file.stream.seek(0);
    auto meta = std::make_unique<ArchiveMeta>();
    while (!input_file.stream.eof())
    {
        auto entry = std::make_unique<ArchiveEntryImpl>();
        entry->path = input_file.stream.read_to_zero().str();
        entry->size = input_file.stream.read_u32_le();
        entry->offset = input_file.stream.tell();
        input_file.stream.skip(entry->size);
        meta->entries.push_back(std::move(entry));
    }
    return meta;
}

std::unique_ptr<io::File> TestArchiveDecoder::read_file_impl(
    const Logger &logger,
    io::File &input_file,
    const ArchiveMeta &,
    const ArchiveEntry &e) const
{
    const auto entry = static_cast<const ArchiveEntryImpl*>(&e);
    const auto data = input_file.stream.seek(entry->offset).read(entry->size);
    return std::make_unique<io::File>(entry->path, data);
}

static std::unique_ptr<io::File> make_archive(
    const io::path &archive_name,
    const std::vector<std::shared_ptr<io::File>> &files)
{
    auto output_file = std::make_unique<io::File>();
    output_file->path = archive_name;
    for (const auto &file : files)
    {
        output_file->stream.write(file->path.str());
        output_file->stream.write_u8(0);
        output_file->stream.write_u32_le(file->stream.size());
        output_file->stream.write(file->stream.seek(0).read_to_eof());
    }
    return output_file;
}

static void test_many_files(
    const TestArchiveDecoder &decoder,
    const std::string &prefix,
    const size_t file_count,
    const size_t format_size)
{
    std::vector<std::shared_ptr<io::File>> input_files;
    for (const auto i : algo::range(file_count))
        input_files.push_back(tests::stub_file("", ""_b));
    auto archive_file = make_archive("path/test.archive", input_files);
    const auto saved_files = tests::unpack(decoder, *archive_file);
    REQUIRE((saved_files.size() == file_count));
    for (const auto i : algo::range(file_count))
    {
        tests::compare_paths(
            saved_files[i]->path,
            algo::format("%s_%0*d.dat", prefix.c_str(), format_size, i));
    }
}

template<algo::NamingStrategy strategy>
    static void test_naming_strategy(const std::string &prefix)
{
    const TestArchiveDecoder decoder(strategy);

    SECTION("No files")
    {
        auto archive_file = make_archive("path/test.archive", {});
        const auto saved_files = tests::unpack(decoder, *archive_file);
        REQUIRE((saved_files.size() == 0));
    }

    SECTION("Just one file")
    {
        auto archive_file = make_archive(
            "path/test.archive", {tests::stub_file("", ""_b)});
        const auto saved_files = tests::unpack(decoder, *archive_file);
        REQUIRE((saved_files.size() == 1));
        tests::compare_paths(saved_files[0]->path, prefix + ".dat");
    }

    SECTION("Two files")
    {
        auto archive_file = make_archive(
            "path/test.archive",
            {
                tests::stub_file("", ""_b),
                tests::stub_file("", ""_b),
            });
        const auto saved_files = tests::unpack(decoder, *archive_file);
        REQUIRE((saved_files.size() == 2));
        tests::compare_paths(saved_files[0]->path, prefix + "_0.dat");
        tests::compare_paths(saved_files[1]->path, prefix + "_1.dat");
    }

    SECTION("Mixed nameless and named files")
    {
        auto archive_file = make_archive(
            "path/test.archive",
            {
                tests::stub_file("", ""_b),
                tests::stub_file("named", ""_b),
                tests::stub_file("", ""_b),
            });
        const auto saved_files = tests::unpack(decoder, *archive_file);
        REQUIRE((saved_files.size() == 3));
        tests::compare_paths(saved_files[0]->path, prefix + "_0.dat");
        tests::compare_paths(saved_files[1]->path, "named");
        tests::compare_paths(saved_files[2]->path, prefix + "_1.dat");
    }

    SECTION("Many files")
    {
        test_many_files(decoder, prefix, 9, 1);
        test_many_files(decoder, prefix, 10, 2);
        test_many_files(decoder, prefix, 11, 2);
        test_many_files(decoder, prefix, 99, 2);
        test_many_files(decoder, prefix, 100, 3);
    }
}

TEST_CASE("Simple archive unpacks correctly", "[dec]")
{
    const TestArchiveDecoder decoder(algo::NamingStrategy::Child);
    auto archive_file = make_archive(
        "test.archive",
        {
            tests::stub_file("deeply/nested/file.txt", "abc"_b)
        });
    const auto saved_files = tests::unpack(decoder, *archive_file);

    REQUIRE(saved_files.size() == 1);
    tests::compare_paths(saved_files[0]->path, "deeply/nested/file.txt");
    REQUIRE(saved_files[0]->stream.read_to_eof() == "abc"_b);
}

TEST_CASE("Archive files get proper fallback names", "[dec]")
{
    SECTION("Child naming strategy")
    {
        test_naming_strategy<algo::NamingStrategy::Child>("unk");
    }

    SECTION("Root naming strategy")
    {
        test_naming_strategy<algo::NamingStrategy::Root>("path/test");
    }

    SECTION("Sibling naming strategy")
    {
        test_naming_strategy<algo::NamingStrategy::Sibling>("test");
    }

    SECTION("Flat sibling naming strategy")
    {
        test_naming_strategy<algo::NamingStrategy::Sibling>("test");
    }
}