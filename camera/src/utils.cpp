#include <sys/time.h>
#include "utils.h"

pair<string, optional<int>> parseEndpoint(const string& spec)
{
    size_t pos = spec.rfind(':');
    if (pos != string::npos) {
        return std::make_pair(spec.substr(0, pos), std::stoi(spec.substr(pos+1)));
    }

    return std::make_pair(spec, std::nullopt);
}

double get_timestamp()
{
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    return tv_now.tv_sec + (double)tv_now.tv_usec / 1000000.0f;
}