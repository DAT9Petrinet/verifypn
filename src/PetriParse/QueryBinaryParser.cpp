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

#include <memory>

#include "PetriParse/QueryBinaryParser.h"
#include "PetriEngine/PQL/Expressions.h"

bool QueryBinaryParser::parse(std::ifstream& bin, const std::set<size_t>& parse_only) {
    //Parse the xml
    uint32_t numq;
    bin.read(reinterpret_cast<char*>(&numq), sizeof(uint32_t));
    std::vector<std::string> names;
    uint32_t nnames;
    bin.read(reinterpret_cast<char*>(&nnames), sizeof(uint32_t));
    names.resize(nnames);
    for(uint32_t i = 0; i < nnames; ++i)
    {
        uint32_t id;
        bin.read(reinterpret_cast<char*>(&id), sizeof(uint32_t));
        std::getline(bin, names[id], '\0');
    }

    bool parsingOK = true;
    for(uint32_t i = 0; i < numq; ++i)
    {
        _queries.emplace_back();
        std::getline(bin, _queries.back()._id, '\0');
        _queries.back()._query = parse_query(bin, names);
        if(_queries.back()._query == nullptr)
        {
            _queries.back()._parsing_result = QueryItem::UNSUPPORTED_QUERY;
            parsingOK = false;
        }
        else
            _queries.back()._parsing_result = QueryItem::PARSING_OK;
    }
    //Release DOM tree
    return parsingOK;
}

Condition_ptr QueryBinaryParser::parse_query(std::ifstream& binary, const std::vector<std::string>& names)
{
    Path p;
    binary.read(reinterpret_cast<char*>(&p), sizeof(Path));
    Quantifier q;
    binary.read(reinterpret_cast<char*>(&q), sizeof(Quantifier));
    if(p == pError)
    {
        if(q == Quantifier::NEG)
        {
            auto c = parse_query(binary, names);
            if(c == nullptr)
            {
                assert(false);
                return nullptr;
            }
            return std::make_shared<NotCondition>(c);
        }
        else if(q == Quantifier::DEADLOCK)
        {
            return DeadlockCondition::DEADLOCK;
        }
        else if(q == Quantifier::PN_BOOLEAN)
        {
            bool val;
            binary.read(reinterpret_cast<char*>(&val), sizeof(bool));
            return BooleanCondition::getShared(val);
        }
        else if(q == Quantifier::AND || q == Quantifier::OR)
        {
            uint32_t size;
            binary.read(reinterpret_cast<char*>(&size), sizeof(uint32_t));
            std::vector<Condition_ptr> conds;
            for(uint32_t i = 0; i < size; ++i)
            {
                conds.push_back(parse_query(binary, names));
                if(conds.back() == nullptr) return nullptr;
            }
            if(q == Quantifier::AND)
                return std::make_shared<AndCondition>(std::move(conds));
            else
                return std::make_shared<OrCondition>(std::move(conds));
        }
        else if(q == Quantifier::COMPCONJ)
        {
            bool neg;
            binary.read(reinterpret_cast<char*>(&neg), sizeof(bool));   
            std::vector<CompareConjunction::cons_t> cons;
            uint32_t size;
            binary.read(reinterpret_cast<char*>(&size), sizeof(uint32_t));            
            for(uint32_t i = 0; i < size; ++i)
            {
                cons.emplace_back();
                binary.read(reinterpret_cast<char*>(&cons.back()._place), sizeof(uint32_t));
                binary.read(reinterpret_cast<char*>(&cons.back()._lower), sizeof(uint32_t));
                binary.read(reinterpret_cast<char*>(&cons.back()._upper), sizeof(uint32_t));
                cons.back()._name = names[cons.back()._place];
            }
            return std::make_shared<CompareConjunction>(std::move(cons), neg);
        }
        else if(q == Quantifier::EMPTY)
        {
            std::string sop;
            std::getline(binary, sop, '\0');
            auto e1 = parse_expr(binary, names);
            auto e2 = parse_expr(binary, names);
            if(e1 == nullptr || e2 == nullptr)
            {
                assert(false);
                return nullptr;
            }
            if(sop == "<")
                return std::make_shared<LessThanCondition>(e1, e2);
            else if(sop == "<=")
                return std::make_shared<LessThanOrEqualCondition>(e1, e2);
            else if(sop == ">=")
                return std::make_shared<LessThanOrEqualCondition>(e2, e1);
            else if(sop == ">")
                return std::make_shared<LessThanCondition>(e2, e1);
            else if(sop == "==")
                return std::make_shared<EqualCondition>(e1, e2);
            else if(sop == "!=")
                return std::make_shared<NotEqualCondition>(e1, e2);
            else
            {
                assert(false);
                return nullptr;
            }
        }
        else if(q == Quantifier::UPPERBOUNDS)
        {
            uint32_t size;
            double max, offset;
            binary.read(reinterpret_cast<char*>(&size), sizeof(uint32_t));            
            binary.read(reinterpret_cast<char*>(&max), sizeof(double));            
            binary.read(reinterpret_cast<char*>(&offset), sizeof(double));   
            std::vector<UnfoldedUpperBoundsCondition::place_t> places;
            for(size_t i = 0; i < size; ++i)
            {
                uint32_t id;
                double pmax;
                binary.read(reinterpret_cast<char*>(&id), sizeof(uint32_t));
                binary.read(reinterpret_cast<char*>(&pmax), sizeof(double));
                places.emplace_back(names[id]);
                places.back()._place = id;
                places.back()._max = pmax;
            }
            return std::make_shared<UnfoldedUpperBoundsCondition>(places, max, offset);
        }
        else if (q == Quantifier::A || q == Quantifier::E)
        {
            Condition_ptr cond1 = parse_query(binary, names);
            assert(cond1);
            if (!cond1) return nullptr;
            if (q == Quantifier::A)
                return std::make_shared<ACondition>(cond1);
            else
                return std::make_shared<ECondition>(cond1);
        }
        else
        {
            assert(false);
            return nullptr;
        }
    }
    else
    {
        Condition_ptr cond1 = parse_query(binary, names);
        assert(cond1);
        if(!cond1) return nullptr;
        if(p == Path::X)
        {
            if (q == Quantifier::EMPTY)
                return std::make_shared<XCondition>(cond1);
            else if(q == Quantifier::A)
                return std::make_shared<AXCondition>(cond1);
            else
                return std::make_shared<EXCondition>(cond1);
        }
        else if(p == Path::F)
        {
            if (q == Quantifier::EMPTY)
                return std::make_shared<FCondition>(cond1);
            else if(q == Quantifier::A)
                return std::make_shared<AFCondition>(cond1);
            else
                return std::make_shared<EFCondition>(cond1);
        }
        else if(p == Path::G)
        {
            if (q == Quantifier::EMPTY)
                return std::make_shared<GCondition>(cond1);
            else if(q == Quantifier::A)
                return std::make_shared<AGCondition>(cond1);
            else
                return std::make_shared<EGCondition>(cond1);
        }
        else if(p == Path::U)
        {
            auto cond2 = parse_query(binary, names);
            if(cond2 == nullptr)
            {
                assert(false);
                return nullptr;
            }
            if (q == Quantifier::EMPTY)
                return std::make_shared<UntilCondition>(cond1, cond2);
            else if(q == Quantifier::A)
                return std::make_shared<AUCondition>(cond1, cond2);
            else
                return std::make_shared<EUCondition>(cond1, cond2);
        }
        else
        {
            assert(false);
            return nullptr;
        }
    }
    assert(false);
    return nullptr;
}

Expr_ptr QueryBinaryParser::parse_expr(std::ifstream& bin, const std::vector<std::string>& names) {
    char t;
    bin.read(&t, sizeof(char));
    if(t == 'l')
    {
        int val;
        bin.read(reinterpret_cast<char*>(&val), sizeof(int));
        return std::make_shared<LiteralExpr>(val);
    }
    else if(t == 'i')
    {
        int offset;
        bin.read(reinterpret_cast<char*>(&offset), sizeof(int));
        return std::make_shared<UnfoldedIdentifierExpr>(names[offset], offset);
    }
    else if(t == '-')
    {
        uint32_t size;
        bin.read(reinterpret_cast<char*>(&size), sizeof(uint32_t));  
        std::vector<Expr_ptr> exprs;
        for(uint32_t i = 0; i < size; ++i)
        {
            exprs.push_back(parse_expr(bin, names));
            if(exprs.back() == nullptr)
            {
                assert(false);
                return nullptr;
            }
        }
        return std::make_shared<SubtractExpr>(std::move(exprs));
    }
    else if(t == '*' || t == '+')
    {
        int32_t constant;
        uint32_t idsize;
        uint32_t exprssize;
        bin.read(reinterpret_cast<char*>(&constant), sizeof(int32_t));  
        bin.read(reinterpret_cast<char*>(&idsize), sizeof(uint32_t));  
        bin.read(reinterpret_cast<char*>(&exprssize), sizeof(uint32_t));  
        std::vector<uint32_t> ids(idsize);
        bin.read(reinterpret_cast<char*>(ids.data()), sizeof(uint32_t)*idsize);
        std::vector<Expr_ptr> exprs;
        exprs.push_back(std::make_shared<LiteralExpr>(constant));
        for(auto i : ids)
        {
            exprs.push_back(std::make_shared<UnfoldedIdentifierExpr>(names[i], i));
        }
        for(uint32_t i = 0; i < exprssize; ++i)
        {
            exprs.push_back(parse_expr(bin, names));
            if(exprs.back() == nullptr)
            {
                assert(false);
                return nullptr;
            }
        }
        if(t == '*')
            return std::make_shared<MultiplyExpr>(std::move(exprs));
        else
            return std::make_shared<PlusExpr>(std::move(exprs));
        
    }
    else
    {
        std::cerr << "An error occurred parsing the binary input." << std::endl;
        std::cerr << t << std::endl;
        assert(false);
        exit(ErrorCode);
        return nullptr;
    }
}
