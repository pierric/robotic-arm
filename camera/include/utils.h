#ifndef _UTILS_H
#define _UTILS_H

#include <string>
#include <optional>
#include <utility>

using std::optional;
using std::pair;
using std::string;

pair<string, optional<int>> parseEndpoint(const string& spec);

double get_timestamp();

pair<uint32_t, uint32_t> float_to_rational(float val);

#if ESP_IDF_VERSION_MAJOR == 4
#define FMT_UINT32_T "%u"
#elif ESP_IDF_VERSION_MAJOR == 5
#define FMT_UINT32_T "%lu"
#endif

#endif