/* VerifyPN - TAPAAL Petri Net Engine
 * Copyright (C) 2017 Peter Gjøl Jensen <root@petergjoel.dk>
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BINARYPARSER_H
#define BINARYPARSER_H

#include <iostream>
#include <set>
#include <vector>

#include "PNMLParser.h"
#include "QueryParser.h"
using namespace PetriEngine::PQL;

class QueryBinaryParser {
  public:
    QueryBinaryParser() = default;
    ~QueryBinaryParser() = default;

    std::vector<query_item_t> _queries;

    auto parse(std::ifstream &binary, const std::set<size_t> &) -> bool;

  private:
    auto parse_query(std::ifstream &binary, const std::vector<std::string> &names) -> Condition_ptr;
    auto parse_expr(std::ifstream &binary, const std::vector<std::string> &names) -> Expr_ptr;
};

#endif /* BINARYPARSER_H */
