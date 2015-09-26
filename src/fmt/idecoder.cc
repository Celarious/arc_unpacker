#include "fmt/idecoder.h"
#include <boost/filesystem/path.hpp>

using namespace au;
using namespace au::fmt;

std::string FileNameDecorator::decorate(
    const FileNamingStrategy &strategy,
    const std::string &parent_file_name,
    const std::string &current_file_name)
{
    switch (strategy)
    {
        case FileNamingStrategy::Root:
            return current_file_name;

        case FileNamingStrategy::Child:
        case FileNamingStrategy::Sibling:
        {
            if (parent_file_name == "")
                return current_file_name;
            auto path = boost::filesystem::path(parent_file_name);
            if (strategy == FileNamingStrategy::Sibling)
                path = path.parent_path();
            path /= current_file_name;
            return path.string();
        }

        default:
            throw std::logic_error("Invalid file naming strategy");
    }
}

IDecoder::~IDecoder()
{
}