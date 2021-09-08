/* Copyright (C) 2020  Alexander Bilgram <alexander@bilgram.dk>,
 *                     Peter Haar Taankvist <ptaankvist@gmail.com>,
 *                     Thomas Pedersen <thomas.pedersen@stofanet.dk>
 *                     Andreas H. Klostergaard
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COLOREDNETSTRUCTURES_H
#define COLOREDNETSTRUCTURES_H

#include "Colors.h"
#include "Expressions.h"
#include "Multiset.h"
#include <assert.h>
#include <set>
#include <vector>

namespace PetriEngine {
namespace Colored {

struct Arc {
    uint32_t _place;
    uint32_t _transition;
    ArcExpression_ptr _expr;
    bool _input;
    uint32_t _weight;
};

struct Transition {
    std::string _name;
    GuardExpression_ptr _guard;
    std::vector<Arc> _input_arcs;
    std::vector<Arc> _output_arcs;
    std::vector<std::unordered_map<const Variable *, interval_vector_t>> _variable_maps;
    bool _considered;
};

struct Place {
    std::string _name;
    const ColorType *_type;
    Multiset _marking;
    bool _inhibitor;
    bool _stable = true;
};
} // namespace Colored
} // namespace PetriEngine

#endif /* COLOREDNETSTRUCTURES_H */
