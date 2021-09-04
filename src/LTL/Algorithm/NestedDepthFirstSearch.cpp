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

#include "LTL/Algorithm/NestedDepthFirstSearch.h"

namespace LTL {
    template<typename S>
    bool NestedDepthFirstSearch<S>::is_satisfied()
{
        this->_is_weak = this->_successor_generator->is_weak() && this->_shortcircuitweak;
        dfs();
        return !_violation;
    }

    template<typename S>
    std::pair<bool,size_t> NestedDepthFirstSearch<S>::mark(State& state, const uint8_t MARKER)
    {
        auto[_, stateid] = _states.add(state);
        if(_markers.size() <= stateid)
            _markers.resize(stateid + 1);
        auto r = _markers[stateid];
        _markers[stateid] = (MARKER | r);
        const bool is_new = (r & MARKER) == 0;
        if(is_new)
        {
            ++_mark_count[MARKER];
            ++this->_discovered;
        }
        return std::make_pair(is_new, stateid);
    }

    template<typename S>
    void NestedDepthFirstSearch<S>::dfs()
    {

        light_deque<StackEntry> todo;
        light_deque<StackEntry> nested_todo;

        State working = this->_factory.new_state();
        State curState = this->_factory.new_state();

        {
            std::vector<State> initial_states = this->_successor_generator->make_initial_state();
            for (auto &state : initial_states) {
                auto res = _states.add(state);
                assert(res.first);
                todo.push_back(StackEntry{res.second, S::initial_suc_info()});
                this->_discovered++;
            }
        }

        while (!todo.empty()) {
            auto &top = todo.back();
            _states.decode(curState, top._id);
            this->_successor_generator->prepare(curState, top._sucinfo);
            if (top._sucinfo.has_prev_state()) {
                _states.decode(working, top._sucinfo._last_state);
            }
            if (!this->_successor_generator->next(working, top._sucinfo)) {
                // no successor
                todo.pop_back();
                if (this->_successor_generator->is_accepting(curState)) {
                    ndfs(curState, nested_todo);
                    if (_violation) {
                        if (_print_trace) {
                            print_trace(todo, nested_todo);
                        }
                        return;
                    }
                }
            } else {
                auto [is_new, stateid] = mark(working, _MARKER1);
                top._sucinfo._last_state = stateid;
                if (is_new) {
                    todo.push_back(StackEntry{stateid, S::initial_suc_info()});
                }
            }
        }
    }

    template<typename S>
    void NestedDepthFirstSearch<S>::ndfs(const State &state, light_deque<StackEntry>& nested_todo)
{

        State working = this->_factory.new_state();
        State curState = this->_factory.new_state();

        nested_todo.push_back(StackEntry{_states.add(state).second, S::initial_suc_info()});

        while (!nested_todo.empty()) {
            auto &top = nested_todo.back();
            _states.decode(curState, top._id);
            this->_successor_generator->prepare(curState, top._sucinfo);
            if (top._sucinfo.has_prev_state()) {
                _states.decode(working, top._sucinfo._last_state);
            }
            if (!this->_successor_generator->next(working, top._sucinfo)) {
                nested_todo.pop_back();
            } else {
                if (this->_is_weak && !this->_successor_generator->is_accepting(working)) {
                    continue;
                }
                if (working == state) {
                    _violation = true;
                    return;
                }
                auto [is_new, stateid] = mark(working, _MARKER2);
                top._sucinfo._last_state = stateid;
                if (is_new) {
                    nested_todo.push_back(StackEntry{stateid, S::initial_suc_info()});
                }
            }
        }
    }

    template<typename S>
    void NestedDepthFirstSearch<S>::print_stats(std::ostream &os)
    {
        std::cout << "STATS:\n"
                  << "\tdiscovered states:          " << _states.discovered() << std::endl
            << "\tmax tokens:                 " << _states.max_tokens() << std::endl
            << "\texplored states:            " << _mark_count[_MARKER1] << std::endl
            << "\texplored states (nested):   " << _mark_count[_MARKER2] << std::endl;
    }


    template<typename S>
    void NestedDepthFirstSearch<S>::print_trace(light_deque<StackEntry>& todo, light_deque<StackEntry>& nested_todo, std::ostream &os)
{
        State state = this->_factory.new_state();
        os << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
              "<trace>\n";

        for(auto* stck : {&todo, &nested_todo})
        {
            while(!(*stck).empty())
            {
                auto& top = (*stck).back();
                if(!top._sucinfo.has_prev_state()) break;
                _states.decode(state, top._sucinfo.state());
                this->print_transition(top._sucinfo.transition(), state, os) << std::endl;
                (*stck).pop_back();
            }
            if(stck == &todo && !nested_todo.empty())
                this->print_loop(os);
        }
        os << std::endl << "</trace>" << std::endl;
    }

    template
    class NestedDepthFirstSearch<LTL::ResumingSuccessorGenerator>;

    template
    class NestedDepthFirstSearch<LTL::SpoolingSuccessorGenerator>;
}
