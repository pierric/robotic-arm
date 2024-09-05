#include <sys/time.h>
#include <numeric>
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

inline constexpr uint32_t cpow(const uint32_t base, unsigned const exponent) noexcept
{
    return (exponent == 0) ? 1 : (base * cpow(base, exponent-1));
}

pair<uint32_t, uint32_t> float_to_rational(float val)
{
    auto numer = uint32_t(val * cpow(10, 4));
    auto denom = uint32_t(cpow(10, 4));
    auto gcd = std::gcd(numer, denom);
    if ((val > 0 && val > numer) || (val < 0 && val < numer)) {
        return std::make_pair(0, 1);
    }
    return std::make_pair(numer / gcd, denom / gcd);
}