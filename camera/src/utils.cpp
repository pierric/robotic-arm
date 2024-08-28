#include "utils.h"

pair<string, optional<int>> parseEndpoint(const string& spec)
{
    size_t pos = spec.rfind(':');
    if (pos != string::npos) {
        return std::make_pair(spec.substr(0, pos), std::stoi(spec.substr(pos+1)));
    }

    return std::make_pair(spec, std::nullopt);
}
