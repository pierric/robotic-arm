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

// IDF_VER == 4.*
#define FMT_UINT32_T "%u"
// IDF_VER == 5.*
// #define FMT_UINT32_T "%lu"

#endif