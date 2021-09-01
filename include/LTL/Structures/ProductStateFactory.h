/* Copyright (C) 2020  Nikolaj J. Ulrik <nikolaj@njulrik.dk>,
 *                     Simon M. Virenfeldt <simon@simwir.dk>
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

#ifndef VERIFYPN_PRODUCTSTATEFACTORY_H
#define VERIFYPN_PRODUCTSTATEFACTORY_H

#include "LTL/Structures/ProductState.h"

#include <memory>

namespace LTL::Structures{
    class ProductStateFactory {
    public:
        ProductStateFactory(const PetriEngine::PetriNet *net, const BuchiAutomaton& aut, size_t initial_buchi_state)
            : _net(net), _aut(aut), _buchi_init(initial_buchi_state) {}

        ProductState new_state() {
            auto buf = new PetriEngine::MarkVal[_net->number_of_places()+1];
            std::copy(_net->initial(), _net->initial() + _net->number_of_places(), buf);
            buf[_net->number_of_places()] = 0;
            ProductState state{&_aut};
            state.set_marking(buf, _net->number_of_places());
            state.set_buchi_state(_buchi_init);
            return state;
        }

    private:
        const PetriEngine::PetriNet *_net;
        const LTL::Structures::BuchiAutomaton _aut;
        const size_t _buchi_init;
    };
}
#endif //VERIFYPN_PRODUCTSTATEFACTORY_H
