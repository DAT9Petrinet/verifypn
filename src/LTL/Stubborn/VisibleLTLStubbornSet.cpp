/* Copyright (C) 2021  Nikolaj J. Ulrik <nikolaj@njulrik.dk>,
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

#include "LTL/Stubborn/VisibleLTLStubbornSet.h"
#include "LTL/Stubborn/EvalAndSetVisitor.h"
#include "PetriEngine/Stubborn/InterestingTransitionVisitor.h"

using namespace PetriEngine;
using namespace PetriEngine::PQL;

namespace LTL {
bool VisibleLTLStubbornSet::prepare(const PetriEngine::Structures::State &marking) {
    reset();
    _parent = &marking;
    PQL::EvaluationContext evaluationContext{_parent->marking(), _net};
    memset(_places_seen.get(), 0, _net.number_of_places());
    construct_enabled();
    if (_ordering.empty())
        return false;
    if (_ordering.size() == 1) {
        _stubborn[_ordering.front()] = true;
        return true;
    }
    // TODO needed? We do not run Interesting visitor so we do not immediately need it, but is is
    // needed by closure?
    for (auto &q : _queries) {
        EvalAndSetVisitor evalAndSetVisitor{evaluationContext};
        q->visit(evalAndSetVisitor);
    }
    find_key_transition();

    ensure_rule_v();

    ensure_rules_l();

    _nenabled = _ordering.size();

    if (!_has_enabled_stubborn) {
        memset(_stubborn.get(), 1, _net.number_of_transitions());
    }
#ifdef STUBBORN_STATISTICS
    float num_stubborn = 0;
    float num_enabled = 0;
    float num_enabled_stubborn = 0;
    for (int i = 0; i < _net.number_of_transitions(); ++i) {
        if (_stubborn[i])
            ++num_stubborn;
        if (_fallback_spooler[i])
            ++num_enabled;
        if (_stubborn[i] && _fallback_spooler[i])
            ++num_enabled_stubborn;
    }
    std::cerr << "Enabled: " << num_enabled << "/" << _net.number_of_transitions() << " ("
              << num_enabled / _net.number_of_transitions() * 100.0 << "%),\t\t "
              << "Stubborn: " << num_stubborn << "/" << _net.number_of_transitions() << " ("
              << num_stubborn / _net.number_of_transitions() * 100.0 << "%),\t\t "
              << "Enabled stubborn: " << num_enabled_stubborn << "/" << num_enabled << " ("
              << num_enabled_stubborn / num_enabled * 100.0 << "%)" << std::endl;
#endif
    return true;
}

uint32_t VisibleLTLStubbornSet::next() {
    while (!_ordering.empty()) {
        _current = _ordering.front();
        _ordering.pop_front();
        if (_stubborn[_current] && _enabled[_current]) {
            return _current;
        } else {
            _skipped.push_back(_current);
        }
    }
    reset();
    return std::numeric_limits<uint32_t>::max();
}

void VisibleLTLStubbornSet::find_key_transition() {
    // try to find invisible key transition first
    assert(!_ordering.empty());
    auto tkey = _ordering.front();
    if (_visible[tkey]) {
        for (uint32_t tid = 0; tid < _net.number_of_transitions(); ++tid) {
            if (_enabled[tid] && !_visible[tid]) {
                tkey = tid;
                break;
            }
        }
    }
    add_to_stub(tkey);

    // include relevant transitions
    auto ptr = transitions()[tkey];
    uint32_t finv = ptr._inputs;
    uint32_t linv = ptr._outputs;

    for (; finv < linv; ++finv) {
        auto inv = invariants()[finv];
        postset_of(inv._place, true);
    }
}

constexpr bool isRuleVPrime = true;
void VisibleLTLStubbornSet::ensure_rule_v() {
    // Rule V: If there is an enabled, visible transition in the stubborn set,
    // all visible transitions must be stubborn.
    // Rule V' (implemented): If there is an enabled, visible transition
    // in the stubborn set, then T_s(s) = T.
    bool visibleStubborn = false;
    for (uint32_t tid = 0; tid < _net.number_of_transitions(); ++tid) {
        if (_stubborn[tid] && _enabled[tid] && _visible[tid]) {
            visibleStubborn = true;
            break;
        }
    }
    if (!visibleStubborn)
        return;
    else {
        memset(_stubborn.get(), true, _net.number_of_transitions());
    }
    // following block would implement rule V
    /*
    static_assert(!isRuleVPrime);
    for (uint32_t tid = 0; tid < _net.number_of_transitions(); ++tid) {
        if (_visible[tid]) addToStub(tid);
    }
    closure();*/
}

void VisibleLTLStubbornSet::ensure_rules_l() {
    static_assert(isRuleVPrime, "Plain rule V does not imply L1");
}

void VisibleLTLStubbornSet::reset() {
    StubbornSet::reset();
    _skipped.clear();
    _has_enabled_stubborn = false;
}

bool VisibleLTLStubbornSet::generate_all(const LTL::Structures::ProductState &parent) {
    prepare(parent);
    // Ensure rule L2, forcing all visible transitions into the stubborn set when closing cycle.
    for (uint32_t i = 0; i < _net.number_of_transitions(); ++i) {
        if (_visible[i]) {
            add_to_stub(i);
        }
    }
    // recompute entire set
    closure();
    if (!_has_enabled_stubborn) {
        memset(_stubborn.get(), 1, _net.number_of_transitions());
    }
    return true;
    /*
    // re-add previously non-stubborn, enabled transitions to order if they are now stubborn.
    while (!_skipped.empty()) {
        auto tid = _skipped.front();
        if (_stubborn[tid])
            _ordering.push_back(tid);
        _skipped.pop_front();
    }*/
}

void VisibleLTLStubbornSet::add_to_stub(uint32_t t) {
    if (_enabled[t])
        _has_enabled_stubborn = true;
    StubbornSet::add_to_stub(t);
}

} // namespace LTL