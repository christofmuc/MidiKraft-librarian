/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <rapidjson/document.h>

#include <string>

rapidjson::Value value(std::string const &string, rapidjson::Document &document);
std::string renderToJson(rapidjson::Document &doc);
std::string renderToJson(rapidjson::Value const &value);
void addToJson(std::string const &key, std::string const &data, rapidjson::Value &object, rapidjson::Document &doc);

