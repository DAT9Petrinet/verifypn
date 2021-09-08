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
#ifndef ABSTRACTPETRINETBUILDER_H
#define ABSTRACTPETRINETBUILDER_H

#include <string>

#include "Colored/Expressions.h"
#include "Colored/Multiset.h"
#include "PQL/PQL.h"

namespace PetriEngine {
/** Abstract builder for petri nets */
class AbstractPetriNetBuilder {
  protected:
    bool _isColored = false;
    AbstractPetriNetBuilder(const AbstractPetriNetBuilder&) = default;
    AbstractPetriNetBuilder() = default;
  public:
    /** Add a new place with a unique name */
    virtual void add_place(const std::string &name, uint32_t tokens, double x = 0, double y = 0) = 0;
    /** Add a new colored place with a unique name */
    virtual void add_place(const std::string &name, const Colored::ColorType *type,
                           Colored::Multiset &&tokens, double x = 0, double y = 0) {
        throw base_error("Colored places are not supported in standard P/T nets");
    }
    /** Add a new transition with a unique name */
    virtual void add_transition(const std::string &name, double x = 0, double y = 0) = 0;
    /** Add a new colored transition with a unique name */
    virtual void add_transition(const std::string &name, const Colored::GuardExpression_ptr &guard,
                                double x = 0, double y = 0) {
        throw base_error("Colored transitions are not supported in standard P/T nets");
    }
    /** Add input arc with given weight */
    virtual void add_input_arc(const std::string &place, const std::string &transition,
                               bool inhibitor, uint32_t) = 0;
    /** Add colored input arc with given arc expression */
    virtual void add_input_arc(const std::string &place, const std::string &transition,
                               const Colored::ArcExpression_ptr &expr, bool inhibitor, uint32_t weight) {
        throw base_error("Colored input arcs are not supported in standard P/T nets");
    }
    /** Add output arc with given weight */
    virtual void add_output_arc(const std::string &transition, const std::string &place,
                                uint32_t weight = 1) = 0;
    /** Add output arc with given arc expression */
    virtual void add_output_arc(const std::string &transition, const std::string &place,
                                const Colored::ArcExpression_ptr &expr) {
        throw base_error("Colored output arcs are not supported in standard P/T nets");
    }
    /** Add color types with id */
    virtual void add_color_type(const std::string &id, const Colored::ColorType *type) {
        throw base_error("Color types are not supported in standard P/T nets");
    }

    virtual void enable_colors() { _isColored = true; }

    [[nodiscard]] virtual bool is_colored() const { return _isColored; }

    virtual void sort() = 0;

    virtual ~AbstractPetriNetBuilder() = default;
};

} // namespace PetriEngine

#endif // ABSTRACTPETRINETBUILDER_H
