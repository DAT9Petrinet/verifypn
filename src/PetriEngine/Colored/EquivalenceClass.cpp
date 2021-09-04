#include "PetriEngine/Colored/EquivalenceClass.h"

namespace PetriEngine {
    namespace Colored {


        EquivalenceClass::EquivalenceClass(uint32_t id) : _id(id){
        }
        EquivalenceClass::EquivalenceClass(uint32_t id, const ColorType *colorType)
        : _id(id), _colorType(colorType){
        }
        EquivalenceClass::EquivalenceClass(uint32_t id, const ColorType *colorType, interval_vector_t&& colorIntervals)
        : _id(id), _colorType(colorType), _colorIntervals(std::move(colorIntervals)){
        }

        EquivalenceClass EquivalenceClass::intersect(uint32_t id, const EquivalenceClass &other) const{
            EquivalenceClass result = EquivalenceClass(id);

            if(_colorType != other._colorType){
                return result;
            }
            result._colorType = _colorType;

            for(const auto& interval : _colorIntervals){
                for(const auto& otherInterval : other._colorIntervals){
                    auto overlappingInterval = interval.get_overlap(otherInterval);
                    if(overlappingInterval.is_sound()){
                        result._colorIntervals.add_interval(overlappingInterval);
                    }
                }
            }
            return result;
        }


        EquivalenceClass EquivalenceClass::subtract(uint32_t id, const EquivalenceClass &other, const std::vector<bool> &diagonalPositions) const{
            EquivalenceClass result = EquivalenceClass(id);
            if(_colorType != other._colorType){
                return result;
            }
            result._colorType = _colorType;
            interval_vector_t resIntervals;
            for(const auto& interval : _colorIntervals){
                interval_vector_t intervalSubRes;
                for(const auto& otherInterval : other._colorIntervals){
                    auto subtractedIntervals = interval.get_substracted(otherInterval, diagonalPositions);
                    

                    if(subtractedIntervals.empty() || subtractedIntervals[0].size() == 0){
                        intervalSubRes.clear();
                        break;                           
                    }

                    if(intervalSubRes.empty()){
                        intervalSubRes = subtractedIntervals;
                    } else {
                        interval_vector_t newIntervals;
                        for(const auto& newInterval : subtractedIntervals){
                            for(const auto& interval : intervalSubRes){
                                auto intersection = interval.get_overlap(newInterval);
                                if(intersection.is_sound()){
                                    newIntervals.add_interval(intersection);
                                }
                            }
                        }
                        intervalSubRes = std::move(newIntervals);                              
                    }
                }
                for(const auto& interval : intervalSubRes){
                    resIntervals.add_interval(interval);
                }                
            }
            result._colorIntervals = resIntervals;                  
            return result;
        }

        bool EquivalenceClass::contains_color(const std::vector<uint32_t> &ids, const std::vector<bool> &diagonalPositions) const {
            if(ids.size() != _colorIntervals.front().size()){
                return false;
            }
            for(const auto &interval : _colorIntervals){
                bool contained = true;
                for(uint32_t i = 0; i < ids.size(); i++){
                    if(!diagonalPositions[i] && !interval[i].contains(ids[i])){
                        contained = false;
                        break;
                    }
                }
                if(contained){
                    return true;
                }
            }
            return false;
        }

        size_t EquivalenceClass::size() const{
            size_t result = 0;
            for(const auto& interval : _colorIntervals){
                size_t intervalSize = 1;
                for(const auto& range : interval._ranges){
                    intervalSize *= range.size();
                }
                result += intervalSize;
            }
            return result;
        }
    }
}