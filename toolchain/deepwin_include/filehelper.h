#pragma once
#include <fstream>
#include <string>
#include <iostream>
#include "json.hpp"

using json = nlohmann::json;

bool file2json(const std::string& filename, json &ret_json);

bool get_json_config(std::string conf_type, std::string name, std::string &json_str);
