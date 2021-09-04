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

#include "LTL/Simplification/SpotToPQL.h"
#include "LTL/LTLToBuchi.h"
#include <algorithm>
#include <map>
#include <memory>

#include <spot/tl/formula.hh>
#include <spot/tl/simplify.hh>
#include <spot/tl/unabbrev.hh>

namespace LTL {
    using namespace PetriEngine::PQL;
    PetriEngine::PQL::Condition_ptr to_PQL(const spot::formula &formula, const APInfo &apinfo) {

        switch (formula.kind()) {
            case spot::op::ff:
                return BooleanCondition::FALSE_CONSTANT;
            case spot::op::tt:
                return BooleanCondition::TRUE_CONSTANT;
            case spot::op::ap: {
                auto it = std::find_if(std::begin(apinfo), std::end(apinfo),
                                       [&](const AtomicProposition &info) {
                                           auto ap = std::string_view(info._text);
                                           return ap == std::string_view(formula.ap_name());
                                       });
                if (it == std::end(apinfo)) {
                    std::cerr << "Error: Expected to find " << formula.ap_name() << " in APInfo.\n";
                    exit(EXIT_FAILURE);
                } else {
                    return it->_expression;
                }

            }
            case spot::op::Not:
                return std::make_shared<NotCondition>(to_PQL(formula[0], apinfo));
            case spot::op::X:
                return std::make_shared<XCondition>(to_PQL(formula[0], apinfo));
            case spot::op::F:
                return std::make_shared<FCondition>(to_PQL(formula[0], apinfo));
            case spot::op::G:
                return std::make_shared<GCondition>(to_PQL(formula[0], apinfo));
            case spot::op::U:
                return std::make_shared<UntilCondition>(
                        to_PQL(formula[0], apinfo), to_PQL(formula[1], apinfo));
            case spot::op::Or: {
                std::vector<Condition_ptr> conds;
                std::transform(std::begin(formula), std::end(formula), std::back_insert_iterator(conds),
                               [&](auto f) { return to_PQL(f, apinfo); });
                return std::make_shared<OrCondition>(conds);
            }
            case spot::op::And: {
                std::vector<Condition_ptr> conds;
                std::transform(std::begin(formula), std::end(formula), std::back_insert_iterator(conds),
                               [&](auto f) { return to_PQL(f, apinfo); });
                return std::make_shared<AndCondition>(conds);
            }
            case spot::op::R:
                std::cerr << "Unimplemented: R" << std::endl;
                exit(0);
            case spot::op::W:
                std::cerr << "Unimplemented: W" << std::endl;
                exit(0);
            case spot::op::M:
                std::cerr << "Unimplemented: M" << std::endl;
                exit(0);
            case spot::op::eword:
                std::cerr << "Unimplemented: eword" << std::endl;
                exit(0);
            case spot::op::Closure:
                std::cerr << "Unimplemented: Closure" << std::endl;
                exit(0);
            case spot::op::NegClosure:
                std::cerr << "Unimplemented: NegClosure" << std::endl;
                exit(0);
            case spot::op::NegClosureMarked:
                std::cerr << "Unimplemented: NegClosureMarked" << std::endl;
                exit(0);
            case spot::op::Xor:
                std::cerr << "Unimplemented: Xor" << std::endl;
                exit(0);
            case spot::op::Implies:
                std::cerr << "Unimplemented: Implies" << std::endl;
                exit(0);
            case spot::op::Equiv:
                std::cerr << "Unimplemented: Equiv" << std::endl;
                exit(0);
            case spot::op::EConcat:
                std::cerr << "Unimplemented: EConcat" << std::endl;
                exit(0);
            case spot::op::EConcatMarked:
                std::cerr << "Unimplemented: EConcatMarked" << std::endl;
                exit(0);
            case spot::op::UConcat:
                std::cerr << "Unimplemented: UConcat" << std::endl;
                exit(0);
            case spot::op::OrRat:
                std::cerr << "Unimplemented: OrRat" << std::endl;
                exit(0);
            case spot::op::AndRat:
                std::cerr << "Unimplemented: AndRat" << std::endl;
                exit(0);
            case spot::op::AndNLM:
                std::cerr << "Unimplemented: AndNLM" << std::endl;
                exit(0);
            case spot::op::Concat:
                std::cerr << "Unimplemented: Concat" << std::endl;
                exit(0);
            case spot::op::Fusion:
                std::cerr << "Unimplemented: Fusion" << std::endl;
                exit(0);

            case spot::op::Star:
                std::cerr << "Unimplemented: Star" << std::endl;
                exit(0);

            case spot::op::FStar:
                std::cerr << "Unimplemented: FStar" << std::endl;
                exit(0);
            case spot::op::first_match:
                std::cerr << "Unimplemented: first_match" << std::endl;
                exit(0);
            default:
                std::cerr << "Found unrecognized op in formula ";
                formula.dump(std::cerr) << std::endl;
                exit(0);
        }
    }

    PetriEngine::PQL::Condition_ptr simplify(const PetriEngine::PQL::Condition_ptr &formula, const options_t &options) {
        using namespace PetriEngine::PQL;
        if (auto e = std::dynamic_pointer_cast<ECondition>(formula); e != nullptr) {
            return std::make_shared<ECondition>(simplify((*e)[0], options));
        } else if (auto a = std::dynamic_pointer_cast<ACondition>(formula); a != nullptr) {
            return std::make_shared<ACondition>(simplify((*a)[0], options));
        }
        auto[f, apinfo] = LTL::to_spot_formula(formula, options);
        spot::tl_simplifier simplifier{static_cast<int>(options._buchi_optimization)};
        f = simplifier.simplify(f);
        // spot simplifies using unsupported operators R, W, and M, which we now remove.
        f = spot::unabbreviate(f, "RWM");
        return to_PQL(f, apinfo);
    }
} // namespace LTL
