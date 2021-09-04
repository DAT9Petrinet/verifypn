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

#ifndef VERIFYPN_PRODUCTSUCCESSORGENERATOR_H
#define VERIFYPN_PRODUCTSUCCESSORGENERATOR_H

#include "PetriEngine/SuccessorGenerator.h"
#include "PetriEngine/ReducingSuccessorGenerator.h"
#include "PetriEngine/PQL/PQL.h"
#include "LTL/Structures/ProductState.h"
#include "BuchiSuccessorGenerator.h"
#include "LTL/LTLToBuchi.h"
#include "LTL/Stubborn/VisibleLTLStubbornSet.h"
#include "LTL/Simplification/SpotToPQL.h"
#include "LTL/Structures/GuardInfo.h"
#include "LTL/SuccessorGeneration/SpoolingSuccessorGenerator.h"
#include "LTL/SuccessorGeneration/ResumingSuccessorGenerator.h"

#include <spot/twa/formula2bdd.hh>
#include <spot/tl/formula.hh>

namespace LTL {

    template<class SuccessorGen>
    class ProductSuccessorGenerator {
    public:

        ProductSuccessorGenerator(const PetriEngine::PetriNet& net,
                                  const Structures::BuchiAutomaton &buchi,
                                  SuccessorGen *successorGen)
                : _successor_generator(successorGen), _net(net),
                  _buchi(buchi), _aut(buchi)
        {

        }

        [[nodiscard]] size_t initial_buchi_state() const { return _buchi.initial_state_number(); };

        bool next(LTL::Structures::ProductState &state)
        {
            if (_fresh_marking) {
                _fresh_marking = false;
                if (!_successor_generator->next(state)) {
                    // This is a fresh marking, so if there is no more successors for the state the state is deadlocked.
                    // The semantics for deadlock is to just loop the marking so return true without changing the value of state.
                    std::copy(_successor_generator->parent(), _successor_generator->get_parent() + state._buchi_state_idx + 1,
                              state.marking());
                }
            }
            if (next_buchi_succ(state)) {
                return true;
            }
                // No valid transition in Büchi automaton for current marking;
                // Try next marking(s) and see if we find a successor.
            else {
                while (_successor_generator->next(state)) {
                    // reset buchi successors
                    _buchi.prepare(_buchi_parent);
                    if (next_buchi_succ(state)) {
                        return true;
                    }
                }
                return false;
            }
        }

        bool is_accepting(const LTL::Structures::ProductState &state)
        {
            return _buchi.is_accepting(state.get_buchi_state());
        }

        std::vector<LTL::Structures::ProductState> make_initial_state()
        {
            std::vector<LTL::Structures::ProductState> states;
            auto buf = new PetriEngine::MarkVal[_net.number_of_places() + 1];
            std::copy(_net.initial(), _net.initial() + _net.number_of_places(), buf);
            buf[_net.number_of_places()] = initial_buchi_state();
            LTL::Structures::ProductState state{&_buchi._aut};
            state.set_marking(buf, _net.number_of_places());
            //state.set_buchi_state(initial_buchi_state());
            _buchi.prepare(state.get_buchi_state());
            while (next_buchi_succ(state)) {
                states.emplace_back(&_buchi._aut);
                states.back().set_marking(new PetriEngine::MarkVal[_net.number_of_places() + 1], _net.number_of_places());
                std::copy(state.marking(), state.marking() + _net.number_of_places(), states.back().marking());
                states.back().set_buchi_state(state.get_buchi_state());
            }
            return states;
        }

        [[nodiscard]] bool is_initial_state(const LTL::Structures::ProductState& state) const
        {
            return state.marking_equal(_net.initial());
        }

        /**
         * prepare a state for successor generation, starting from specific point in iteration
         * @param state the source state to generate successors from
         * @param sucinfo the point in the iteration to start from, as returned by `next`.
         */
        virtual void prepare(const LTL::Structures::ProductState& state, typename SuccessorGen::successor_info_t &sucinfo)
        {
            _successor_generator->prepare(state, sucinfo);
            _fresh_marking = sucinfo.fresh();
            _buchi.prepare(state.get_buchi_state());
            _buchi_parent = state.get_buchi_state();
            if (!_fresh_marking) {
                assert(sucinfo._buchi_state != std::numeric_limits<size_t>::max());
                // spool Büchi successors until last state found.
                // TODO is there perhaps a good way to avoid this, perhaps using raw edge vector?
                // Caveat: it seems like there usually are not that many successors, so this is probably cheap regardless
                size_t tmp;
                while (_buchi.next(tmp, _cond)) {
                    if (tmp == sucinfo._buchi_state) {
                        break;
                    }
                }
            }
        }

        /**
         * compute the next successor from the last state that was sent to `prepare`.
         * @param[out] state the state to write
         * @param[out] sucinfo checkpoint information from which iteration can later be resumed.
         * @return `true` if a successor was successfully generated, `false` otherwise.
         * @warning do not use the same State for both prepare and next, this will cause wildly incorrect behaviour!
         */
        bool next(Structures::ProductState &state, typename SuccessorGen::successor_info_t &sucinfo)
        {
            if (_fresh_marking) {
                _fresh_marking = false;
                if (!_successor_generator->next(state, sucinfo)) {
                    // This is a fresh marking, so if there are no more successors for the state the state is deadlocked.
                    // The semantics for deadlock is to just loop the marking so return true without changing the value of state.
                    std::copy(_successor_generator->get_parent(), _successor_generator->get_parent() + state._buchi_state_idx + 1,
                              state.marking());
                }
            }
            if (next_buchi_succ(state)) {
                //_successor_generator->getSuccInfo(sucinfo);
                sucinfo._buchi_state = state.get_buchi_state();
                return true;
            }
                // No valid transition in Büchi automaton for current marking;
                // Try next marking(s) and see if we find a successor.
            else {
                while (_successor_generator->next(state, sucinfo)) {
                    // reset buchi successors
                    _buchi.prepare(_buchi_parent);
                    if (next_buchi_succ(state)) {
                        //_successor_generator->getSuccInfo(sucinfo);
                        sucinfo._buchi_state = state.get_buchi_state();
                        return true;
                    }
                }
                return false;
            }
        }


        [[nodiscard]] bool is_weak() const
        {
            return _buchi.is_weak();
        }

        size_t last_transition() const { return _successor_generator->last_transition();
        }

        size_t fired() const {
            return _successor_generator->fired();
        }

        void generate_all(LTL::Structures::ProductState& parent, typename SuccessorGen::successor_info_t &sucinfo)
        {
            if constexpr (std::is_same_v<SuccessorGen, LTL::SpoolingSuccessorGenerator>) {
                _successor_generator->generate_all(parent, sucinfo);
            }
        }

        size_t num_enabled()
        {
            if constexpr (std::is_same_v<SuccessorGen, LTL::SpoolingSuccessorGenerator>) {
                return _successor_generator->nenabled();
            }
            return -1;
        }

        const bool *enabled() const
        {
            if constexpr (std::is_same_v<SuccessorGen, LTL::SpoolingSuccessorGenerator>) {
                return _successor_generator->enabled();
            }
            return nullptr;
        };

        const bool *stubborn() const
        {
            if constexpr (std::is_same_v<SuccessorGen, LTL::SpoolingSuccessorGenerator>) {
                return _successor_generator->stubborn();
            }
            return nullptr;
        };

        size_t buchi_states() {
            return _buchi.buchi_states();
        }

        void push() {
            if constexpr (std::is_same_v<SuccessorGen, LTL::SpoolingSuccessorGenerator>) {
                _successor_generator->push();
            }
        }

        void pop(const typename SuccessorGen::successor_info_t &sucinfo) {
            if constexpr (std::is_same_v<SuccessorGen, LTL::SpoolingSuccessorGenerator>) {
                _successor_generator->pop(sucinfo);
            }
        }

        bool has_invariant_self_loop(const LTL::Structures::ProductState &state) {
            return _buchi.has_invariant_self_loop(state.get_buchi_state());
        }

        virtual ~ProductSuccessorGenerator() = default;

    protected:
        SuccessorGen *_successor_generator;
        const PetriEngine::PetriNet &_net;
        BuchiSuccessorGenerator _buchi;

        const LTL::Structures::BuchiAutomaton &_aut;
        bdd _cond;
        size_t _buchi_parent;
        bool _fresh_marking = true;
        std::vector<GuardInfo> _stateToGuards;
        /**
         * Evaluate binary decision diagram (BDD) representation of transition guard in given state.
         */
        bool guard_valid(const PetriEngine::Structures::State &state, bdd bdd)
        {
            PetriEngine::PQL::EvaluationContext ctx{state.marking(), _net};
            return _buchi._aut.guard_valid(ctx, bdd);
        }


    private:

        bool next_buchi_succ(LTL::Structures::ProductState &state)
        {
            size_t tmp;
            while (_buchi.next(tmp, _cond)) {
                if (guard_valid(state, _cond)) {
                    state.set_buchi_state(tmp);
                    return true;
                }
            }
            return false;
        }
    };

}


#endif //VERIFYPN_PRODUCTSUCCESSORGENERATOR_H
