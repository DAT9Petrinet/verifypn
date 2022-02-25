/* Copyright (C) 2020  Alexander Bilgram <alexander@bilgram.dk>,
 *                     Peter Haar Taankvist <ptaankvist@gmail.com>,
 *                     Thomas Pedersen <thomas.pedersen@stofanet.dk>
 *                     Andreas H. Klostergaard
 *                     Peter G. Jensen
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


/*
 * File:   Unfolder.h
 * Author: Peter G. Jensen
 *
 * Created on 12 February 2022, 00.14
 */

#ifndef UNFOLDER_H
#define UNFOLDER_H

#include "PartitionBuilder.h"
#include "SymmetryVisitor.h"
#include "ForwardFixedPoint.h"
#include "PetriEngine/PetriNetBuilder.h"
#include "VariableSymmetry.h"
#include "StablePlaceFinder.h"


namespace PetriEngine {
    class ColoredPetriNetBuilder;
    namespace Colored {
        typedef std::unordered_map<std::string, std::unordered_map<uint32_t , std::string>> PTPlaceMap;
        typedef std::unordered_map<std::string, std::vector<std::string>> PTTransitionMap;

        class Unfolder {
        private:
            const ColoredPetriNetBuilder& _builder;
            void getArcIntervals(const Colored::Transition& transition, bool &transitionActivated, uint32_t max_intervals, uint32_t transitionId);

            void unfoldPlace(PetriNetBuilder& ptBuilder, const Colored::Place* place, const PetriEngine::Colored::Color *color, uint32_t unfoldPlace, uint32_t id);
            void unfoldTransition(PetriNetBuilder& builder, uint32_t transitionId);
            void handleOrphanPlace(PetriNetBuilder& ptBuilder, const Colored::Place& place, const std::unordered_map<std::string, uint32_t> &unfoldedPlaceMap);
            void createPartionVarmaps();
            void unfoldInhibitorArc(PetriNetBuilder& ptBuilder, const std::string &oldname, const std::string &newname);
            std::string arc_to_string(const Colored::Arc& arc) const;
            void unfoldArc(PetriNetBuilder& ptBuilder, const Colored::Arc& arc, const Colored::BindingMap& binding, const std::string& name);
            Colored::StablePlaceFinder _stable;
            double _time = 0;
            PTPlaceMap _ptplacenames;
            PTTransitionMap _pttransitionnames;
            uint32_t _nptarcs = 0;
            std::vector<std::string> _sumPlacesNames;
            const VariableSymmetry& _symmetry;
            const PartitionBuilder& _partition;
            const ForwardFixedPoint& _fixed_point;


        public:
            Unfolder(const ColoredPetriNetBuilder& b, const PartitionBuilder& partition, const VariableSymmetry& symmetry, const ForwardFixedPoint& fixed_point)
            : _builder(b),
              _stable(b),
              _symmetry(symmetry),
              _partition(partition),
              _fixed_point(fixed_point) {}

            PetriNetBuilder unfold();

            size_t number_of_arcs() const { return _nptarcs; }

            Colored::PTPlaceMap place_names() const {
                return _ptplacenames;
            }

            Colored::PTTransitionMap transition_names() const {
                return _pttransitionnames;
            }

            double time() const {
                return _time;
            }

            PetriNetBuilder strip_colors();

        };
    }
}

#endif /* UNFOLDER_H */

