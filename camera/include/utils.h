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

#endif