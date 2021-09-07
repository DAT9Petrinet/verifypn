/* PeTe - Petri Engine exTremE
 * Copyright (C) 2011  Jonas Finnemann Jensen <jopsen@gmail.com>,
 *                     Thomas Søndersø Nielsen <primogens@gmail.com>,
 *                     Lars Kærlund Østergaard <larsko@gmail.com>,
 *                     Peter Gjøl Jensen <root@petergjoel.dk>
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
#include "PetriEngine/PQL/Contexts.h"
#include "PetriEngine/PQL/Expressions.h"
#include "errorcodes.h"
#include "PetriEngine/PQL/Visitor.h"
#include "PetriEngine/PQL/MutatingVisitor.h"

#include <sstream>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <set>
#include <cmath>
#include <numeric>
#include <PetriEngine/Stubborn/StubbornSet.h>
#include "PetriEngine/PQL/QueryPrinter.h"

using namespace PetriEngine::Simplification;

namespace PetriEngine {
    namespace PQL {

        std::ostream& generateTabs(std::ostream& out, uint32_t tabs) {

            for(uint32_t i = 0; i < tabs; i++) {
                out << "  ";
            }
            return out;
        }

        /** FOR COMPILING AND CONSTRUCTING LOGICAL OPERATORS **/

        template<typename T>
        void tryMerge(std::vector<Condition_ptr>& _conds, const Condition_ptr& ptr, bool aggressive = false)
        {
            if(auto lor = std::dynamic_pointer_cast<T>(ptr))
            {
                for(auto& c : *lor) tryMerge<T>(_conds, c, aggressive);
            }
            else if (!aggressive)
            {
                _conds.emplace_back(ptr);
            }
            else if (auto comp = std::dynamic_pointer_cast<CompareCondition>(ptr))
            {
                if((std::is_same<T, AndCondition>::value && std::dynamic_pointer_cast<NotEqualCondition>(ptr)) ||
                   (std::is_same<T, OrCondition>::value && std::dynamic_pointer_cast<EqualCondition>(ptr)))
                {
                    _conds.emplace_back(ptr);
                }
                else
                {
                    if(! ((dynamic_cast<UnfoldedIdentifierExpr*>((*comp)[0].get()) && (*comp)[1]->place_free()) ||
                          (dynamic_cast<UnfoldedIdentifierExpr*>((*comp)[1].get()) && (*comp)[0]->place_free())))
                    {
                        _conds.emplace_back(ptr);
                        return;
                    }

                    std::vector<Condition_ptr> cnds{ptr};
                    auto cmp = std::make_shared<CompareConjunction>(cnds, std::is_same<T, OrCondition>::value);
                    tryMerge<T>(_conds, cmp, aggressive);
                }
            }
            else if (auto conj = std::dynamic_pointer_cast<CompareConjunction>(ptr))
            {
                if((std::is_same<T, OrCondition>::value  && ( conj->isNegated() || conj->singular())) ||
                   (std::is_same<T, AndCondition>::value && (!conj->isNegated() || conj->singular())))
                {
                    if(auto lc = std::dynamic_pointer_cast<CompareConjunction>(_conds.size() == 0 ? nullptr : _conds[0]))
                    {
                        if(lc->isNegated() == std::is_same<T, OrCondition>::value)
                        {
                            auto cpy = std::make_shared<CompareConjunction>(*lc);
                            cpy->merge(*conj);
                            _conds[0] = cpy;
                        }
                        else
                        {
                            if(conj->isNegated() == std::is_same<T, OrCondition>::value)
                                _conds.insert(_conds.begin(), conj);
                            else
                            {
                                auto next = std::make_shared<CompareConjunction>(std::is_same<T, OrCondition>::value);
                                next->merge(*conj);
                                _conds.insert(_conds.begin(), next);
                            }
                        }
                    }
                    else
                    {
                        _conds.insert(_conds.begin(), conj);
                    }
                }
                else
                {
                    _conds.emplace_back(ptr);
                }
            }
            else
            {
                _conds.emplace_back(ptr);
            }

        }

        template<typename T, bool K>
        Condition_ptr makeLog(const std::vector<Condition_ptr>& conds, bool aggressive)
        {
            if(conds.size() == 0)
                return BooleanCondition::getShared(K);
            if(conds.size() == 1) return conds[0];

            std::vector<Condition_ptr> cnds;
            for(auto& c : conds) tryMerge<T>(cnds, c, aggressive);
            auto res = std::make_shared<T>(cnds);
            if(res->singular()) return *res->begin();
            if(res->empty())
                return BooleanCondition::getShared(K);
            return res;
        }

        Condition_ptr makeOr(const std::vector<Condition_ptr>& cptr)
        { return makeLog<OrCondition,false>(cptr, true); }
        Condition_ptr makeAnd(const std::vector<Condition_ptr>& cptr)
        { return makeLog<AndCondition,true>(cptr, true); }
        Condition_ptr makeOr(const Condition_ptr& a, const Condition_ptr& b) {
            std::vector<Condition_ptr> cnds{a,b};
            return makeLog<OrCondition,false>(cnds, true);
        }
        Condition_ptr makeAnd(const Condition_ptr& a, const Condition_ptr& b)
        {
            std::vector<Condition_ptr> cnds{a,b};
            return makeLog<AndCondition,true>(cnds, true);
        }


        // CONSTANTS
        Condition_ptr BooleanCondition::FALSE_CONSTANT = std::make_shared<BooleanCondition>(false);
        Condition_ptr BooleanCondition::TRUE_CONSTANT = std::make_shared<BooleanCondition>(true);
        Condition_ptr DeadlockCondition::DEADLOCK = std::make_shared<DeadlockCondition>();


        Condition_ptr BooleanCondition::getShared(bool val)
        {
            if(val)
            {
                return TRUE_CONSTANT;
            }
            else
            {
                return FALSE_CONSTANT;
            }
        }

        /******************** To TAPAAL Query ********************/

        void SimpleQuantifierCondition::to_tapaal_query(std::ostream& out,TAPAALConditionExportContext& context) const {
            out << op() << " ";
            _cond->to_tapaal_query(out,context);
        }

        void UntilCondition::to_tapaal_query(std::ostream& out,TAPAALConditionExportContext& context) const {
            out << op() << " (";
            _cond1->to_tapaal_query(out, context);
            out << " U ";
            _cond2->to_tapaal_query(out,context);
            out << ")";
        }

        void LogicalCondition::to_tapaal_query(std::ostream& out,TAPAALConditionExportContext& context) const {
            out << "(";
            _conds[0]->to_tapaal_query(out, context);
            for(size_t i = 1; i < _conds.size(); ++i)
            {
                out << " " << op() << " ";
                _conds[i]->to_tapaal_query(out, context);
            }
            out << ")";
        }

        void CompareConjunction::to_tapaal_query(std::ostream& out,TAPAALConditionExportContext& context) const {
            out << "(";
            if(_negated) out << "!";
            bool first = true;
            for(auto& c : _constraints)
            {
                if(!first) out << " and ";
                if(c._lower != 0)
                    out << "(" << c._lower << " <= " << context._netName << "." << c._name << ")";
                if(c._lower != 0 && c._upper != std::numeric_limits<uint32_t>::max())
                    out << " and ";
                if(c._lower != 0)
                    out << "(" << c._upper << " >= " << context._netName << "." << c._name << ")";
                first = false;
            }
            out << ")";
        }

        void CompareCondition::to_tapaal_query(std::ostream& out,TAPAALConditionExportContext& context) const {
            //If <id> <op> <literal>
            QueryPrinter printer;
            if (_expr1->type() == Expr::IdentifierExpr && _expr2->type() == Expr::LiteralExpr) {
                out << " ( " << context._netName << ".";
                _expr1->visit(printer);
                out << " " << opTAPAAL() << " ";
                _expr2->visit(printer);
                out << " ) ";
                //If <literal> <op> <id>
            } else if (_expr2->type() == Expr::IdentifierExpr && _expr1->type() == Expr::LiteralExpr) {
                out << " ( ";
                _expr1->visit(printer);
                out << " " << sopTAPAAL() << " " << context._netName << ".";
                _expr2->visit(printer);
                out << " ) ";
            } else {
                context._failed = true;
                out << " false ";
            }
        }

        void NotEqualCondition::to_tapaal_query(std::ostream& out,TAPAALConditionExportContext& context) const {
            out << " !( ";
            CompareCondition::to_tapaal_query(out,context);
            out << " ) ";
        }

        void NotCondition::to_tapaal_query(std::ostream& out,TAPAALConditionExportContext& context) const {
            out << " !( ";
            _cond->to_tapaal_query(out,context);
            out << " ) ";
        }

        void BooleanCondition::to_tapaal_query(std::ostream& out,TAPAALConditionExportContext&) const {
            if (value)
                out << "true";
            else
                out << "false";
        }

        void DeadlockCondition::to_tapaal_query(std::ostream& out,TAPAALConditionExportContext&) const {
            out << "deadlock";
        }

        void UnfoldedUpperBoundsCondition::to_tapaal_query(std::ostream& out, TAPAALConditionExportContext&) const {
            out << "bounds (";
            for(size_t i = 0; i < _places.size(); ++i)
            {
                if(i != 0) out << ", ";
                out << _places[i]._name;
            }
            out << ")";
        }

        /******************** opTAPAAL ********************/

        std::string EqualCondition::opTAPAAL() const {
            return "=";
        }

        std::string NotEqualCondition::opTAPAAL() const {
            return "=";
        } //Handled with hack in NotEqualCondition::toTAPAALQuery

        std::string LessThanCondition::opTAPAAL() const {
            return "<";
        }

        std::string LessThanOrEqualCondition::opTAPAAL() const {
            return "<=";
        }

        std::string EqualCondition::sopTAPAAL() const {
            return "=";
        }

        std::string NotEqualCondition::sopTAPAAL() const {
            return "=";
        } //Handled with hack in NotEqualCondition::toTAPAALQuery

        std::string LessThanCondition::sopTAPAAL() const {
            return ">=";
        }

        std::string LessThanOrEqualCondition::sopTAPAAL() const {
            return ">";
        }

        /******************** Context Analysis ********************/

        void NaryExpr::analyze(AnalysisContext& context) {
            for(auto& e : _exprs) e->analyze(context);
        }

        void CommutativeExpr::analyze(AnalysisContext& context) {
            for(auto& i : _ids)
            {
                AnalysisContext::ResolutionResult result = context.resolve(i.second);
                if (result._success) {
                    i.first = result._offset;
                } else {
                    ExprError error("Unable to resolve identifier \"" + i.second + "\"",
                            i.second.length());
                    context.report_error(error);
                }
            }
            NaryExpr::analyze(context);
            std::sort(_ids.begin(), _ids.end(), [](auto& a, auto& b){ return a.first < b.first; });
            std::sort(_exprs.begin(), _exprs.end(), [](auto& a, auto& b)
            {
                auto ida = std::dynamic_pointer_cast<PQL::UnfoldedIdentifierExpr>(a);
                auto idb = std::dynamic_pointer_cast<PQL::UnfoldedIdentifierExpr>(b);
                if(ida == nullptr) return false;
                if(ida && !idb) return true;
                return ida->offset() < idb->offset();
            });
        }

        void MinusExpr::analyze(AnalysisContext& context) {
            _expr->analyze(context);
        }

        void LiteralExpr::analyze(AnalysisContext&) {
            return;
        }

        uint32_t getPlace(AnalysisContext& context, const std::string& name)
        {
            AnalysisContext::ResolutionResult result = context.resolve(name);
            if (result._success) {
                return result._offset;
            } else {
                ExprError error("Unable to resolve identifier \"" + name + "\"",
                                name.length());
                context.report_error(error);
            }
            return -1;
        }

        Expr_ptr generateUnfoldedIdentifierExpr(ColoredAnalysisContext& context, std::unordered_map<uint32_t,std::string>& names, uint32_t colorIndex) {
            std::string& place = names[colorIndex];
            return std::make_shared<UnfoldedIdentifierExpr>(place, getPlace(context, place));
        }

        void IdentifierExpr::analyze(AnalysisContext &context) {
            if (_compiled) {
                _compiled->analyze(context);
                return;
            }

            auto coloredContext = dynamic_cast<ColoredAnalysisContext*>(&context);
            if(coloredContext != nullptr && coloredContext->is_colored())
            {
                std::unordered_map<uint32_t,std::string> names;
                if (!coloredContext->resolve_place(_name, names)) {
                    ExprError error("Unable to resolve colored identifier \"" + _name + "\"", _name.length());
                    coloredContext->report_error(error);
                }

                if (names.size() == 1) {
                    _compiled = generateUnfoldedIdentifierExpr(*coloredContext, names, names.begin()->first);
                } else {
                    std::vector<Expr_ptr> identifiers;
                    for (auto& unfoldedName : names) {
                        identifiers.push_back(generateUnfoldedIdentifierExpr(*coloredContext,names,unfoldedName.first));
                    }
                    _compiled = std::make_shared<PQL::PlusExpr>(std::move(identifiers));
                }
            } else {
                _compiled = std::make_shared<UnfoldedIdentifierExpr>(_name, getPlace(context, _name));
            }
            _compiled->analyze(context);
        }

        void UnfoldedIdentifierExpr::analyze(AnalysisContext& context) {
            AnalysisContext::ResolutionResult result = context.resolve(_name);
            if (result._success) {
                _offsetInMarking = result._offset;
            } else {
                ExprError error("Unable to resolve identifier \"" + _name + "\"",
                        _name.length());
                context.report_error(error);
            }
        }

        void SimpleQuantifierCondition::analyze(AnalysisContext& context) {
            _cond->analyze(context);
        }

        void UntilCondition::analyze(AnalysisContext& context) {
            _cond1->analyze(context);
            _cond2->analyze(context);
        }

        void LogicalCondition::analyze(AnalysisContext& context) {
            for(auto& c : _conds) c->analyze(context);
        }

        void UnfoldedFireableCondition::_analyze(AnalysisContext& context)
        {
            std::vector<Condition_ptr> conds;
            AnalysisContext::ResolutionResult result = context.resolve(_name, false);
            if (!result._success)
            {
                ExprError error("Unable to resolve identifier \"" + _name + "\"",
                        _name.length());
                context.report_error(error);
                return;
            }

            assert(_name.compare(context.net().transition_names()[result._offset]) == 0);
            auto preset = context.net().preset(result._offset);
            for(; preset.first != preset.second; ++preset.first)
            {
                assert(preset.first->_place != std::numeric_limits<uint32_t>::max());
                assert(preset.first->_place != -1);
                auto id = std::make_shared<UnfoldedIdentifierExpr>(context.net().place_names()[preset.first->_place], preset.first->_place);
                auto lit = std::make_shared<LiteralExpr>(preset.first->_tokens);

                if(!preset.first->_inhibitor)
                {
                    conds.emplace_back(std::make_shared<LessThanOrEqualCondition>(lit, id));
                }
                else if(preset.first->_tokens > 0)
                {
                    conds.emplace_back(std::make_shared<LessThanCondition>(id, lit));
                }
            }
            if(conds.size() == 1) _compiled = conds[0];
            else _compiled = std::make_shared<AndCondition>(conds);
            _compiled->analyze(context);
        }

        void FireableCondition::_analyze(AnalysisContext &context) {

            auto coloredContext = dynamic_cast<ColoredAnalysisContext*>(&context);
            if(coloredContext != nullptr && coloredContext->is_colored()) {
                std::vector<std::string> names;
                if (!coloredContext->resolve_transition(_name, names)) {
                    ExprError error("Unable to resolve colored identifier \"" + _name + "\"", _name.length());
                    coloredContext->report_error(error);
                    return;
                }
                if(names.size() < 1){
                    //If the transition points to empty vector we know that it has
                    //no legal bindings and can never fire
                    _compiled = std::make_shared<BooleanCondition>(false);
                    _compiled->analyze(context);
                    return;
                }
                if (names.size() == 1) {
                    _compiled = std::make_shared<UnfoldedFireableCondition>(names[0]);
                } else {
                    std::vector<Condition_ptr> identifiers;
                    for (auto& unfoldedName : names) {
                        identifiers.push_back(std::make_shared<UnfoldedFireableCondition>(unfoldedName));
                    }
                    _compiled = std::make_shared<OrCondition>(std::move(identifiers));
                }
            } else {
                _compiled = std::make_shared<UnfoldedFireableCondition>(_name);
            }
            _compiled->analyze(context);
        }

        void CompareConjunction::analyze(AnalysisContext& context) {
            for(auto& c : _constraints){
                c._place = getPlace(context, c._name);
                assert(c._place >= 0);
            }
            std::sort(std::begin(_constraints), std::end(_constraints));
        }

        void CompareCondition::analyze(AnalysisContext& context) {
            _expr1->analyze(context);
            _expr2->analyze(context);
        }

        void NotCondition::analyze(AnalysisContext& context) {
            _cond->analyze(context);
        }

        void BooleanCondition::analyze(AnalysisContext&) {
        }

        void DeadlockCondition::analyze(AnalysisContext& c) {
            c.set_has_deadlock();
        }

        void KSafeCondition::_analyze(AnalysisContext &context) {
            auto coloredContext = dynamic_cast<ColoredAnalysisContext*>(&context);
            std::vector<Condition_ptr> k_safe;
            if(coloredContext != nullptr && coloredContext->is_colored())
            {
                for(auto& p : coloredContext->all_colored_place_names())
                    k_safe.emplace_back(std::make_shared<LessThanOrEqualCondition>(std::make_shared<IdentifierExpr>(p.first), _bound));
            }
            else
            {
                for(auto& p : context.all_place_names())
                    k_safe.emplace_back(std::make_shared<LessThanOrEqualCondition>(std::make_shared<UnfoldedIdentifierExpr>(p.first), _bound));
            }
            _compiled = std::make_shared<AGCondition>(std::make_shared<AndCondition>(std::move(k_safe)));
            _compiled->analyze(context);
        }

        void QuasiLivenessCondition::_analyze(AnalysisContext &context)
        {
            auto coloredContext = dynamic_cast<ColoredAnalysisContext*>(&context);
            std::vector<Condition_ptr> quasi;
            if(coloredContext != nullptr && coloredContext->is_colored())
            {
                for(auto& n : coloredContext->all_colored_transition_names())
                {
                    std::vector<Condition_ptr> disj;
                    for(auto& tn : n.second)
                        disj.emplace_back(std::make_shared<UnfoldedFireableCondition>(tn));
                    quasi.emplace_back(std::make_shared<EFCondition>(std::make_shared<OrCondition>(std::move(disj))));
                }
            }
            else
            {
                for(auto& n : context.all_transition_names())
                {
                    quasi.emplace_back(std::make_shared<EFCondition>(std::make_shared<UnfoldedFireableCondition>(n.first)));
                }
            }
            _compiled = std::make_shared<AndCondition>(std::move(quasi));
            _compiled->analyze(context);
        }

        void LivenessCondition::_analyze(AnalysisContext &context)
        {
            auto coloredContext = dynamic_cast<ColoredAnalysisContext*>(&context);
            std::vector<Condition_ptr> liveness;
            if(coloredContext != nullptr && coloredContext->is_colored())
            {
                for(auto& n : coloredContext->all_colored_transition_names())
                {
                    std::vector<Condition_ptr> disj;
                    for(auto& tn : n.second)
                        disj.emplace_back(std::make_shared<UnfoldedFireableCondition>(tn));
                    liveness.emplace_back(std::make_shared<AGCondition>(std::make_shared<EFCondition>(std::make_shared<OrCondition>(std::move(disj)))));
                }
            }
            else
            {
                for(auto& n : context.all_transition_names())
                {
                    liveness.emplace_back(std::make_shared<AGCondition>(std::make_shared<EFCondition>(std::make_shared<UnfoldedFireableCondition>(n.first))));
                }
            }
            _compiled = std::make_shared<AndCondition>(std::move(liveness));
            _compiled->analyze(context);
        }

        void StableMarkingCondition::_analyze(AnalysisContext &context)
        {
            auto coloredContext = dynamic_cast<ColoredAnalysisContext*>(&context);
            std::vector<Condition_ptr> stable_check;
            if(coloredContext != nullptr && coloredContext->is_colored())
            {
                for(auto& cpn : coloredContext->all_colored_place_names())
                {
                    std::vector<Expr_ptr> sum;
                    MarkVal init_marking = 0;
                    for(auto& pn : cpn.second)
                    {
                        auto id = std::make_shared<UnfoldedIdentifierExpr>(pn.second);
                        id->analyze(context);
                        init_marking += context.net().initial(id->offset());
                        sum.emplace_back(std::move(id));

                    }
                    stable_check.emplace_back(std::make_shared<AGCondition>(std::make_shared<EqualCondition>(
                            std::make_shared<PlusExpr>(std::move(sum)),
                            std::make_shared<LiteralExpr>(init_marking))));
                }
            }
            else
            {
                size_t i = 0;
                for(auto& p : context.net().place_names())
                {
                    stable_check.emplace_back(std::make_shared<AGCondition>(std::make_shared<EqualCondition>(
                            std::make_shared<UnfoldedIdentifierExpr>(p, i),
                            std::make_shared<LiteralExpr>(context.net().initial(i)))));
                    ++i;
                }
            }
            _compiled = std::make_shared<OrCondition>(std::move(stable_check));
            _compiled->analyze(context);
        }

        void UpperBoundsCondition::_analyze(AnalysisContext& context)
        {
            auto coloredContext = dynamic_cast<ColoredAnalysisContext*>(&context);
            if(coloredContext != nullptr && coloredContext->is_colored())
            {
                std::vector<std::string> uplaces;
                for(auto& p : _places)
                {
                    std::unordered_map<uint32_t,std::string> names;
                    if (!coloredContext->resolve_place(p, names)) {
                        ExprError error("Unable to resolve colored identifier \"" + p + "\"", p.length());
                        coloredContext->report_error(error);
                    }

                    for(auto& id : names)
                    {
                        uplaces.push_back(names[id.first]);
                    }
                }
                _compiled = std::make_shared<UnfoldedUpperBoundsCondition>(uplaces);
            } else {
                _compiled = std::make_shared<UnfoldedUpperBoundsCondition>(_places);
            }
            _compiled->analyze(context);
        }

        void UnfoldedUpperBoundsCondition::analyze(AnalysisContext& c)
        {
            for(auto& p : _places)
            {
                AnalysisContext::ResolutionResult result = c.resolve(p._name);
                if (result._success) {
                    p._place = result._offset;
                } else {
                    ExprError error("Unable to resolve identifier \"" + p._name + "\"",
                            p._name.length());
                    c.report_error(error);
                }
            }
            std::sort(_places.begin(), _places.end());
        }

        /******************** Evaluation ********************/

        int NaryExpr::evaluate(const EvaluationContext& context) {
            int32_t r = preOp(context);
            for(size_t i = 1; i < _exprs.size(); ++i)
            {
                r = apply(r, _exprs[i]->eval_and_set(context));
            }
            return r;
        }

        int32_t NaryExpr::preOp(const EvaluationContext& context) const {
            return _exprs[0]->evaluate(context);
        }

        int32_t CommutativeExpr::preOp(const EvaluationContext& context) const {
            int32_t res = _constant;
            for(auto& i : _ids) res = this->apply(res, context.marking()[i.first]);
            if(_exprs.size() > 0) res = this->apply(res, _exprs[0]->eval_and_set(context));
            return res;
        }

        int CommutativeExpr::evaluate(const EvaluationContext& context) {
            if(_exprs.size() == 0) return preOp(context);
            return NaryExpr::evaluate(context);
        }

        int MinusExpr::evaluate(const EvaluationContext& context) {
            return -(_expr->evaluate(context));
        }

        int LiteralExpr::evaluate(const EvaluationContext&) {
            return _value;
        }

        int UnfoldedIdentifierExpr::evaluate(const EvaluationContext& context) {
            assert(_offsetInMarking != -1);
            return context.marking()[_offsetInMarking];
        }

        Condition::Result SimpleQuantifierCondition::evaluate(const EvaluationContext& context) {
	    return RUNKNOWN;
        }

        Condition::Result EGCondition::evaluate(const EvaluationContext& context) {
            if(_cond->evaluate(context) == RFALSE) return RFALSE;
            return RUNKNOWN;
        }

        Condition::Result AGCondition::evaluate(const EvaluationContext& context)
        {
            if(_cond->evaluate(context) == RFALSE) return RFALSE;
            return RUNKNOWN;
        }

        Condition::Result EFCondition::evaluate(const EvaluationContext& context) {
            if(_cond->evaluate(context) == RTRUE) return RTRUE;
            return RUNKNOWN;
        }

        Condition::Result AFCondition::evaluate(const EvaluationContext& context) {
            if(_cond->evaluate(context) == RTRUE) return RTRUE;
            return RUNKNOWN;
        }

        Condition::Result ACondition::evaluate(const EvaluationContext& context) {
            //if (_cond->evaluate(context) == RFALSE) return RFALSE;
            return RUNKNOWN;
        }

        Condition::Result ECondition::evaluate(const EvaluationContext& context) {
            //if (_cond->evaluate(context) == RTRUE) return RTRUE;
            return RUNKNOWN;
        }

        Condition::Result FCondition::evaluate(const EvaluationContext& context) {
            //if (_cond->evaluate(context) == RTRUE) return RTRUE;
            return RUNKNOWN;
        }

        Condition::Result GCondition::evaluate(const EvaluationContext& context) {
            //if (_cond->evaluate(context) == RFALSE) return RFALSE;
            return RUNKNOWN;
        }

/*        Condition::Result XCondition::evaluate(const EvaluationContext& context) {
            return _cond->evaluate(context);
        }*/

        Condition::Result UntilCondition::evaluate(const EvaluationContext& context) {
            auto r2 = _cond2->evaluate(context);
            if(r2 != RFALSE) return r2;
            auto r1 = _cond1->evaluate(context);
            if(r1 == RFALSE)
            {
                return RFALSE;
            }
            return RUNKNOWN;
        }



        Condition::Result AndCondition::evaluate(const EvaluationContext& context) {
            auto res = RTRUE;
            for(auto& c : _conds)
            {
                auto r = c->evaluate(context);
                if(r == RFALSE) return RFALSE;
                else if(r == RUNKNOWN) res = RUNKNOWN;
            }
            return res;
        }

        Condition::Result OrCondition::evaluate(const EvaluationContext& context) {
            auto res = RFALSE;
            for(auto& c : _conds)
            {
                auto r = c->evaluate(context);
                if(r == RTRUE) return RTRUE;
                else if(r == RUNKNOWN) res = RUNKNOWN;
            }
            return res;
        }

        Condition::Result CompareConjunction::evaluate(const EvaluationContext& context){
//            auto rres = _org->evaluate(context);
            bool res = true;
            for(auto& c : _constraints)
            {
                res = res && context.marking()[c._place] <= c._upper &&
                             context.marking()[c._place] >= c._lower;
                if(!res) break;
            }
            return (_negated xor res) ? RTRUE : RFALSE;
        }

        Condition::Result CompareCondition::evaluate(const EvaluationContext& context) {
            int v1 = _expr1->evaluate(context);
            int v2 = _expr2->evaluate(context);
            return apply(v1, v2) ? RTRUE : RFALSE;
        }

        Condition::Result NotCondition::evaluate(const EvaluationContext& context) {
            auto res = _cond->evaluate(context);
            if(res != RUNKNOWN) return res == RFALSE ? RTRUE : RFALSE;
            return RUNKNOWN;
        }

        Condition::Result BooleanCondition::evaluate(const EvaluationContext&) {
            return value ? RTRUE : RFALSE;
        }

        Condition::Result DeadlockCondition::evaluate(const EvaluationContext& context) {
            if (!context.net())
                return RFALSE;
            if (!context.net()->deadlocked(context.marking())) {
                return RFALSE;
            }
            return RTRUE;
        }

        size_t UnfoldedUpperBoundsCondition::value(const MarkVal* marking)
        {
            size_t tmp = 0;
            for(auto& p : _places)
            {
                auto val = marking[p._place];
                p._maxed_out = (p._max <= val);
                tmp += val;
            }
            return tmp;
        }

        Condition::Result UnfoldedUpperBoundsCondition::evaluate(const EvaluationContext& context) {
            setUpperBound(value(context.marking()));
            return _max <= _bound ? RTRUE : RUNKNOWN;
        }

        /******************** Evaluation - save result ********************/
        Condition::Result SimpleQuantifierCondition::eval_and_set(const EvaluationContext& context) {
	    return RUNKNOWN;
        }

        Condition::Result GCondition::eval_and_set(const EvaluationContext &context) {
            auto res = _cond->eval_and_set(context);
            if(res != RFALSE) res = RUNKNOWN;
            set_satisfied(res);
            return res;
        }

        Condition::Result FCondition::eval_and_set(const EvaluationContext &context) {
            auto res = _cond->eval_and_set(context);
            if(res != RTRUE) res = RUNKNOWN;
            set_satisfied(res);
            return res;
        }

        Condition::Result EGCondition::eval_and_set(const EvaluationContext& context) {
            auto res = _cond->eval_and_set(context);
            if(res != RFALSE) res = RUNKNOWN;
            set_satisfied(res);
            return res;
        }

        Condition::Result AGCondition::eval_and_set(const EvaluationContext& context) {
            auto res = _cond->eval_and_set(context);
            if(res != RFALSE) res = RUNKNOWN;
            set_satisfied(res);
            return res;
        }

        Condition::Result EFCondition::eval_and_set(const EvaluationContext& context) {
            auto res = _cond->eval_and_set(context);
            if(res != RTRUE) res = RUNKNOWN;
            set_satisfied(res);
            return res;
        }

        Condition::Result AFCondition::eval_and_set(const EvaluationContext& context) {
            auto res = _cond->eval_and_set(context);
            if(res != RTRUE) res = RUNKNOWN;
            set_satisfied(res);
            return res;
        }

        Condition::Result UntilCondition::eval_and_set(const EvaluationContext& context) {
            auto r2 = _cond2->eval_and_set(context);
            if(r2 != RFALSE) return r2;
            auto r1 = _cond1->eval_and_set(context);
            if(r1 == RFALSE) return RFALSE;
            return RUNKNOWN;
        }

        int Expr::eval_and_set(const EvaluationContext& context) {
            int r = evaluate(context);
            set_eval(r);
            return r;
        }

        Condition::Result AndCondition::eval_and_set(const EvaluationContext& context) {
            Result res = RTRUE;
            for(auto& c : _conds)
            {
                auto r = c->eval_and_set(context);
                if(r == RFALSE)
                {
                    res = RFALSE;
                    break;
                }
                else if(r == RUNKNOWN)
                {
                    res = RUNKNOWN;
                }
            }
            set_satisfied(res);
            return res;
        }

        Condition::Result OrCondition::eval_and_set(const EvaluationContext& context) {
            Result res = RFALSE;
            for(auto& c : _conds)
            {
                auto r = c->eval_and_set(context);
                if(r == RTRUE)
                {
                    res = RTRUE;
                    break;
                }
                else if(r == RUNKNOWN)
                {
                    res = RUNKNOWN;
                }
            }
            set_satisfied(res);
            return res;
        }

        Condition::Result CompareConjunction::eval_and_set(const EvaluationContext& context)
        {
            auto res = evaluate(context);
            set_satisfied(res);
            return res;
        }

        Condition::Result CompareCondition::eval_and_set(const EvaluationContext& context) {
            int v1 = _expr1->eval_and_set(context);
            int v2 = _expr2->eval_and_set(context);
            bool res = apply(v1, v2);
            set_satisfied(res);
            return res ? RTRUE : RFALSE;
        }

        Condition::Result NotCondition::eval_and_set(const EvaluationContext& context) {
            auto res = _cond->eval_and_set(context);
            if(res != RUNKNOWN) res = res == RFALSE ? RTRUE : RFALSE;
            set_satisfied(res);
            return res;
        }

        Condition::Result BooleanCondition::eval_and_set(const EvaluationContext&) {
            set_satisfied(value);
            return value ? RTRUE : RFALSE;
        }

        Condition::Result DeadlockCondition::eval_and_set(const EvaluationContext& context) {
            if (!context.net())
                return RFALSE;
            set_satisfied(context.net()->deadlocked(context.marking()));
            return is_satisfied() ? RTRUE : RFALSE;
        }

        Condition::Result UnfoldedUpperBoundsCondition::eval_and_set(const EvaluationContext& context)
        {
            auto res = evaluate(context);
            set_satisfied(res);
            return res;
        }

        /******************** Range Contexts ********************/

        void UntilCondition::visit(Visitor &ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void EGCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void EUCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void EXCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void EFCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void AUCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void AXCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void AFCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void AGCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void ACondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void ECondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void GCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void FCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void XCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void AndCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void OrCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void NotCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void EqualCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void NotEqualCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void CompareConjunction::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void LessThanOrEqualCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void LessThanCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void BooleanCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void DeadlockCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void StableMarkingCondition::visit(Visitor& ctx) const
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void QuasiLivenessCondition::visit(Visitor& ctx) const
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void KSafeCondition::visit(Visitor& ctx) const
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void LivenessCondition::visit(Visitor& ctx) const
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void FireableCondition::visit(Visitor& ctx) const
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void UpperBoundsCondition::visit(Visitor& ctx) const
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void UnfoldedFireableCondition::visit(Visitor& ctx) const
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }


        void UnfoldedUpperBoundsCondition::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void LiteralExpr::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void IdentifierExpr::visit(Visitor& ctx) const
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void UnfoldedIdentifierExpr::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void MinusExpr::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void SubtractExpr::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void PlusExpr::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        void MultiplyExpr::visit(Visitor& ctx) const
        {
            ctx.accept<decltype(this)>(this);
        }

        /******************** Mutating visitor **********************************/

        void UntilCondition::visit(MutatingVisitor &ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void EGCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void EUCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void EXCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void EFCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void AUCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void AXCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void AFCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void AGCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void ACondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void ECondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void GCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void FCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void XCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void AndCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void OrCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void NotCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void EqualCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void NotEqualCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void CompareConjunction::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void LessThanOrEqualCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void LessThanCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void BooleanCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void DeadlockCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        void StableMarkingCondition::visit(MutatingVisitor& ctx)
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void QuasiLivenessCondition::visit(MutatingVisitor& ctx)
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void KSafeCondition::visit(MutatingVisitor& ctx)
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void LivenessCondition::visit(MutatingVisitor& ctx)
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void FireableCondition::visit(MutatingVisitor& ctx)
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void UpperBoundsCondition::visit(MutatingVisitor& ctx)
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }

        void UnfoldedFireableCondition::visit(MutatingVisitor& ctx)
        {
            if(_compiled)
                _compiled->visit(ctx);
            else
                ctx.accept<decltype(this)>(this);
        }


        void UnfoldedUpperBoundsCondition::visit(MutatingVisitor& ctx)
        {
            ctx.accept<decltype(this)>(this);
        }

        /******************** Apply (BinaryExpr subclasses) ********************/

        int PlusExpr::apply(int v1, int v2) const {
            return v1 + v2;
        }

        int SubtractExpr::apply(int v1, int v2) const {
            return v1 - v2;
        }

        int MultiplyExpr::apply(int v1, int v2) const {
            return v1 * v2;
        }

        /******************** Apply (CompareCondition subclasses) ********************/

        bool EqualCondition::apply(int v1, int v2) const {
            return v1 == v2;
        }

        bool NotEqualCondition::apply(int v1, int v2) const {
            return v1 != v2;
        }

        bool LessThanCondition::apply(int v1, int v2) const {
            return v1 < v2;
        }

        bool LessThanOrEqualCondition::apply(int v1, int v2) const {
            return v1 <= v2;
        }

        /******************** Op (BinaryExpr subclasses) ********************/

        std::string PlusExpr::op() const {
            return "+";
        }

        std::string SubtractExpr::op() const {
            return "-";
        }

        std::string MultiplyExpr::op() const {
            return "*";
        }

        /******************** Op (QuantifierCondition subclasses) ********************/

        std::string ACondition::op() const {
            return "A";
        }

        std::string ECondition::op() const {
            return "E";
        }

        std::string GCondition::op() const {
            return "G";
        }

        std::string FCondition::op() const {
            return "F";
        }

        std::string XCondition::op() const {
            return "X";
        }

        std::string EXCondition::op() const {
            return "EX";
        }

        std::string EGCondition::op() const {
            return "EG";
        }

        std::string EFCondition::op() const {
            return "EF";
        }

        std::string AXCondition::op() const {
            return "AX";
        }

        std::string AGCondition::op() const {
            return "AG";
        }

        std::string AFCondition::op() const {
            return "AF";
        }

        /******************** Op (UntilCondition subclasses) ********************/

        std::string UntilCondition::op() const {
            return "";
        }

        std::string EUCondition::op() const {
            return "E";
        }

        std::string AUCondition::op() const {
            return "A";
        }

        /******************** Op (LogicalCondition subclasses) ********************/

        std::string AndCondition::op() const {
            return "and";
        }

        std::string OrCondition::op() const {
            return "or";
        }

        /******************** Op (CompareCondition subclasses) ********************/

        std::string EqualCondition::op() const {
            return "==";
        }

        std::string NotEqualCondition::op() const {
            return "!=";
        }

        std::string LessThanCondition::op() const {
            return "<";
        }

        std::string LessThanOrEqualCondition::op() const {
            return "<=";
        }

        /******************** free of places ********************/

        bool NaryExpr::place_free() const
        {
            for(auto& e : _exprs)
                if(!e->place_free())
                    return false;
            return true;
        }

        bool CommutativeExpr::place_free() const
        {
            if(_ids.size() > 0) return false;
            return NaryExpr::place_free();
        }

        bool MinusExpr::place_free() const
        {
            return _expr->place_free();
        }

        /******************** Expr::type() implementation ********************/

        Expr::Types PlusExpr::type() const {
            return Expr::PlusExpr;
        }

        Expr::Types SubtractExpr::type() const {
            return Expr::SubtractExpr;
        }

        Expr::Types MultiplyExpr::type() const {
            return Expr::MinusExpr;
        }

        Expr::Types MinusExpr::type() const {
            return Expr::MinusExpr;
        }

        Expr::Types LiteralExpr::type() const {
            return Expr::LiteralExpr;
        }

        Expr::Types UnfoldedIdentifierExpr::type() const {
            return Expr::IdentifierExpr;
        }

        /******************** Distance Condition ********************/


        template<>
        uint32_t delta<EqualCondition>(int v1, int v2, bool negated) {
            if (!negated)
                return std::abs(v1 - v2);
            else
                return v1 == v2 ? 1 : 0;
        }

        template<>
        uint32_t delta<NotEqualCondition>(int v1, int v2, bool negated) {
            return delta<EqualCondition>(v1, v2, !negated);
        }

        template<>
        uint32_t delta<LessThanCondition>(int v1, int v2, bool negated) {
            if (!negated)
                return v1 < v2 ? 0 : v1 - v2 + 1;
            else
                return v1 >= v2 ? 0 : v2 - v1;
        }

        template<>
        uint32_t delta<LessThanOrEqualCondition>(int v1, int v2, bool negated) {
            if (!negated)
                return v1 <= v2 ? 0 : v1 - v2;
            else
                return v1 > v2 ? 0 : v2 - v1 + 1;
        }

        uint32_t NotCondition::distance(DistanceContext& context) const {
            context.negate();
            uint32_t retval = _cond->distance(context);
            context.negate();
            return retval;
        }

        uint32_t BooleanCondition::distance(DistanceContext& context) const {
            if (context.negated() != value)
                return 0;
            return std::numeric_limits<uint32_t>::max();
        }

        uint32_t DeadlockCondition::distance(DistanceContext& context) const {
            return 0;
        }

        uint32_t UnfoldedUpperBoundsCondition::distance(DistanceContext& context) const
        {
            size_t tmp = 0;
            for(auto& p : _places)
            {
                tmp += context.marking()[p._place];
            }

            return _max - tmp;
        }

        uint32_t EFCondition::distance(DistanceContext& context) const {
            return _cond->distance(context);
        }

        uint32_t EGCondition::distance(DistanceContext& context) const {
            return _cond->distance(context);
        }

        uint32_t EXCondition::distance(DistanceContext& context) const {
            return _cond->distance(context);
        }

        uint32_t EUCondition::distance(DistanceContext& context) const {
            return _cond2->distance(context);
        }

        uint32_t AFCondition::distance(DistanceContext& context) const {
            context.negate();
            uint32_t retval = _cond->distance(context);
            context.negate();
            return retval;
        }

        uint32_t AXCondition::distance(DistanceContext& context) const {
            context.negate();
            uint32_t retval = _cond->distance(context);
            context.negate();
            return retval;
        }

        uint32_t AGCondition::distance(DistanceContext& context) const {
            context.negate();
            uint32_t retval = _cond->distance(context);
            context.negate();
            return retval;
        }

        uint32_t AUCondition::distance(DistanceContext& context) const {
            context.negate();
            auto r1 = _cond1->distance(context);
            auto r2 = _cond2->distance(context);
            context.negate();
            return r1 + r2;
        }

        uint32_t CompareConjunction::distance(DistanceContext& context) const {
            uint32_t d = 0;
            auto neg = context.negated() != _negated;
            if(!neg)
            {
                for(auto& c : _constraints)
                {
                    auto pv = context.marking()[c._place];
                    d += (c._upper == std::numeric_limits<uint32_t>::max() ? 0 : delta<LessThanOrEqualCondition>(pv, c._upper, neg)) +
                         (c._lower == 0 ? 0 : delta<LessThanOrEqualCondition>(c._lower, pv, neg));
                }
            }
            else
            {
                bool first = true;
                for(auto& c : _constraints)
                {
                    auto pv = context.marking()[c._place];
                    if(c._upper != std::numeric_limits<uint32_t>::max())
                    {
                        auto d2 = delta<LessThanOrEqualCondition>(pv, c._upper, neg);
                        if(first) d = d2;
                        else      d = std::min(d, d2);
                        first = false;
                    }

                    if(c._lower != 0)
                    {
                        auto d2 = delta<LessThanOrEqualCondition>(c._upper, pv, neg);
                        if(first) d = d2;
                        else      d = std::min(d, d2);
                        first = false;
                    }
                }
            }
            return d;
        }

        uint32_t conjDistance(DistanceContext& context, const std::vector<Condition_ptr>& conds)
        {
            uint32_t val = 0;
            for(auto& c : conds)
                val += c->distance(context);
            return val;
        }

        uint32_t disjDistance(DistanceContext& context, const std::vector<Condition_ptr>& conds)
        {
            uint32_t val = std::numeric_limits<uint32_t>::max();
            for(auto& c : conds)
                val = std::min(c->distance(context), val);
            return val;
        }

        uint32_t AndCondition::distance(DistanceContext& context) const {
            if(context.negated())
                return disjDistance(context, _conds);
            else
                return conjDistance(context, _conds);
        }

        uint32_t OrCondition::distance(DistanceContext& context) const {
            if(context.negated())
                return conjDistance(context, _conds);
            else
                return disjDistance(context, _conds);
        }


        struct S {
            int d;
            unsigned int p;
        };

        uint32_t LessThanOrEqualCondition::distance(DistanceContext& context) const {
            return _distance(context, delta<LessThanOrEqualCondition>);
        }

        uint32_t LessThanCondition::distance(DistanceContext& context) const {
            return _distance(context, delta<LessThanCondition>);
        }

        uint32_t NotEqualCondition::distance(DistanceContext& context) const {
            return _distance(context, delta<NotEqualCondition>);
        }

        uint32_t EqualCondition::distance(DistanceContext& context) const {
            return _distance(context, delta<EqualCondition>);
        }

        /******************** BIN output ********************/

        void LiteralExpr::to_binary(std::ostream& out) const {
            out.write("l", sizeof(char));
            out.write(reinterpret_cast<const char*>(&_value), sizeof(int));
        }

        void UnfoldedIdentifierExpr::to_binary(std::ostream& out) const {
            out.write("i", sizeof(char));
            out.write(reinterpret_cast<const char*>(&_offsetInMarking), sizeof(int));
        }

        void MinusExpr::to_binary(std::ostream& out) const
        {
            auto e1 = std::make_shared<PQL::LiteralExpr>(0);
            std::vector<Expr_ptr> exprs;
            exprs.push_back(e1);
            exprs.push_back(_expr);
            PQL::SubtractExpr(std::move(exprs)).to_binary(out);
        }

        void SubtractExpr::to_binary(std::ostream& out) const {
            out.write("-", sizeof(char));
            uint32_t size = _exprs.size();
            out.write(reinterpret_cast<const char*>(&size), sizeof(uint32_t));
            for(auto& e : _exprs)
                e->to_binary(out);
        }

        void CommutativeExpr::to_binary(std::ostream& out) const
        {
            auto sop = op();
            out.write(&sop[0], sizeof(char));
            out.write(reinterpret_cast<const char*>(&_constant), sizeof(int32_t));
            uint32_t size = _ids.size();
            out.write(reinterpret_cast<const char*>(&size), sizeof(uint32_t));
            size = _exprs.size();
            out.write(reinterpret_cast<const char*>(&size), sizeof(uint32_t));
            for(auto& id : _ids)
                out.write(reinterpret_cast<const char*>(&id.first), sizeof(uint32_t));
            for(auto& e : _exprs)
                e->to_binary(out);
        }

        void SimpleQuantifierCondition::to_binary(std::ostream& out) const
        {
            auto path = get_path();
            auto quant = get_quantifier();
            out.write(reinterpret_cast<const char*>(&path), sizeof(Path));
            out.write(reinterpret_cast<const char*>(&quant), sizeof(Quantifier));
            _cond->to_binary(out);
        }

        void UntilCondition::to_binary(std::ostream& out) const
        {
            auto path = get_path();
            auto quant = get_quantifier();
            out.write(reinterpret_cast<const char*>(&path), sizeof(Path));
            out.write(reinterpret_cast<const char*>(&quant), sizeof(Quantifier));
            _cond1->to_binary(out);
            _cond2->to_binary(out);
        }

        void LogicalCondition::to_binary(std::ostream& out) const
        {
            auto path = get_path();
            auto quant = get_quantifier();
            out.write(reinterpret_cast<const char*>(&path), sizeof(Path));
            out.write(reinterpret_cast<const char*>(&quant), sizeof(Quantifier));
            uint32_t size = _conds.size();
            out.write(reinterpret_cast<const char*>(&size), sizeof(uint32_t));
            for(auto& c : _conds) c->to_binary(out);
        }

        void CompareConjunction::to_binary(std::ostream& out) const
        {
            auto path = get_path();
            auto quant = Quantifier::COMPCONJ;
            out.write(reinterpret_cast<const char*>(&path), sizeof(Path));
            out.write(reinterpret_cast<const char*>(&quant), sizeof(Quantifier));
            out.write(reinterpret_cast<const char*>(&_negated), sizeof(bool));
            uint32_t size = _constraints.size();
            out.write(reinterpret_cast<const char*>(&size), sizeof(uint32_t));
            for(auto& c : _constraints)
            {
                out.write(reinterpret_cast<const char*>(&c._place), sizeof(int32_t));
                out.write(reinterpret_cast<const char*>(&c._lower), sizeof(uint32_t));
                out.write(reinterpret_cast<const char*>(&c._upper), sizeof(uint32_t));
            }
        }

        void CompareCondition::to_binary(std::ostream& out) const
        {
            auto path = get_path();
            auto quant = get_quantifier();
            out.write(reinterpret_cast<const char*>(&path), sizeof(Path));
            out.write(reinterpret_cast<const char*>(&quant), sizeof(Quantifier));
            std::string sop = op();
            out.write(sop.data(), sop.size());
            out.write("\0", sizeof(char));
            _expr1->to_binary(out);
            _expr2->to_binary(out);
        }

        void DeadlockCondition::to_binary(std::ostream& out) const
        {
            auto path = get_path();
            auto quant = Quantifier::DEADLOCK;
            out.write(reinterpret_cast<const char*>(&path), sizeof(Path));
            out.write(reinterpret_cast<const char*>(&quant), sizeof(Quantifier));
        }

        void BooleanCondition::to_binary(std::ostream& out) const
        {
            auto path = get_path();
            auto quant = Quantifier::PN_BOOLEAN;
            out.write(reinterpret_cast<const char*>(&path), sizeof(Path));
            out.write(reinterpret_cast<const char*>(&quant), sizeof(Quantifier));
            out.write(reinterpret_cast<const char*>(&value), sizeof(bool));
        }

        void UnfoldedUpperBoundsCondition::to_binary(std::ostream& out) const
        {
            auto path = get_path();
            auto quant = Quantifier::UPPERBOUNDS;
            out.write(reinterpret_cast<const char*>(&path), sizeof(Path));
            out.write(reinterpret_cast<const char*>(&quant), sizeof(Quantifier));
            uint32_t size = _places.size();
            out.write(reinterpret_cast<const char*>(&size), sizeof(uint32_t));
            out.write(reinterpret_cast<const char*>(&_max), sizeof(double));
            out.write(reinterpret_cast<const char*>(&_offset), sizeof(double));
            for(auto& b : _places)
            {
                out.write(reinterpret_cast<const char*>(&b._place), sizeof(uint32_t));
                out.write(reinterpret_cast<const char*>(&b._max), sizeof(double));
            }
        }

        void NotCondition::to_binary(std::ostream& out) const
        {
            auto path = get_path();
            auto quant = get_quantifier();
            out.write(reinterpret_cast<const char*>(&path), sizeof(Path));
            out.write(reinterpret_cast<const char*>(&quant), sizeof(Quantifier));
            _cond->to_binary(out);
        }

        /******************** CTL Output ********************/

        void LiteralExpr::to_xml(std::ostream& out,uint32_t tabs, bool tokencount) const {
            generateTabs(out,tabs) << "<integer-constant>" + std::to_string(_value) + "</integer-constant>\n";
        }

        void UnfoldedFireableCondition::to_xml(std::ostream& out, uint32_t tabs) const{
            generateTabs(out, tabs) << "<is-fireable><transition>" + _name << "</transition></is-fireable>\n";
        }

        void UnfoldedIdentifierExpr::to_xml(std::ostream& out,uint32_t tabs, bool tokencount) const {
            if (tokencount) {
                generateTabs(out,tabs) << "<place>" << _name << "</place>\n";
            }
            else
            {
                generateTabs(out,tabs) << "<tokens-count>\n";
                generateTabs(out,tabs+1) << "<place>" << _name << "</place>\n";
                generateTabs(out,tabs) << "</tokens-count>\n";
            }
        }

        void PlusExpr::to_xml(std::ostream& ss,uint32_t tabs, bool tokencount) const {
            if (tokencount) {
                for(auto& e : _exprs) e->to_xml(ss,tabs, tokencount);
                return;
            }

            if(_tk) {
                generateTabs(ss,tabs) << "<tokens-count>\n";
                for(auto& e : _ids) generateTabs(ss,tabs+1) << "<place>" << e.second << "</place>\n";
                for(auto& e : _exprs) e->to_xml(ss,tabs+1, true);
                generateTabs(ss,tabs) << "</tokens-count>\n";
                return;
            }
            generateTabs(ss,tabs) << "<integer-sum>\n";
            generateTabs(ss,tabs+1) << "<integer-constant>" + std::to_string(_constant) + "</integer-constant>\n";
            for(auto& i : _ids)
            {
                generateTabs(ss,tabs+1) << "<tokens-count>\n";
                generateTabs(ss,tabs+2) << "<place>" << i.second << "</place>\n";
                generateTabs(ss,tabs+1) << "</tokens-count>\n";
            }
            for(auto& e : _exprs) e->to_xml(ss,tabs+1, tokencount);
            generateTabs(ss,tabs) << "</integer-sum>\n";
        }

        void SubtractExpr::to_xml(std::ostream& ss,uint32_t tabs, bool tokencount) const {
            generateTabs(ss,tabs) << "<integer-difference>\n";
            for(auto& e : _exprs) e->to_xml(ss,tabs+1);
            generateTabs(ss,tabs) << "</integer-difference>\n";
        }

        void MultiplyExpr::to_xml(std::ostream& ss,uint32_t tabs, bool tokencount) const {
            generateTabs(ss,tabs) << "<integer-product>\n";
            for(auto& e : _exprs) e->to_xml(ss,tabs+1);
            generateTabs(ss,tabs) << "</integer-product>\n";
        }

        void MinusExpr::to_xml(std::ostream& out,uint32_t tabs, bool tokencount) const {

            generateTabs(out,tabs) << "<integer-product>\n";
            _expr->to_xml(out,tabs+1);
            generateTabs(out,tabs+1) << "<integer-difference>\n" ; generateTabs(out,tabs+2) <<
                    "<integer-constant>0</integer-constant>\n" ; generateTabs(out,tabs+2) <<
                    "<integer-constant>1</integer-constant>\n" ; generateTabs(out,tabs+1) <<
                    "</integer-difference>\n" ; generateTabs(out,tabs) << "</integer-product>\n";
        }

        void EXCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<exists-path>\n" ; generateTabs(out,tabs+1) << "<next>\n";
            _cond->to_xml(out,tabs+2);
            generateTabs(out,tabs+1) << "</next>\n" ; generateTabs(out,tabs) << "</exists-path>\n";
        }

        void AXCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<all-paths>\n"; generateTabs(out,tabs+1) << "<next>\n";
            _cond->to_xml(out,tabs+2);
            generateTabs(out,tabs+1) << "</next>\n"; generateTabs(out,tabs) << "</all-paths>\n";
        }

        void EFCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<exists-path>\n" ; generateTabs(out,tabs+1) << "<finally>\n";
            _cond->to_xml(out,tabs+2);
            generateTabs(out,tabs+1) << "</finally>\n" ; generateTabs(out,tabs) << "</exists-path>\n";
        }

        void AFCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<all-paths>\n" ; generateTabs(out,tabs+1) << "<finally>\n";
            _cond->to_xml(out,tabs+2);
            generateTabs(out,tabs+1) << "</finally>\n" ; generateTabs(out,tabs) << "</all-paths>\n";
        }

        void EGCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<exists-path>\n" ; generateTabs(out,tabs+1) << "<globally>\n";
            _cond->to_xml(out,tabs+2);
            generateTabs(out,tabs+1) <<  "</globally>\n" ; generateTabs(out,tabs) << "</exists-path>\n";
        }

        void AGCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<all-paths>\n" ; generateTabs(out,tabs+1) << "<globally>\n";
            _cond->to_xml(out,tabs+2);
            generateTabs(out,tabs+1) << "</globally>\n" ; generateTabs(out,tabs) << "</all-paths>\n";
        }

        void EUCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<exists-path>\n" ; generateTabs(out,tabs+1) << "<until>\n" ; generateTabs(out,tabs+2) << "<before>\n";
            _cond1->to_xml(out,tabs+3);
            generateTabs(out,tabs+2) << "</before>\n" ; generateTabs(out,tabs+2) << "<reach>\n";
            _cond2->to_xml(out,tabs+3);
            generateTabs(out,tabs+2) << "</reach>\n" ; generateTabs(out,tabs+1) << "</until>\n" ; generateTabs(out,tabs) << "</exists-path>\n";
        }

        void AUCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<all-paths>\n" ; generateTabs(out,tabs+1) << "<until>\n" ; generateTabs(out,tabs+2) << "<before>\n";
            _cond1->to_xml(out,tabs+3);
            generateTabs(out,tabs+2) << "</before>\n" ; generateTabs(out,tabs+2) << "<reach>\n";
            _cond2->to_xml(out,tabs+3);
            generateTabs(out,tabs+2) << "</reach>\n" ; generateTabs(out,tabs+1) << "</until>\n" ; generateTabs(out,tabs) << "</all-paths>\n";
        }

        void ACondition::to_xml(std::ostream& out, uint32_t tabs) const {
            generateTabs(out, tabs) << "<all-paths>\n";
            _cond->to_xml(out, tabs+1);
            generateTabs(out, tabs) << "</all-paths>\n";
        }

        void ECondition::to_xml(std::ostream& out, uint32_t tabs) const {
            generateTabs(out, tabs) << "<exists-path>\n";
            _cond->to_xml(out, tabs+1);
            generateTabs(out, tabs) << "</exists-path>\n";
        }

        void FCondition::to_xml(std::ostream& out, uint32_t tabs) const {
            generateTabs(out, tabs) << "<finally>\n";
            _cond->to_xml(out, tabs+1);
            generateTabs(out, tabs) << "</finally>\n";
        }

        void GCondition::to_xml(std::ostream& out, uint32_t tabs) const {
            generateTabs(out, tabs) << "<globally>\n";
            _cond->to_xml(out, tabs+1);
            generateTabs(out, tabs) << "</globally>\n";
        }

        void XCondition::to_xml(std::ostream& out, uint32_t tabs) const {
            generateTabs(out, tabs) << "<next>\n";
            _cond->to_xml(out, tabs+1);
            generateTabs(out, tabs) << "</next>\n";
        }

        void UntilCondition::to_xml(std::ostream& out, uint32_t tabs) const {
            generateTabs(out,tabs) << "<until>\n" ; generateTabs(out,tabs+1) << "<before>\n";
            _cond1->to_xml(out,tabs+2);
            generateTabs(out,tabs+1) << "</before>\n" ; generateTabs(out,tabs+1) << "<reach>\n";
            _cond2->to_xml(out,tabs+2);
            generateTabs(out,tabs+1) << "</reach>\n" ; generateTabs(out,tabs) << "</until>\n" ;
        }

        void AndCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            if(_conds.size() == 0)
            {
                BooleanCondition::TRUE_CONSTANT->to_xml(out, tabs);
                return;
            }
            if(_conds.size() == 1)
            {
                _conds[0]->to_xml(out, tabs);
                return;
            }
            generateTabs(out,tabs) << "<conjunction>\n";
            _conds[0]->to_xml(out, tabs + 1);
            for(size_t i = 1; i < _conds.size(); ++i)
            {
                if(i + 1 == _conds.size())
                {
                    _conds[i]->to_xml(out, tabs + i + 1);
                }
                else
                {
                    generateTabs(out,tabs + i) << "<conjunction>\n";
                    _conds[i]->to_xml(out, tabs + i + 1);
                }
            }
            for(size_t i = _conds.size() - 1; i > 1; --i)
            {
                generateTabs(out,tabs + i) << "</conjunction>\n";
            }
            generateTabs(out,tabs) << "</conjunction>\n";
        }

        void OrCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            if(_conds.size() == 0)
            {
                BooleanCondition::FALSE_CONSTANT->to_xml(out, tabs);
                return;
            }
            if(_conds.size() == 1)
            {
                _conds[0]->to_xml(out, tabs);
                return;
            }
            generateTabs(out,tabs) << "<disjunction>\n";
            _conds[0]->to_xml(out, tabs + 1);
            for(size_t i = 1; i < _conds.size(); ++i)
            {
                if(i + 1 == _conds.size())
                {
                    _conds[i]->to_xml(out, tabs + i + 1);
                }
                else
                {
                    generateTabs(out,tabs + i) << "<disjunction>\n";
                    _conds[i]->to_xml(out, tabs + i + 1);
                }
            }
            for(size_t i = _conds.size() - 1; i > 1; --i)
            {
                generateTabs(out,tabs + i) << "</disjunction>\n";
            }
            generateTabs(out,tabs) << "</disjunction>\n";
        }

        void CompareConjunction::to_xml(std::ostream& out, uint32_t tabs) const
        {
            if(_negated) generateTabs(out,tabs++) << "<negation>";
            if(_constraints.size() == 0) BooleanCondition::TRUE_CONSTANT->to_xml(out, tabs);
            else
            {
                bool single = _constraints.size() == 1 &&
                                (_constraints[0]._lower == 0 ||
                                 _constraints[0]._upper == std::numeric_limits<uint32_t>::max());
                if(!single)
                    generateTabs(out,tabs) << "<conjunction>\n";
                for(auto& c : _constraints)
                {
                    if(c._lower != 0)
                    {
                        generateTabs(out,tabs+1) << "<integer-ge>\n";
                        generateTabs(out,tabs+2) << "<tokens-count>\n";
                        generateTabs(out,tabs+3) << "<place>" << c._name << "</place>\n";
                        generateTabs(out,tabs+2) << "</tokens-count>\n";
                        generateTabs(out,tabs+2) << "<integer-constant>" << c._lower << "</integer-constant>\n";
                        generateTabs(out,tabs+1) << "</integer-ge>\n";
                    }
                    if(c._upper != std::numeric_limits<uint32_t>::max())
                    {
                        generateTabs(out,tabs+1) << "<integer-le>\n";
                        generateTabs(out,tabs+2) << "<tokens-count>\n";
                        generateTabs(out,tabs+3) << "<place>" << c._name << "</place>\n";
                        generateTabs(out,tabs+2) << "</tokens-count>\n";
                        generateTabs(out,tabs+2) << "<integer-constant>" << c._upper << "</integer-constant>\n";
                        generateTabs(out,tabs+1) << "</integer-le>\n";
                    }
                }
                if(!single)
                    generateTabs(out,tabs) << "</conjunction>\n";
            }
            if(_negated) generateTabs(out,--tabs) << "</negation>";
        }

        void EqualCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<integer-eq>\n";
            _expr1->to_xml(out,tabs+1);
            _expr2->to_xml(out,tabs+1);
            generateTabs(out,tabs) << "</integer-eq>\n";
        }

        void NotEqualCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<integer-ne>\n";
            _expr1->to_xml(out,tabs+1);
            _expr2->to_xml(out,tabs+1);
            generateTabs(out,tabs) << "</integer-ne>\n";
        }

        void LessThanCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<integer-lt>\n";
            _expr1->to_xml(out,tabs+1);
            _expr2->to_xml(out,tabs+1);
            generateTabs(out,tabs) << "</integer-lt>\n";
        }

        void LessThanOrEqualCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<integer-le>\n";
            _expr1->to_xml(out,tabs+1);
            _expr2->to_xml(out,tabs+1);
            generateTabs(out,tabs) << "</integer-le>\n";
        }

        void NotCondition::to_xml(std::ostream& out,uint32_t tabs) const {

            generateTabs(out,tabs) << "<negation>\n";
            _cond->to_xml(out,tabs+1);
            generateTabs(out,tabs) << "</negation>\n";
        }

        void BooleanCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<" <<
                    (value ? "true" : "false")
                    << "/>\n";
        }

        void DeadlockCondition::to_xml(std::ostream& out,uint32_t tabs) const {
            generateTabs(out,tabs) << "<deadlock/>\n";
        }

        void UnfoldedUpperBoundsCondition::to_xml(std::ostream& out, uint32_t tabs) const {
            generateTabs(out, tabs) << "<place-bound>\n";
            for(auto& p : _places)
                generateTabs(out, tabs + 1) << "<place>" << p._name << "</place>\n";
            generateTabs(out, tabs) << "</place-bound>\n";
        }

        /******************** Query Simplification ********************/

        Member LiteralExpr::constraint(SimplificationContext& context) const {
            return Member(_value);
        }

        Member member_for_place(size_t p, SimplificationContext& context)
        {
            std::vector<int> row(context.net().number_of_transitions(), 0);
            row.shrink_to_fit();
            for (size_t t = 0; t < context.net().number_of_transitions(); t++) {
                row[t] = context.net().out_arc(t, p) - context.net().in_arc(p, t);
            }
            return Member(std::move(row), context.marking()[p]);
        }

        Member UnfoldedIdentifierExpr::constraint(SimplificationContext& context) const {
            return member_for_place(_offsetInMarking, context);
        }

        Member CommutativeExpr::commutative_cons(int constant, SimplificationContext& context, std::function<void(Member& a, Member b)> op) const
        {
            Member res;
            bool first = true;
            if(_constant != constant || (_exprs.size() == 0 && _ids.size() == 0))
            {
                first = false;
                res = Member(_constant);
            }

            for(auto& i : _ids)
            {
                if(first) res = member_for_place(i.first, context);
                else op(res, member_for_place(i.first, context));
                first = false;
            }

            for(auto& e : _exprs)
            {
                if(first) res = e->constraint(context);
                else op(res, e->constraint(context));
                first = false;
            }
            return res;
        }

        Member PlusExpr::constraint(SimplificationContext& context) const {
            return commutative_cons(0, context, [](auto& a , auto b){ a += b;});
        }

        Member SubtractExpr::constraint(SimplificationContext& context) const {
            Member res = _exprs[0]->constraint(context);
            for(size_t i = 1; i < _exprs.size(); ++i) res -= _exprs[i]->constraint(context);
            return res;
        }

        Member MultiplyExpr::constraint(SimplificationContext& context) const {
            return commutative_cons(1, context, [](auto& a , auto b){ a *= b;});
        }

        Member MinusExpr::constraint(SimplificationContext& context) const {
            Member neg(-1);
            return _expr->constraint(context) *= neg;
        }

        Retval simplifyEX(Retval& r, SimplificationContext& context) {
            if(r._formula->is_trivially_true() || !r._neglps->satisfiable(context)) {
                return Retval(std::make_shared<NotCondition>(
                        std::make_shared<DeadlockCondition>()));
            } else if(r._formula->is_trivially_false() || !r._lps->satisfiable(context)) {
                return Retval(BooleanCondition::FALSE_CONSTANT);
            } else {
                return Retval(std::make_shared<EXCondition>(r._formula));
            }
        }

        Retval simplifyAX(Retval& r, SimplificationContext& context) {
            if(r._formula->is_trivially_true() || !r._neglps->satisfiable(context)){
                return Retval(BooleanCondition::TRUE_CONSTANT);
            } else if(r._formula->is_trivially_false() || !r._lps->satisfiable(context)){
                return Retval(std::make_shared<DeadlockCondition>());
            } else{
                return Retval(std::make_shared<AXCondition>(r._formula));
            }
        }

        Retval simplifyEF(Retval& r, SimplificationContext& context){
            if(r._formula->is_trivially_true() || !r._neglps->satisfiable(context)){
                return Retval(BooleanCondition::TRUE_CONSTANT);
            } else if(r._formula->is_trivially_false() || !r._lps->satisfiable(context)){
                return Retval(BooleanCondition::FALSE_CONSTANT);
            } else {
                return Retval(std::make_shared<EFCondition>(r._formula));
            }
        }

        Retval simplifyAF(Retval& r, SimplificationContext& context){
            if(r._formula->is_trivially_true() || !r._neglps->satisfiable(context)){
                return Retval(BooleanCondition::TRUE_CONSTANT);
            } else if(r._formula->is_trivially_false() || !r._lps->satisfiable(context)){
                return Retval(BooleanCondition::FALSE_CONSTANT);
            } else {
                return Retval(std::make_shared<AFCondition>(r._formula));
            }
        }

        Retval simplifyEG(Retval& r, SimplificationContext& context){
            if(r._formula->is_trivially_true() || !r._neglps->satisfiable(context)){
                return Retval(BooleanCondition::TRUE_CONSTANT);
            } else if(r._formula->is_trivially_false() || !r._lps->satisfiable(context)){
                return Retval(BooleanCondition::FALSE_CONSTANT);
            } else {
                return Retval(std::make_shared<EGCondition>(r._formula));
            }
        }

        Retval simplifyAG(Retval& r, SimplificationContext& context){
            if(r._formula->is_trivially_true() || !r._neglps->satisfiable(context)){
                return Retval(BooleanCondition::TRUE_CONSTANT);
            } else if(r._formula->is_trivially_false() || !r._lps->satisfiable(context)){
                return Retval(BooleanCondition::FALSE_CONSTANT);
            } else {
                return Retval(std::make_shared<AGCondition>(r._formula));
            }
        }

        template <typename Quantifier>
        Retval simplifySimpleQuant(Retval& r, SimplificationContext& context) {
            static_assert(std::is_base_of_v<SimpleQuantifierCondition, Quantifier>);
            if (r._formula->is_trivially_true() || !r._neglps->satisfiable(context)) {
                return Retval(BooleanCondition::TRUE_CONSTANT);
            } else if (r._formula->is_trivially_false() || !r._lps->satisfiable(context)) {
                return Retval(BooleanCondition::FALSE_CONSTANT);
            } else {
                return Retval(std::make_shared<Quantifier>(r._formula));
            }
        }


        Retval EXCondition::simplify(SimplificationContext& context) const {
            Retval r = _cond->simplify(context);
            return context.negated() ? simplifyAX(r, context) : simplifyEX(r, context);
        }

        Retval AXCondition::simplify(SimplificationContext& context) const {
            Retval r = _cond->simplify(context);
            return context.negated() ? simplifyEX(r, context) : simplifyAX(r, context);
        }

        Retval EFCondition::simplify(SimplificationContext& context) const {
            Retval r = _cond->simplify(context);
            return context.negated() ? simplifyAG(r, context) : simplifyEF(r, context);
        }

        Retval AFCondition::simplify(SimplificationContext& context) const {
            Retval r = _cond->simplify(context);
            return context.negated() ? simplifyEG(r, context) : simplifyAF(r, context);
        }

        Retval EGCondition::simplify(SimplificationContext& context) const {
            Retval r = _cond->simplify(context);
            return context.negated() ? simplifyAF(r, context) : simplifyEG(r, context);
        }

        Retval AGCondition::simplify(SimplificationContext& context) const {
            Retval r = _cond->simplify(context);
            return context.negated() ? simplifyEF(r, context) : simplifyAG(r, context);
        }

        Retval EUCondition::simplify(SimplificationContext& context) const {
            // cannot push negation any further
            bool neg = context.negated();
            context.set_negate(false);
            Retval r2 = _cond2->simplify(context);
            if(r2._formula->is_trivially_true() || !r2._neglps->satisfiable(context))
            {
                context.set_negate(neg);
                return neg ?
                            Retval(BooleanCondition::FALSE_CONSTANT) :
                            Retval(BooleanCondition::TRUE_CONSTANT);
            }
            else if(r2._formula->is_trivially_false() || !r2._lps->satisfiable(context))
            {
                context.set_negate(neg);
                return neg ?
                            Retval(BooleanCondition::TRUE_CONSTANT) :
                            Retval(BooleanCondition::FALSE_CONSTANT);
            }
            Retval r1 = _cond1->simplify(context);
            context.set_negate(neg);

            if(context.negated()){
                if(r1._formula->is_trivially_true() || !r1._neglps->satisfiable(context)){
                    return Retval(std::make_shared<NotCondition>(
                            std::make_shared<EFCondition>(r2._formula)));
                } else if(r1._formula->is_trivially_false() || !r1._lps->satisfiable(context)){
                    return Retval(std::make_shared<NotCondition>(r2._formula));
                } else {
                    return Retval(std::make_shared<NotCondition>(
                            std::make_shared<EUCondition>(r1._formula, r2._formula)));
                }
            } else {
                if(r1._formula->is_trivially_true() || !r1._neglps->satisfiable(context)){
                    return Retval(std::make_shared<EFCondition>(r2._formula));
                } else if(r1._formula->is_trivially_false() || !r1._lps->satisfiable(context)){
                    return r2;
                } else {
                    return Retval(std::make_shared<EUCondition>(r1._formula, r2._formula));
                }
            }
        }

        Retval AUCondition::simplify(SimplificationContext& context) const {
            // cannot push negation any further
            bool neg = context.negated();
            context.set_negate(false);
            Retval r2 = _cond2->simplify(context);
            if(r2._formula->is_trivially_true() || !r2._neglps->satisfiable(context))
            {
                context.set_negate(neg);
                return neg ?
                            Retval(BooleanCondition::FALSE_CONSTANT) :
                            Retval(BooleanCondition::TRUE_CONSTANT);
            }
            else if(r2._formula->is_trivially_false() || !r2._lps->satisfiable(context))
            {
                context.set_negate(neg);
                return neg ?
                            Retval(BooleanCondition::TRUE_CONSTANT) :
                            Retval(BooleanCondition::FALSE_CONSTANT);
            }
            Retval r1 = _cond1->simplify(context);
            context.set_negate(neg);

            if(context.negated()){
                if(r1._formula->is_trivially_true() || !r1._neglps->satisfiable(context)){
                    return Retval(std::make_shared<NotCondition>(
                            std::make_shared<AFCondition>(r2._formula)));
                } else if(r1._formula->is_trivially_false() || !r1._lps->satisfiable(context)){
                    return Retval(std::make_shared<NotCondition>(r2._formula));
                } else {
                    return Retval(std::make_shared<NotCondition>(
                            std::make_shared<AUCondition>(r1._formula, r2._formula)));
                }
            } else {
                if(r1._formula->is_trivially_true() || !r1._neglps->satisfiable(context)){
                    return Retval(std::make_shared<AFCondition>(r2._formula));
                } else if(r1._formula->is_trivially_false() || !r1._lps->satisfiable(context)){
                    return r2;
                } else {
                    return Retval(std::make_shared<AUCondition>(r1._formula, r2._formula));
                }
            }
        }

        Retval UntilCondition::simplify(SimplificationContext& context) const {
            bool neg = context.negated();
            context.set_negate(false);

            Retval r2 = _cond2->simplify(context);
            if(r2._formula->is_trivially_true() || !r2._neglps->satisfiable(context))
            {
                context.set_negate(neg);
                return neg ?
                       Retval(BooleanCondition::FALSE_CONSTANT) :
                       Retval(BooleanCondition::TRUE_CONSTANT);
            }
            else if(r2._formula->is_trivially_false() || !r2._lps->satisfiable(context))
            {
                context.set_negate(neg);
                return neg ?
                       Retval(BooleanCondition::TRUE_CONSTANT) :
                       Retval(BooleanCondition::FALSE_CONSTANT);
            }
            Retval r1 = _cond1->simplify(context);
            context.set_negate(neg);

            if(context.negated()){
                if(r1._formula->is_trivially_true() || !r1._neglps->satisfiable(context)){
                    return Retval(std::make_shared<NotCondition>(
                            std::make_shared<FCondition>(r2._formula)));
                } else if(r1._formula->is_trivially_false() || !r1._lps->satisfiable(context)){
                    return Retval(std::make_shared<NotCondition>(r2._formula));
                } else {
                    return Retval(std::make_shared<NotCondition>(
                            std::make_shared<UntilCondition>(r1._formula, r2._formula)));
                }
            } else {
                if(r1._formula->is_trivially_true() || !r1._neglps->satisfiable(context)){
                    return Retval(std::make_shared<FCondition>(r2._formula));
                } else if(r1._formula->is_trivially_false() || !r1._lps->satisfiable(context)){
                    return r2;
                } else {
                    return Retval(std::make_shared<UntilCondition>(r1._formula, r2._formula));
                }
            }
        }

        Retval ECondition::simplify(SimplificationContext& context) const {
            assert(false);
            Retval r = _cond->simplify(context);
            return context.negated() ? simplifySimpleQuant<ACondition>(r, context) : simplifySimpleQuant<ECondition>(r, context);
        }

        Retval ACondition::simplify(SimplificationContext& context) const {
            assert(false);
            Retval r = _cond->simplify(context);
            return context.negated() ? simplifySimpleQuant<ECondition>(r, context) : simplifySimpleQuant<ACondition>(r, context);
        }

        Retval FCondition::simplify(SimplificationContext& context) const {
            Retval r = _cond->simplify(context);
            return context.negated() ? simplifySimpleQuant<GCondition>(r, context) : simplifySimpleQuant<FCondition>(r, context);
        }

        Retval GCondition::simplify(SimplificationContext& context) const {
            Retval r = _cond->simplify(context);
            return context.negated() ? simplifySimpleQuant<FCondition>(r, context) : simplifySimpleQuant<GCondition>(r, context);
        }

        Retval XCondition::simplify(SimplificationContext& context) const {
            Retval r = _cond->simplify(context);
            return simplifySimpleQuant<XCondition>(r, context);
        }

        AbstractProgramCollection_ptr mergeLps(std::vector<AbstractProgramCollection_ptr>&& lps)
        {
            if(lps.size() == 0) return nullptr;
            int j = 0;
            int i = lps.size() - 1;
            while(i > 0)
            {
                if(i <= j) j = 0;
                else
                {
                    lps[j] = std::make_shared<MergeCollection>(lps[j], lps[i]);
                    --i;
                    ++j;
                }
            }
            return lps[0];
        }

        Retval LogicalCondition::simplifyAnd(SimplificationContext& context) const {

            std::vector<Condition_ptr> conditions;
            std::vector<AbstractProgramCollection_ptr> lpsv;
            std::vector<AbstractProgramCollection_ptr>  neglps;
            for(auto& c : _conds)
            {
                auto r = c->simplify(context);
                if(r._formula->is_trivially_false())
                {
                    return Retval(BooleanCondition::FALSE_CONSTANT);
                }
                else if(r._formula->is_trivially_true())
                {
                    continue;
                }

                conditions.push_back(r._formula);
                lpsv.emplace_back(r._lps);
                neglps.emplace_back(r._neglps);
            }

            if(conditions.size() == 0)
            {
                return Retval(BooleanCondition::TRUE_CONSTANT);
            }

            auto lps = mergeLps(std::move(lpsv));

            try {
                if(!context.timeout() && !lps->satisfiable(context))
                {
                    return Retval(BooleanCondition::FALSE_CONSTANT);
                }
             }
             catch(std::bad_alloc& e)
             {
                // we are out of memory, deal with it.
                std::cout<<"Query reduction: memory exceeded during LPS merge."<<std::endl;
             }

            // Lets try to see if the r1 AND r2 can ever be false at the same time
            // If not, then we know that r1 || r2 must be true.
            // we check this by checking if !r1 && !r2 is unsat

            return Retval(
                    makeAnd(conditions),
                    std::move(lps),
                    std::make_shared<UnionCollection>(std::move(neglps)));
        }

        Retval LogicalCondition::simplifyOr(SimplificationContext& context) const {

            std::vector<Condition_ptr> conditions;
            std::vector<AbstractProgramCollection_ptr> lps, neglpsv;
            for(auto& c : _conds)
            {
                auto r = c->simplify(context);
                assert(r._neglps);
                assert(r._lps);

                if(r._formula->is_trivially_true())
                {
                    return Retval(BooleanCondition::TRUE_CONSTANT);
                }
                else if(r._formula->is_trivially_false())
                {
                    continue;
                }
                conditions.push_back(r._formula);
                lps.push_back(r._lps);
                neglpsv.emplace_back(r._neglps);
            }

            AbstractProgramCollection_ptr  neglps = mergeLps(std::move(neglpsv));

            if(conditions.size() == 0)
            {
                return Retval(BooleanCondition::FALSE_CONSTANT);
            }

            try {
               if(!context.timeout() && !neglps->satisfiable(context))
               {
                   return Retval(BooleanCondition::TRUE_CONSTANT);
               }
            }
            catch(std::bad_alloc& e)
            {
               // we are out of memory, deal with it.
               std::cout<<"Query reduction: memory exceeded during LPS merge."<<std::endl;
            }

            // Lets try to see if the r1 AND r2 can ever be false at the same time
            // If not, then we know that r1 || r2 must be true.
            // we check this by checking if !r1 && !r2 is unsat

            return Retval(
                    makeOr(conditions),
                    std::make_shared<UnionCollection>(std::move(lps)),
                    std::move(neglps));
        }

        Retval AndCondition::simplify(SimplificationContext& context) const {
            if(context.timeout())
            {
                if(context.negated())
                    return Retval(std::make_shared<NotCondition>(
                            makeAnd(_conds)));
                else
                    return Retval(
                            makeAnd(_conds));
            }

            if(context.negated())
                return simplifyOr(context);
            else
                return simplifyAnd(context);

        }

        Retval OrCondition::simplify(SimplificationContext& context) const {
            if(context.timeout())
            {
                if(context.negated())
                    return Retval(std::make_shared<NotCondition>(
                            makeOr(_conds)));
                else
                    return Retval(makeOr(_conds));
            }
            if(context.negated())
                return simplifyAnd(context);
            else
                return simplifyOr(context);
        }

        Retval CompareConjunction::simplify(SimplificationContext& context) const {
            if(context.timeout())
            {
                return Retval(std::make_shared<CompareConjunction>(*this, context.negated()));
            }
            std::vector<AbstractProgramCollection_ptr>  neglps, lpsv;
            auto neg = context.negated() != _negated;
            std::vector<cons_t> nconstraints;
            for(auto& c : _constraints)
            {
                nconstraints.push_back(c);
                if(c._lower != 0 /*&& !context.timeout()*/ )
                {
                    auto m2 = member_for_place(c._place, context);
                    Member m1(c._lower);
                    // test for trivial comparison
                    Trivial eval = m1 <= m2;
                    if(eval != Trivial::Indeterminate) {
                        if(eval == Trivial::False)
                            return Retval(BooleanCondition::getShared(neg));
                        else
                            nconstraints.back()._lower = 0;
                    } else { // if no trivial case
                        int constant = m2.constant() - m1.constant();
                        m1 -= m2;
                        m2 = m1;
                        auto lp = std::make_shared<SingleProgram>(std::move(m1), constant, Simplification::OP_LE);
                        auto nlp = std::make_shared<SingleProgram>(std::move(m2), constant, Simplification::OP_GT);
                        lpsv.push_back(lp);
                        neglps.push_back(nlp);
                   }
                }

                if(c._upper != std::numeric_limits<uint32_t>::max() /*&& !context.timeout()*/)
                {
                    auto m1 = member_for_place(c._place, context);
                    Member m2(c._upper);
                    // test for trivial comparison
                    Trivial eval = m1 <= m2;
                    if(eval != Trivial::Indeterminate) {
                        if(eval == Trivial::False)
                            return Retval(BooleanCondition::getShared(neg));
                        else
                            nconstraints.back()._upper = std::numeric_limits<uint32_t>::max();
                    } else { // if no trivial case
                        int constant = m2.constant() - m1.constant();
                        m1 -= m2;
                        m2 = m1;
                        auto lp = std::make_shared<SingleProgram>(std::move(m1), constant, Simplification::OP_LE);
                        auto nlp = std::make_shared<SingleProgram>(std::move(m2), constant, Simplification::OP_GT);
                        lpsv.push_back(lp);
                        neglps.push_back(nlp);
                   }
                }

                assert(nconstraints.size() > 0);
                if(nconstraints.back()._lower == 0 && nconstraints.back()._upper == std::numeric_limits<uint32_t>::max())
                    nconstraints.pop_back();

                assert(nconstraints.size() <= neglps.size()*2);
            }

            auto lps = mergeLps(std::move(lpsv));

            if(lps == nullptr && !context.timeout())
            {
                return Retval(BooleanCondition::getShared(!neg));
            }

            try {
                if(!context.timeout() && lps &&  !lps->satisfiable(context))
                {
                    return Retval(BooleanCondition::getShared(neg));
                }
             }
             catch(std::bad_alloc& e)
             {
                // we are out of memory, deal with it.
                std::cout<<"Query reduction: memory exceeded during LPS merge."<<std::endl;
             }
            // Lets try to see if the r1 AND r2 can ever be false at the same time
            // If not, then we know that r1 || r2 must be true.
            // we check this by checking if !r1 && !r2 is unsat
            try {
                // remove trivial rules from neglp
                int ncnt = neglps.size() - 1;
                for(int i = nconstraints.size() - 1; i >= 0; --i)
                {
                    if(context.timeout()) break;
                    assert(ncnt >= 0);
                    size_t cnt = 0;
                    auto& c = nconstraints[i];
                    if(c._lower != 0) ++cnt;
                    if(c._upper != std::numeric_limits<uint32_t>::max()) ++cnt;
                    for(size_t j = 0; j < cnt ; ++j)
                    {
                        assert(ncnt >= 0);
                        if(!neglps[ncnt]->satisfiable(context))
                        {
                            if(j == 1 || c._upper == std::numeric_limits<uint32_t>::max())
                                c._lower = 0;
                            else if(j == 0)
                                c._upper = std::numeric_limits<uint32_t>::max();
                            neglps.erase(neglps.begin() + ncnt);
                        }
                        if(c._upper == std::numeric_limits<uint32_t>::max() && c._lower == 0)
                            nconstraints.erase(nconstraints.begin() + i);
                        --ncnt;
                    }
                }
            }
            catch(std::bad_alloc& e)
            {
               // we are out of memory, deal with it.
               std::cout<<"Query reduction: memory exceeded during LPS merge."<<std::endl;
            }
            if(nconstraints.size() == 0)
            {
                return Retval(BooleanCondition::getShared(!neg));
            }


            Condition_ptr rc = [&]() -> Condition_ptr {
                if(nconstraints.size() == 1)
                {
                    auto& c = nconstraints[0];
                    auto id = std::make_shared<UnfoldedIdentifierExpr>(c._name, c._place);
                    auto ll = std::make_shared<LiteralExpr>(c._lower);
                    auto lu = std::make_shared<LiteralExpr>(c._upper);
                    if(c._lower == c._upper)
                    {
                        if(c._lower != 0)
                            if(neg) return std::make_shared<NotEqualCondition>(id, lu);
                            else    return std::make_shared<EqualCondition>(id, lu);
                        else
                            if(neg) return std::make_shared<LessThanCondition>(lu, id);
                            else    return std::make_shared<LessThanOrEqualCondition>(id, lu);
                    }
                    else
                    {
                        if(c._lower != 0 && c._upper != std::numeric_limits<uint32_t>::max())
                        {
                            if(neg) return makeOr(std::make_shared<LessThanCondition>(id, ll),std::make_shared<LessThanCondition>(lu, id));
                            else    return makeAnd(std::make_shared<LessThanOrEqualCondition>(ll, id),std::make_shared<LessThanOrEqualCondition>(id, lu));
                        }
                        else if(c._lower != 0)
                        {
                            if(neg) return std::make_shared<LessThanCondition>(id, ll);
                            else    return std::make_shared<LessThanOrEqualCondition>(ll, id);
                        }
                        else
                        {
                            if(neg) return std::make_shared<LessThanCondition>(lu, id);
                            else    return std::make_shared<LessThanOrEqualCondition>(id, lu);
                        }
                    }
                }
                else
                {
                    return std::make_shared<CompareConjunction>(std::move(nconstraints), context.negated() != _negated);
                }
            }();


            if(!neg)
            {
                return Retval(
                    rc,
                    std::move(lps),
                    std::make_shared<UnionCollection>(std::move(neglps)));
            }
            else
            {
                return Retval(
                    rc,
                    std::make_shared<UnionCollection>(std::move(neglps)),
                    std::move(lps));
            }
        }

        Retval EqualCondition::simplify(SimplificationContext& context) const {

            Member m1 = _expr1->constraint(context);
            Member m2 = _expr2->constraint(context);
            std::shared_ptr<AbstractProgramCollection> lps, neglps;
            if (!context.timeout() && m1.can_analyze() && m2.can_analyze()) {
                if ((m1.is_zero() && m2.is_zero()) || m1.substration_is_zero(m2)) {
                    return Retval(BooleanCondition::getShared(
                        context.negated() ? (m1.constant() != m2.constant()) : (m1.constant() == m2.constant())));
                } else {
                    int constant = m2.constant() - m1.constant();
                    m1 -= m2;
                    m2 = m1;
                    neglps =
                            std::make_shared<UnionCollection>(
                            std::make_shared<SingleProgram>(std::move(m1), constant, Simplification::OP_GT),
                            std::make_shared<SingleProgram>(std::move(m2), constant, Simplification::OP_LT));
                    Member m3 = m2;
                    lps = std::make_shared<SingleProgram>(std::move(m3), constant, Simplification::OP_EQ);

                    if(context.negated()) lps.swap(neglps);
                }
            } else {
                lps = std::make_shared<SingleProgram>();
                neglps = std::make_shared<SingleProgram>();
            }

            if (!context.timeout() && !lps->satisfiable(context)) {
                return Retval(BooleanCondition::FALSE_CONSTANT);
            }
            else if(!context.timeout() && !neglps->satisfiable(context))
            {
                return Retval(BooleanCondition::TRUE_CONSTANT);
            }
            else {
                if (context.negated()) {
                    return Retval(std::make_shared<NotEqualCondition>(_expr1, _expr2), std::move(lps), std::move(neglps));
                } else {
                    return Retval(std::make_shared<EqualCondition>(_expr1, _expr2), std::move(lps), std::move(neglps));
                }
            }
        }

        Retval NotEqualCondition::simplify(SimplificationContext& context) const {
            Member m1 = _expr1->constraint(context);
            Member m2 = _expr2->constraint(context);
            std::shared_ptr<AbstractProgramCollection> lps, neglps;
            if (!context.timeout() && m1.can_analyze() && m2.can_analyze()) {
                if ((m1.is_zero() && m2.is_zero()) || m1.substration_is_zero(m2)) {
                    return Retval(std::make_shared<BooleanCondition>(
                        context.negated() ? (m1.constant() == m2.constant()) : (m1.constant() != m2.constant())));
                } else{
                    int constant = m2.constant() - m1.constant();
                    m1 -= m2;
                    m2 = m1;
                    lps =
                            std::make_shared<UnionCollection>(
                            std::make_shared<SingleProgram>(std::move(m1), constant, Simplification::OP_GT),
                            std::make_shared<SingleProgram>(std::move(m2), constant, Simplification::OP_LT));
                    Member m3 = m2;
                    neglps = std::make_shared<SingleProgram>(std::move(m3), constant, Simplification::OP_EQ);

                    if(context.negated()) lps.swap(neglps);
                }
            } else {
                lps = std::make_shared<SingleProgram>();
                neglps = std::make_shared<SingleProgram>();
            }
            if (!context.timeout() && !lps->satisfiable(context)) {
                return Retval(BooleanCondition::FALSE_CONSTANT);
            }
            else if(!context.timeout() && !neglps->satisfiable(context))
            {
                return Retval(BooleanCondition::TRUE_CONSTANT);
            }
            else {
                if (context.negated()) {
                    return Retval(std::make_shared<EqualCondition>(_expr1, _expr2), std::move(lps), std::move(neglps));
                } else {
                    return Retval(std::make_shared<NotEqualCondition>(_expr1, _expr2), std::move(lps), std::move(neglps));
                }
            }
        }

        Retval LessThanCondition::simplify(SimplificationContext& context) const {
            Member m1 = _expr1->constraint(context);
            Member m2 = _expr2->constraint(context);
            AbstractProgramCollection_ptr lps, neglps;
            if (!context.timeout() && m1.can_analyze() && m2.can_analyze()) {
                // test for trivial comparison
                Trivial eval = context.negated() ? m1 >= m2 : m1 < m2;
                if(eval != Trivial::Indeterminate) {
                    return Retval(BooleanCondition::getShared(eval == Trivial::True));
                } else { // if no trivial case
                    int constant = m2.constant() - m1.constant();
                    m1 -= m2;
                    m2 = m1;
                    lps = std::make_shared<SingleProgram>(std::move(m1), constant, (context.negated() ? Simplification::OP_GE : Simplification::OP_LT));
                    neglps = std::make_shared<SingleProgram>(std::move(m2), constant, (!context.negated() ? Simplification::OP_GE : Simplification::OP_LT));
                }
            } else {
                lps = std::make_shared<SingleProgram>();
                neglps = std::make_shared<SingleProgram>();
            }

            if (!context.timeout() && !lps->satisfiable(context)) {
                return Retval(BooleanCondition::FALSE_CONSTANT);
            }
            else if(!context.timeout() && !neglps->satisfiable(context))
            {
                return Retval(BooleanCondition::TRUE_CONSTANT);
            }
            else {
                if (context.negated()) {
                    return Retval(std::make_shared<LessThanOrEqualCondition>(_expr2, _expr1), std::move(lps), std::move(neglps));
                } else {
                    return Retval(std::make_shared<LessThanCondition>(_expr1, _expr2), std::move(lps), std::move(neglps));
                }
            }
        }

        Retval LessThanOrEqualCondition::simplify(SimplificationContext& context) const {
            Member m1 = _expr1->constraint(context);
            Member m2 = _expr2->constraint(context);

            AbstractProgramCollection_ptr lps, neglps;
            if (!context.timeout() && m1.can_analyze() && m2.can_analyze()) {
                // test for trivial comparison
                Trivial eval = context.negated() ? m1 > m2 : m1 <= m2;
                if(eval != Trivial::Indeterminate) {
                    return Retval(BooleanCondition::getShared(eval == Trivial::True));
                } else { // if no trivial case
                    int constant = m2.constant() - m1.constant();
                    m1 -= m2;
                    m2 = m1;
                    lps = std::make_shared<SingleProgram>(std::move(m1), constant, (context.negated() ? Simplification::OP_GT : Simplification::OP_LE));
                    neglps = std::make_shared<SingleProgram>(std::move(m2), constant, (context.negated() ? Simplification::OP_LE : Simplification::OP_GT));
                }
            } else {
                lps = std::make_shared<SingleProgram>();
                neglps = std::make_shared<SingleProgram>();
            }

            assert(lps);
            assert(neglps);

            if(!context.timeout() && !neglps->satisfiable(context)){
                return Retval(BooleanCondition::TRUE_CONSTANT);
            } else if (!context.timeout() && !lps->satisfiable(context)) {
                return Retval(BooleanCondition::FALSE_CONSTANT);
            } else {
                if (context.negated()) {
                    return Retval(std::make_shared<LessThanCondition>(_expr2, _expr1), std::move(lps), std::move(neglps));
                } else {
                    return Retval(std::make_shared<LessThanOrEqualCondition>(_expr1, _expr2),
                            std::move(lps), std::move(neglps));
                }
            }
        }

        Retval NotCondition::simplify(SimplificationContext& context) const {
            context.negate();
            Retval r = _cond->simplify(context);
            context.negate();
            return r;
        }

        Retval BooleanCondition::simplify(SimplificationContext& context) const {
            if (context.negated()) {
                return Retval(getShared(!value));
            } else {
                return Retval(getShared(value));
            }
        }

        Retval DeadlockCondition::simplify(SimplificationContext& context) const {
            if (context.negated()) {
                return Retval(std::make_shared<NotCondition>(DeadlockCondition::DEADLOCK));
            } else {
                return Retval(DeadlockCondition::DEADLOCK);
            }
        }

        Retval UnfoldedUpperBoundsCondition::simplify(SimplificationContext& context) const
        {
            std::vector<place_t> next;
            std::vector<uint32_t> places;
            for(auto& p : _places)
                places.push_back(p._place);
            const auto nplaces = _places.size();
            const auto bounds = LinearProgram::bounds(context, context.get_lp_timeout(), places);
            double offset = _offset;
            for(size_t i = 0; i < nplaces; ++i)
            {
                if(bounds[i].first != 0 && !bounds[i].second)
                    next.emplace_back(_places[i], bounds[i].first);
                if(bounds[i].second)
                    offset += bounds[i].first;
            }
            if(bounds[nplaces].second)
            {
                next.clear();
                return Retval(std::make_shared<UnfoldedUpperBoundsCondition>(next, 0, bounds[nplaces].first + _offset));
            }
            return Retval(std::make_shared<UnfoldedUpperBoundsCondition>(next, bounds[nplaces].first-offset, offset));
        }

        /******************** Check if query is a reachability query ********************/

        bool EXCondition::is_reachability(uint32_t depth) const {
            return false;
        }

        bool EGCondition::is_reachability(uint32_t depth) const {
            return false;
        }

        bool EFCondition::is_reachability(uint32_t depth) const {
            return depth > 0 ? false : _cond->is_reachability(depth + 1);
        }

        bool AXCondition::is_reachability(uint32_t depth) const {
            return false;
        }

        bool AGCondition::is_reachability(uint32_t depth) const {
            return depth > 0 ? false : _cond->is_reachability(depth + 1);
        }

        bool AFCondition::is_reachability(uint32_t depth) const {
            return false;
        }

        bool ECondition::is_reachability(uint32_t depth) const {
            if (depth != 0) {
                return false;
            }

            if (auto cond = dynamic_cast<FCondition*>(_cond.get())) {
                // EF is a reachability formula so skip checking the F.
                return (*cond)[0]->is_reachability(depth + 1);
            }
            return _cond->is_reachability(depth + 1);
        }

        bool ACondition::is_reachability(uint32_t depth) const {
            if (depth != 0) {
                return false;
            }
            if (auto cond = dynamic_cast<GCondition*>(_cond.get())) {
                return (*cond)[0]->is_reachability(depth + 1);
            }
            return _cond->is_reachability(depth + 1);
        }

        bool UntilCondition::is_reachability(uint32_t depth) const {
            return false;
        }

        bool LogicalCondition::is_reachability(uint32_t depth) const {
            if(depth == 0) return false;
            bool reachability = true;
            for(auto& c : _conds)
            {
                reachability = reachability && c->is_reachability(depth + 1);
                if(!reachability) break;
            }
            return reachability;
        }

        bool CompareCondition::is_reachability(uint32_t depth) const {
            return depth > 0;
        }

        bool NotCondition::is_reachability(uint32_t depth) const {
            return _cond->is_reachability(depth);
        }

        bool BooleanCondition::is_reachability(uint32_t depth) const {
            return depth > 0;
        }

        bool DeadlockCondition::is_reachability(uint32_t depth) const {
            return depth > 0;
        }

        bool UnfoldedUpperBoundsCondition::is_reachability(uint32_t depth) const {
            return depth > 0;
        }

        /******************** Prepare Reachability Queries ********************/

        Condition_ptr EXCondition::prepare_for_reachability(bool negated) const {
            return nullptr;
        }

        Condition_ptr EGCondition::prepare_for_reachability(bool negated) const {
            return nullptr;
        }

        Condition_ptr EFCondition::prepare_for_reachability(bool negated) const {
            _cond->set_invariant(negated);
            return _cond;
        }

        Condition_ptr AXCondition::prepare_for_reachability(bool negated) const {
            return nullptr;
        }

        Condition_ptr AGCondition::prepare_for_reachability(bool negated) const {
            Condition_ptr cond = std::make_shared<NotCondition>(_cond);
            cond->set_invariant(!negated);
            return cond;
        }

        Condition_ptr AFCondition::prepare_for_reachability(bool negated) const {
            return nullptr;
        }

        Condition_ptr ACondition::prepare_for_reachability(bool negated) const {
            auto g = std::dynamic_pointer_cast<GCondition>(_cond);
            return g ? AGCondition((*g)[0]).prepare_for_reachability(negated) : nullptr;
        }

        Condition_ptr ECondition::prepare_for_reachability(bool negated) const {
            auto f = std::dynamic_pointer_cast<FCondition>(_cond);
            return f ? EFCondition((*f)[0]).prepare_for_reachability(negated) : nullptr;
        }

        Condition_ptr UntilCondition::prepare_for_reachability(bool negated) const {
            return nullptr;
        }

        Condition_ptr LogicalCondition::prepare_for_reachability(bool negated) const {
            return nullptr;
        }

        Condition_ptr CompareConjunction::prepare_for_reachability(bool negated) const {
            return nullptr;
        }

        Condition_ptr CompareCondition::prepare_for_reachability(bool negated) const {
            return nullptr;
        }

        Condition_ptr NotCondition::prepare_for_reachability(bool negated) const {
            return _cond->prepare_for_reachability(!negated);
        }

        Condition_ptr BooleanCondition::prepare_for_reachability(bool negated) const {
            return nullptr;
        }

        Condition_ptr DeadlockCondition::prepare_for_reachability(bool negated) const {
            return nullptr;
        }

        Condition_ptr UnfoldedUpperBoundsCondition::prepare_for_reachability(bool negated) const {
            return nullptr;
        }

/******************** Prepare CTL Queries ********************/

        Condition_ptr EGCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            ++stats[0];
            return AFCondition(std::make_shared<NotCondition>(_cond)).push_negation(stats, context, nested, !negated, initrw);
        }

        Condition_ptr AGCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            ++stats[1];
            return EFCondition(std::make_shared<NotCondition>(_cond)).push_negation(stats, context, nested, !negated, initrw);
        }

        Condition_ptr EXCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            auto a = _cond->push_negation(stats, context, true, negated, initrw);
            if(negated)
            {
                ++stats[2];
                return AXCondition(a).push_negation(stats, context, nested, false, initrw);
            }
            else
            {
                if(a == BooleanCondition::FALSE_CONSTANT)
                { ++stats[3]; return a;}
                if(a == BooleanCondition::TRUE_CONSTANT)
                { ++stats[4]; return std::make_shared<NotCondition>(DeadlockCondition::DEADLOCK); }
                a = std::make_shared<EXCondition>(a);
            }
            return a;
            }, stats, context, nested, negated, initrw);
        }

        Condition_ptr AXCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            auto a = _cond->push_negation(stats, context, true, negated, initrw);
            if(negated)
            {
                ++stats[5];
                return EXCondition(a).push_negation(stats, context, nested, false, initrw);
            }
            else
            {
                if(a == BooleanCondition::TRUE_CONSTANT)
                { ++stats[6]; return a;}
                if(a == BooleanCondition::FALSE_CONSTANT)
                { ++stats[7]; return DeadlockCondition::DEADLOCK; }
                a = std::make_shared<AXCondition>(a);
            }
            return a;
            }, stats, context, nested, negated, initrw);
        }


        Condition_ptr EFCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            auto a = _cond->push_negation(stats, context, true, false, initrw);

            if(auto cond = dynamic_cast<NotCondition*>(a.get()))
            {
                if((*cond)[0] == DeadlockCondition::DEADLOCK)
                {
                    ++stats[8];
                    return a->push_negation(stats, context, nested, negated, initrw);
                }
            }

            if(!a->is_temporal())
            {
                auto res = std::make_shared<EFCondition>(a);
                if(negated) return std::make_shared<NotCondition>(res);
                return res;
            }

            if( dynamic_cast<EFCondition*>(a.get()))
            {
                ++stats[9];
                if(negated) a = std::make_shared<NotCondition>(a);
                return a;
            }
            else if(auto cond = dynamic_cast<AFCondition*>(a.get()))
            {
                ++stats[10];
                a = EFCondition((*cond)[0]).push_negation(stats, context, nested, negated, initrw);
                return a;
            }
            else if(auto cond = dynamic_cast<EUCondition*>(a.get()))
            {
                ++stats[11];
                a = EFCondition((*cond)[1]).push_negation(stats, context, nested, negated, initrw);
                return a;
            }
            else if(auto cond = dynamic_cast<AUCondition*>(a.get()))
            {
                ++stats[12];
                a = EFCondition((*cond)[1]).push_negation(stats, context, nested, negated, initrw);
                return a;
            }
            else if(auto cond = dynamic_cast<OrCondition*>(a.get()))
            {
                if(!cond->is_temporal())
                {
                    Condition_ptr b = std::make_shared<EFCondition>(a);
                    if(negated) b = std::make_shared<NotCondition>(b);
                    return b;
                }
                ++stats[13];
                std::vector<Condition_ptr> pef, atomic;
                for(auto& i : *cond)
                {
                    pef.push_back(std::make_shared<EFCondition>(i));
                }
                a = makeOr(pef)->push_negation(stats, context, nested, negated, initrw);
                return a;
            }
            else
            {
                Condition_ptr b = std::make_shared<EFCondition>(a);
                if(negated) b = std::make_shared<NotCondition>(b);
                return b;
            }
            }, stats, context, nested, negated, initrw);
        }


        Condition_ptr AFCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            auto a = _cond->push_negation(stats, context, true, false, initrw);
            if(auto cond = dynamic_cast<NotCondition*>(a.get()))
            {
                if((*cond)[0] == DeadlockCondition::DEADLOCK)
                {
                    ++stats[14];
                    return a->push_negation(stats, context, nested, negated, initrw);
                }
            }

            if(dynamic_cast<AFCondition*>(a.get()))
            {
                ++stats[15];
                if(negated) return std::make_shared<NotCondition>(a);
                return a;

            }
            else if(dynamic_cast<EFCondition*>(a.get()))
            {
                ++stats[16];
                if(negated) return std::make_shared<NotCondition>(a);
                return a;
            }
            else if(auto cond = dynamic_cast<OrCondition*>(a.get()))
            {

                std::vector<Condition_ptr> pef, npef;
                for(auto& i : *cond)
                {
                    if(dynamic_cast<EFCondition*>(i.get()))
                    {
                        pef.push_back(i);
                    }
                    else
                    {
                        npef.push_back(i);
                    }
                }
                if(pef.size() > 0)
                {
                    stats[17] += pef.size();
                    pef.push_back(std::make_shared<AFCondition>(makeOr(npef)));
                    return makeOr(pef)->push_negation(stats, context, nested, negated, initrw);
                }
            }
            else if(auto cond = dynamic_cast<AUCondition*>(a.get()))
            {
                ++stats[18];
                return AFCondition((*cond)[1]).push_negation(stats, context, nested, negated, initrw);
            }
            auto b = std::make_shared<AFCondition>(a);
            if(negated) return std::make_shared<NotCondition>(b);
            return b;
            }, stats, context, nested, negated, initrw);
        }

        Condition_ptr AUCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            auto b = _cond2->push_negation(stats, context, true, false, initrw);
            auto a = _cond1->push_negation(stats, context, true, false, initrw);
            if(auto cond = dynamic_cast<NotCondition*>(b.get()))
            {
                if((*cond)[0] == DeadlockCondition::DEADLOCK)
                {
                    ++stats[19];
                    return b->push_negation(stats, context, nested, negated, initrw);
                }
            }
            else if(a == DeadlockCondition::DEADLOCK)
            {
                ++stats[20];
                return b->push_negation(stats, context, nested, negated, initrw);
            }
            else if(auto cond = dynamic_cast<NotCondition*>(a.get()))
            {
                if((*cond)[0] == DeadlockCondition::DEADLOCK)
                {
                    ++stats[21];
                    return AFCondition(b).push_negation(stats, context, nested, negated, initrw);
                }
            }

            if(auto cond = dynamic_cast<AFCondition*>(b.get()))
            {
                ++stats[22];
                return cond->push_negation(stats, context, nested, negated, initrw);
            }
            else if(dynamic_cast<EFCondition*>(b.get()))
            {
                ++stats[23];
                if(negated) return std::make_shared<NotCondition>(b);
                return b;
            }
            else if(auto cond = dynamic_cast<OrCondition*>(b.get()))
            {
                std::vector<Condition_ptr> pef, npef;
                for(auto& i : *cond)
                {
                    if(dynamic_cast<EFCondition*>(i.get()))
                    {
                        pef.push_back(i);
                    }
                    else
                    {
                        npef.push_back(i);
                    }
                }
                if(pef.size() > 0)
                {
                    stats[24] += pef.size();
                    if(npef.size() != 0)
                    {
                        pef.push_back(std::make_shared<AUCondition>(_cond1, makeOr(npef)));
                    }
                    else
                    {
                        ++stats[23];
                        --stats[24];
                    }
                    return makeOr(pef)->push_negation(stats, context, nested, negated, initrw);
                }
            }

            auto c = std::make_shared<AUCondition>(a, b);
            if(negated) return std::make_shared<NotCondition>(c);
            return c;
            }, stats, context, nested, negated, initrw);
        }


        Condition_ptr EUCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            auto b = _cond2->push_negation(stats, context, true, false, initrw);
            auto a = _cond1->push_negation(stats, context, true, false, initrw);

            if(auto cond = dynamic_cast<NotCondition*>(b.get()))
            {
                if((*cond)[0] == DeadlockCondition::DEADLOCK)
                {
                    ++stats[25];
                    return b->push_negation(stats, context, nested, negated, initrw);
                }
            }
            else if(a == DeadlockCondition::DEADLOCK)
            {
                ++stats[26];
                return b->push_negation(stats, context, nested, negated, initrw);
            }
            else if(auto cond = dynamic_cast<NotCondition*>(a.get()))
            {
                if((*cond)[0] == DeadlockCondition::DEADLOCK)
                {
                    ++stats[27];
                    return EFCondition(b).push_negation(stats, context, nested, negated, initrw);
                }
            }

            if(dynamic_cast<EFCondition*>(b.get()))
            {
                ++stats[28];
                if(negated) return std::make_shared<NotCondition>(b);
                return b;
            }
            else if(auto cond = dynamic_cast<OrCondition*>(b.get()))
            {
                std::vector<Condition_ptr> pef, npef;
                for(auto& i : *cond)
                {
                    if(dynamic_cast<EFCondition*>(i.get()))
                    {
                        pef.push_back(i);
                    }
                    else
                    {
                        npef.push_back(i);
                    }
                }
                if(pef.size() > 0)
                {
                    stats[29] += pef.size();
                    if(npef.size() != 0)
                    {
                        pef.push_back(std::make_shared<EUCondition>(_cond1, makeOr(npef)));
                        ++stats[28];
                        --stats[29];
                    }
                    return makeOr(pef)->push_negation(stats, context, nested, negated, initrw);
                }
            }
            auto c = std::make_shared<EUCondition>(a, b);
            if(negated) return std::make_shared<NotCondition>(c);
            return c;
            }, stats, context, nested, negated, initrw);
        }

        /*LTL negation push*/
        Condition_ptr
        UntilCondition::push_negation(negstat_t &stats, const EvaluationContext &context, bool nested, bool negated,
                                     bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
                auto b = _cond2->push_negation(stats, context, true, false, initrw);
                auto a = _cond1->push_negation(stats, context, true, false, initrw);

                if (auto cond = std::dynamic_pointer_cast<FCondition>(b)) {
                    static_assert(negstat_t::nrules >= 35);
                    ++stats[34];
                    if (negated)
                        return std::make_shared<NotCondition>(b);
                    return b;
                }

                auto c = std::make_shared<UntilCondition>(a, b);
                if(negated) return std::make_shared<NotCondition>(c);
                return c;
            }, stats, context, nested, negated, initrw);
        }

        Condition_ptr XCondition::push_negation(negstat_t &stats, const EvaluationContext &context, bool nested, bool negated,
                                               bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
               auto res = _cond->push_negation(stats, context, true, negated, initrw);
               if (res == BooleanCondition::TRUE_CONSTANT || res == BooleanCondition::FALSE_CONSTANT) {
                   return res;
               }
               return std::make_shared<XCondition>(res);
            }, stats, context, nested, negated, initrw);
        }

        Condition_ptr FCondition::push_negation(negstat_t &stats, const EvaluationContext &context, bool nested, bool negated,
                                               bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
                auto a = _cond->push_negation(stats, context, true, false, initrw);
                if(!a->is_temporal())
                {
                    auto res = std::make_shared<FCondition>(a);
                    if(negated) return std::make_shared<NotCondition>(res);
                    return res;
                }

                if (dynamic_cast<FCondition*>(a.get())) {
                    ++stats[31];
                    if (negated) a = std::make_shared<NotCondition>(a);
                    return a;
                }
                else if (auto cond = dynamic_cast<UntilCondition*>(a.get())) {
                    ++stats[32];
                    return FCondition((*cond)[1]).push_negation(stats, context, nested, negated, initrw);
                }
                else if (auto cond = dynamic_cast<OrCondition*>(a.get())) {
                    if(!cond->is_temporal())
                    {
                        Condition_ptr b = std::make_shared<FCondition>(a);
                        if(negated) b = std::make_shared<NotCondition>(b);
                        return b;
                    }
                    ++stats[33];
                    std::vector<Condition_ptr> distributed;
                    for (auto& i: *cond) {
                        distributed.push_back(std::make_shared<FCondition>(i));
                    }
                    return makeOr(distributed)->push_negation(stats, context, nested, negated, initrw);
                }
                else {
                    Condition_ptr b = std::make_shared<FCondition>(a);
                    if (negated) b = std::make_shared<NotCondition>(b);
                    return b;
                }
            }, stats, context, nested, negated, initrw);
        }

        Condition_ptr ACondition::push_negation(negstat_t &stats, const EvaluationContext &context, bool nested, bool negated,
                                               bool initrw) {
            return ECondition(std::make_shared<NotCondition>(_cond))
                .push_negation(stats, context, nested, !negated, initrw);
        }


        Condition_ptr ECondition::push_negation(negstat_t &stats, const EvaluationContext &context, bool nested, bool negated,
                                               bool initrw) {
            // we forward the negated flag, we flip the outer quantifier later!
            auto _sub = _cond->push_negation(stats, context, nested, negated, initrw);
            if(negated) return std::make_shared<ACondition>(_sub);
            else        return std::make_shared<ECondition>(_sub);
        }

        Condition_ptr GCondition::push_negation(negstat_t &stats, const EvaluationContext &context, bool nested, bool negated,
                                               bool initrw) {
            return FCondition(std::make_shared<NotCondition>(_cond)).push_negation(stats, context, nested, !negated, initrw);
        }

        /*Boolean connectives */
        Condition_ptr pushAnd(const std::vector<Condition_ptr>& _conds, negstat_t& stats, const EvaluationContext& context, bool nested, bool negate_children, bool initrw)
        {
            std::vector<Condition_ptr> nef, other;
            for(auto& c : _conds)
            {
                auto n = c->push_negation(stats, context, nested, negate_children, initrw);
                if(n->is_trivially_false()) return n;
                if(n->is_trivially_true()) continue;
                if(auto neg = dynamic_cast<NotCondition*>(n.get()))
                {
                    if(auto ef = dynamic_cast<EFCondition*>((*neg)[0].get()))
                    {
                        nef.push_back((*ef)[0]);
                    }
                    else
                    {
                        other.emplace_back(n);
                    }
                }
                else
                {
                    other.emplace_back(n);
                }
            }
            if(nef.size() + other.size() == 0)
                return BooleanCondition::TRUE_CONSTANT;
            if(nef.size() + other.size() == 1)
            {
                return nef.size() == 0 ?
                    other[0] :
                    std::make_shared<NotCondition>(std::make_shared<EFCondition>(nef[0]));
            }
            if(nef.size() != 0) other.push_back(
                    std::make_shared<NotCondition>(
                    std::make_shared<EFCondition>(
                    makeOr(nef))));
            if(other.size() == 1) return other[0];
            auto res = makeAnd(other);
            return res;
        }

        Condition_ptr pushOr(const std::vector<Condition_ptr>& _conds, negstat_t& stats, const EvaluationContext& context, bool nested, bool negate_children, bool initrw)
        {
            std::vector<Condition_ptr> nef, other;
            for(auto& c : _conds)
            {
                auto n = c->push_negation(stats, context, nested, negate_children, initrw);
                if(n->is_trivially_true())
                {
                    return n;
                }
                if(n->is_trivially_false()) continue;
                if(auto ef = dynamic_cast<EFCondition*>(n.get()))
                {
                    nef.push_back((*ef)[0]);
                }
                else
                {
                    other.emplace_back(n);
                }
            }
            if(nef.size() + other.size() == 0)
                return BooleanCondition::FALSE_CONSTANT;
            if(nef.size() + other.size() == 1) { return nef.size() == 0 ? other[0] : std::make_shared<EFCondition>(nef[0]);}
            if(nef.size() != 0) other.push_back(
                    std::make_shared<EFCondition>(
                    makeOr(nef)));
            if(other.size() == 1) return other[0];
            return makeOr(other);
        }

        Condition_ptr OrCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            return negated ? pushAnd(_conds, stats, context, nested, true, initrw) :
                             pushOr (_conds, stats, context, nested, false, initrw);
            }, stats, context, nested, negated, initrw);
        }


        Condition_ptr AndCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            return negated ? pushOr (_conds, stats, context, nested, true, initrw) :
                             pushAnd(_conds, stats, context, nested, false, initrw);

            }, stats, context, nested, negated, initrw);
        }

        Condition_ptr CompareConjunction::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            return std::make_shared<CompareConjunction>(*this, negated);
            }, stats, context, nested, negated, initrw);
        }


        Condition_ptr NotCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            if(negated) ++stats[30];
            return _cond->push_negation(stats, context, nested, !negated, initrw);
            }, stats, context, nested, negated, initrw);
        }

        template<typename T>
        Condition_ptr pushFireableNegation(negstat_t& stat, const EvaluationContext& context, bool nested, bool negated, bool initrw, const std::string& name, const Condition_ptr& compiled)
        {
            if(compiled)
                return compiled->push_negation(stat, context, nested, negated, initrw);
            if(negated)
            {
                stat._negated_fireability = true;
                return std::make_shared<NotCondition>(std::make_shared<T>(name));
            }
            else
                return std::make_shared<T>(name);
        }

        Condition_ptr UnfoldedFireableCondition::push_negation(negstat_t& stat, const EvaluationContext& context, bool nested, bool negated, bool initrw)
        {
            return pushFireableNegation<UnfoldedFireableCondition>(stat, context, nested, negated, initrw, _name, _compiled);
        }

        Condition_ptr FireableCondition::push_negation(negstat_t& stat, const EvaluationContext& context, bool nested, bool negated, bool initrw)
        {
            return pushFireableNegation<FireableCondition>(stat, context, nested, negated, initrw, _name, _compiled);
        }

        bool CompareCondition::is_trivial() const
        {
            auto remdup = [](auto& a, auto& b){
                auto ai = a->_ids.begin();
                auto bi = b->_ids.begin();
                while(ai != a->_ids.end() && bi != b->_ids.end())
                {
                    while(ai != a->_ids.end() && ai->first < bi->first) ++ai;
                    if(ai == a->_ids.end()) break;
                    if(ai->first == bi->first)
                    {
                        ai = a->_ids.erase(ai);
                        bi = b->_ids.erase(bi);
                    }
                    else
                    {
                        ++bi;
                    }
                    if(bi == b->_ids.end() || ai == a->_ids.end()) break;
                }
            };
            if(auto p1 = std::dynamic_pointer_cast<PlusExpr>(_expr1))
                if(auto p2 = std::dynamic_pointer_cast<PlusExpr>(_expr2))
                    remdup(p1, p2);

            if(auto m1 = std::dynamic_pointer_cast<MultiplyExpr>(_expr1))
                if(auto m2 = std::dynamic_pointer_cast<MultiplyExpr>(_expr2))
                    remdup(m1, m2);

            if(auto p1 = std::dynamic_pointer_cast<CommutativeExpr>(_expr1))
                if(auto p2 = std::dynamic_pointer_cast<CommutativeExpr>(_expr2))
                    return p1->_exprs.size() + p1->_ids.size() + p2->_exprs.size() + p2->_ids.size() == 0;
            return _expr1->place_free() && _expr2->place_free();
        }

        Condition_ptr LessThanCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            if(is_trivial())
                return BooleanCondition::getShared(evaluate(context) xor negated);
            if(negated) return std::make_shared<LessThanOrEqualCondition>(_expr2, _expr1);
            else        return std::make_shared<LessThanCondition>(_expr1, _expr2);
            }, stats, context, nested, negated, initrw);
        }

        Condition_ptr LessThanOrEqualCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            if(is_trivial())
                return BooleanCondition::getShared(evaluate(context) xor negated);
            if(negated) return std::make_shared<LessThanCondition>(_expr2, _expr1);
            else        return std::make_shared<LessThanOrEqualCondition>(_expr1, _expr2);
            }, stats, context, nested, negated, initrw);
        }

        Condition_ptr pushEqual(CompareCondition* org, bool negated, bool noteq, const EvaluationContext& context)
        {
            if(org->is_trivial())
                return BooleanCondition::getShared(org->evaluate(context) xor negated);
            for(auto i : {0,1})
            {
                if((*org)[i]->place_free() && (*org)[i]->evaluate(context) == 0)
                {
                    if(negated == noteq) return std::make_shared<LessThanOrEqualCondition>((*org)[(i + 1) % 2], std::make_shared<LiteralExpr>(0));
                    else                 return std::make_shared<LessThanOrEqualCondition>(std::make_shared<LiteralExpr>(1), (*org)[(i + 1) % 2]);
                }
            }
            if(negated == noteq) return std::make_shared<EqualCondition>((*org)[0], (*org)[1]);
            else                 return std::make_shared<NotEqualCondition>((*org)[0], (*org)[1]);
        }

        Condition_ptr NotEqualCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
                return pushEqual(this, negated, true, context);
            }, stats, context, nested, negated, initrw);
        }


        Condition_ptr EqualCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
                return pushEqual(this, negated, false, context);
            }, stats, context, nested, negated, initrw);
        }

        Condition_ptr BooleanCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            if(negated) return getShared(!value);
            else        return getShared(value);
            }, stats, context, nested, negated, initrw);
        }

        Condition_ptr DeadlockCondition::push_negation(negstat_t& stats, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            return initial_marking_rewrite([&]() -> Condition_ptr {
            if(negated) return std::make_shared<NotCondition>(DEADLOCK);
            else        return DEADLOCK;
            }, stats, context, nested, negated, initrw);
        }

        Condition_ptr UnfoldedUpperBoundsCondition::push_negation(negstat_t&, const EvaluationContext& context, bool nested, bool negated, bool initrw) {
            if(negated)
            {
                throw base_error("UPPER BOUNDS CANNOT BE NEGATED!");
            }
            return std::make_shared<UnfoldedUpperBoundsCondition>(_places, _max, _offset);
        }


        /********************** CONSTRUCTORS *********************************/

        void postMerge(std::vector<Condition_ptr>& conds) {
            std::sort(std::begin(conds), std::end(conds),
                    [](auto& a, auto& b) {
                        return a->is_temporal() < b->is_temporal();
                    });
        }

        AndCondition::AndCondition(std::vector<Condition_ptr>&& conds) {
            for (auto& c : conds) tryMerge<AndCondition>(_conds, c);
            for (auto& c : _conds) _temporal = _temporal || c->is_temporal();
            for (auto& c : _conds) _loop_sensitive = _loop_sensitive || c->is_loop_sensitive();
            postMerge(_conds);
        }

        AndCondition::AndCondition(const std::vector<Condition_ptr>& conds) {
            for (auto& c : conds) tryMerge<AndCondition>(_conds, c);
            for (auto& c : _conds) _temporal = _temporal || c->is_temporal();
            for (auto& c : _conds) _loop_sensitive = _loop_sensitive || c->is_loop_sensitive();
            postMerge(_conds);
        }

        AndCondition::AndCondition(Condition_ptr left, Condition_ptr right) {
            tryMerge<AndCondition>(_conds, left);
            tryMerge<AndCondition>(_conds, right);
            for (auto& c : _conds) _temporal = _temporal || c->is_temporal();
            for (auto& c : _conds) _loop_sensitive = _loop_sensitive || c->is_loop_sensitive();
            postMerge(_conds);
        }

        OrCondition::OrCondition(std::vector<Condition_ptr>&& conds) {
            for (auto& c : conds) tryMerge<OrCondition>(_conds, c);
            for (auto& c : _conds) _temporal = _temporal || c->is_temporal();
            for (auto& c : _conds) _loop_sensitive = _loop_sensitive || c->is_loop_sensitive();
            postMerge(_conds);
        }

        OrCondition::OrCondition(const std::vector<Condition_ptr>& conds) {
            for (auto& c : conds) tryMerge<OrCondition>(_conds, c);
            for (auto& c : _conds) _temporal = _temporal || c->is_temporal();
            for (auto& c : _conds) _loop_sensitive = _loop_sensitive || c->is_loop_sensitive();
            postMerge(_conds);
        }

        OrCondition::OrCondition(Condition_ptr left, Condition_ptr right) {
            tryMerge<OrCondition>(_conds, left);
            tryMerge<OrCondition>(_conds, right);
            for (auto& c : _conds) _temporal = _temporal || c->is_temporal();
            for (auto& c : _conds) _loop_sensitive = _loop_sensitive || c->is_loop_sensitive();
            postMerge(_conds);
        }


        CompareConjunction::CompareConjunction(const std::vector<Condition_ptr>& conditions, bool negated)
        {
            _negated = negated;
            merge(conditions, negated);
        }

        void CompareConjunction::merge(const CompareConjunction& other)
        {
            auto neg = _negated != other._negated;
            if(neg && other._constraints.size() > 1)
            {
                throw base_error("MERGE OF CONJUNCT AND DISJUNCT NOT ALLOWED");
            }
            auto il = _constraints.begin();
            for(auto c : other._constraints)
            {
                if(neg)
                    c.invert();

                if(c._upper == std::numeric_limits<uint32_t>::max() && c._lower == 0)
                {
                    continue;
                }
                else if (c._upper != std::numeric_limits<uint32_t>::max() && c._lower != 0 && neg)
                {
                    throw base_error(ErrorCode,"MERGE OF CONJUNCT AND DISJUNCT NOT ALLOWED");
                }

                il = std::lower_bound(_constraints.begin(), _constraints.end(), c);
                if(il == _constraints.end() || il->_place != c._place)
                {
                    il = _constraints.insert(il, c);
                }
                else
                {
                    il->_lower = std::max(il->_lower, c._lower);
                    il->_upper = std::min(il->_upper, c._upper);
                }
            }
        }

        void CompareConjunction::merge(const std::vector<Condition_ptr>& conditions, bool negated)
        {
            for(auto& c : conditions)
            {
                auto cmp = dynamic_cast<CompareCondition*>(c.get());
                assert(cmp);
                auto id = dynamic_cast<UnfoldedIdentifierExpr*>((*cmp)[0].get());
                uint32_t val;
                bool inverted = false;
                EvaluationContext context;
                if(!id)
                {
                    id = dynamic_cast<UnfoldedIdentifierExpr*>((*cmp)[1].get());
                    val = (*cmp)[0]->evaluate(context);
                    inverted = true;
                }
                else
                {
                    val = (*cmp)[1]->evaluate(context);
                }
                assert(id);
                cons_t next;
                next._place = id->offset();

                if(dynamic_cast<LessThanOrEqualCondition*>(c.get()))
                    if(inverted) next._lower = val;
                    else         next._upper = val;
                else if(dynamic_cast<LessThanCondition*>(c.get()))
                    if(inverted) next._lower = val+1;
                    else         next._upper = val-1;
                else if(dynamic_cast<EqualCondition*>(c.get()))
                {
                    assert(!negated);
                    next._lower = val;
                    next._upper = val;
                }
                else if(dynamic_cast<NotEqualCondition*>(c.get()))
                {
                    assert(negated);
                    next._lower = val;
                    next._upper = val;
                    negated = false; // we already handled negation here!
                }
                else
                {
                    throw base_error("Unknown Error in CompareConjunction::merge");
                }
                if(negated)
                    next.invert();

                auto lb = std::lower_bound(std::begin(_constraints), std::end(_constraints), next);
                if(lb == std::end(_constraints) || lb->_place != next._place)
                {
                    next._name = id->name();
                    _constraints.insert(lb, next);
                }
                else
                {
                    assert(id->name().compare(lb->_name) == 0);
                    lb->intersect(next);
                }
            }
        }

        void CommutativeExpr::init(std::vector<Expr_ptr>&& exprs)
        {
            for (auto& e : exprs) {
                if (e->place_free())
                {
                    EvaluationContext c;
                    _constant = apply(_constant, e->evaluate(c));
                }
                else if (auto id = std::dynamic_pointer_cast<PQL::UnfoldedIdentifierExpr>(e)) {
                    _ids.emplace_back(id->offset(), id->name());
                }
                else if(auto c = std::dynamic_pointer_cast<CommutativeExpr>(e))
                {
                    // we should move up plus/multiply here when possible;
                    if(c->_ids.size() == 0 && c->_exprs.size() == 0)
                    {
                        _constant = apply(_constant, c->_constant);
                    }
                    else
                    {
                        _exprs.emplace_back(std::move(e));
                    }
                } else {
                    _exprs.emplace_back(std::move(e));
                }
            }
        }

        PlusExpr::PlusExpr(std::vector<Expr_ptr>&& exprs, bool tk) : CommutativeExpr(0), _tk(tk)
        {
            init(std::move(exprs));
        }

        MultiplyExpr::MultiplyExpr(std::vector<Expr_ptr>&& exprs) : CommutativeExpr(1)
        {
            init(std::move(exprs));
        }

        bool LogicalCondition::nested_deadlock() const {
            for(auto& c : _conds)
            {
                if(c->get_quantifier() == PQL::DEADLOCK ||
                   c->nested_deadlock() ||
                    (c->get_quantifier() == PQL::NEG &&
                     (*static_cast<NotCondition*>(c.get()))[0]->get_quantifier() == PQL::DEADLOCK
                        ))
                {
                    return true;
                }
            }
            return false;
        }

    } // PQL
} // PetriEngine

