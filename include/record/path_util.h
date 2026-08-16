#pragma once

#include <cerrno>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

namespace ipc_mini::record {

inline const char* stream_tag(int stream_id)
{
    return stream_id == 1 ? "sub" : "main";
}

inline bool mkdir_p(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
    std::string current;
    current.reserve(path.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        const char ch = path[i];
        current.push_back(ch);
        if (ch != '/' && i + 1 != path.size()) {
            continue;
        }
        if (current.empty() || current == "/") {
            continue;
        }
        if (current.size() > 1 && current.back() == '/') {
            current.pop_back();
        }
        if (::mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
            return false;
        }
        if (i + 1 != path.size() && current.back() != '/') {
            current.push_back('/');
        }
    }
    return true;
}

inline std::string join_path(const std::string& directory, const std::string& name)
{
    if (directory.empty()) {
        return name;
    }
    if (directory.back() == '/') {
        return directory + name;
    }
    return directory + "/" + name;
}

} // namespace ipc_mini::record
