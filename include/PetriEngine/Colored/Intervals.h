/* Copyright (C) 2020  Alexander Bilgram <alexander@bilgram.dk>,
 *                     Peter Haar Taankvist <ptaankvist@gmail.com>,
 *                     Thomas Pedersen <thomas.pedersen@stofanet.dk>
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

#ifndef INTERVALS_H
#define INTERVALS_H

#include "../TAR/range.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string.h>
#include <string>
#include <unordered_map>

namespace PetriEngine {
namespace Colored {

struct interval_t {
    std::vector<Reachability::range_t> _ranges;

    interval_t() {}

    ~interval_t() {}

    interval_t(const std::vector<Reachability::range_t> &ranges) : _ranges(ranges) {}

    size_t size() const { return _ranges.size(); }

    bool is_sound() const {
        for (const auto &range : _ranges) {
            if (!range.is_sound()) {
                return false;
            }
        }
        return true;
    }

    void add_range(Reachability::range_t &&newRange) { _ranges.emplace_back(newRange); }

    void add_range(const Reachability::range_t &newRange) { _ranges.emplace_back(newRange); }

    void add_range(Reachability::range_t newRange, uint32_t index) {
        _ranges.insert(_ranges.begin() + index, newRange);
    }

    void add_range(uint32_t l, uint32_t u) { _ranges.emplace_back(l, u); }

    Reachability::range_t &operator[](size_t index) {
        assert(index < _ranges.size());
        return _ranges[index];
    }

    const Reachability::range_t &operator[](size_t index) const { return _ranges[index]; }

    std::vector<uint32_t> get_lower_ids() const {
        std::vector<uint32_t> ids;
        for (auto &range : _ranges) {
            ids.push_back(range._lower);
        }
        return ids;
    }

    interval_t get_single_color_interval() const {
        interval_t newInterval;
        for (auto &range : _ranges) {
            newInterval.add_range(range._lower, range._lower);
        }
        return newInterval;
    }

    bool equals(const interval_t &other) const {
        if (other.size() != size()) {
            return false;
        }
        for (uint32_t i = 0; i < size(); i++) {
            auto comparisonRes = _ranges[i].compare(other[i]);
            if (!comparisonRes.first || !comparisonRes.second) {
                return false;
            }
        }
        return true;
    }

    uint32_t get_contained_colors() const {
        uint32_t colors = 1;
        for (const auto &range : _ranges) {
            colors *= 1 + range._upper - range._lower;
        }
        return colors;
    }

    bool contains(const interval_t &other, const std::vector<bool> &diagonalPositions) const {
        if (other.size() != size()) {
            return false;
        }
        for (uint32_t i = 0; i < size(); i++) {
            if (!diagonalPositions[i] && !_ranges[i].compare(other[i]).first) {
                return false;
            }
        }
        return true;
    }

    interval_t get_overlap(const interval_t &other) const {
        interval_t overlapInterval;
        if (size() != other.size()) {
            return overlapInterval;
        }

        for (uint32_t i = 0; i < size(); i++) {
            auto rangeCopy = _ranges[i];
            overlapInterval.add_range(rangeCopy &= other[i]);
        }

        return overlapInterval;
    }

    interval_t get_overlap(const interval_t &other,
                           const std::vector<bool> &diagonalPositions) const {
        interval_t overlapInterval;
        if (size() != other.size()) {
            return overlapInterval;
        }

        for (uint32_t i = 0; i < size(); i++) {
            if (diagonalPositions[i]) {
                overlapInterval.add_range(_ranges[i]);
            } else {
                auto rangeCopy = _ranges[i];
                overlapInterval.add_range(rangeCopy &= other[i]);
            }
        }

        return overlapInterval;
    }

    interval_t &operator|=(const interval_t &other) {
        assert(size() == other.size());
        for (uint32_t l = 0; l < size(); ++l) {
            _ranges[l] |= other[l];
        }
        return *this;
    }

    bool intersects(const interval_t &otherInterval) const {
        assert(size() == otherInterval.size());
        for (uint32_t k = 0; k < size(); k++) {
            if (!_ranges[k].intersects(otherInterval[k])) {
                return false;
            }
        }
        return true;
    }

    std::vector<interval_t> get_substracted(const interval_t &other,
                                            const std::vector<bool> &diagonalPositions) const {
        std::vector<interval_t> result;

        if (size() != other.size()) {
            return result;
        }

        for (uint32_t i = 0; i < size(); i++) {
            interval_t newInterval = *this;
            if (diagonalPositions[i]) {
                continue;
            }

            int32_t newMinUpper = std::min(((int)other[i]._lower) - 1, (int)_ranges[i]._upper);
            uint32_t newMaxLower = std::max(other[i]._upper + 1, _ranges[i]._lower);

            if (((int32_t)_ranges[i]._lower) <= newMinUpper && newMaxLower <= _ranges[i]._upper) {
                auto intervalCopy = *this;
                auto lowerRange = Reachability::range_t(_ranges[i]._lower, newMinUpper);
                auto upperRange = Reachability::range_t(newMaxLower, _ranges[i]._upper);
                newInterval._ranges[i] = lowerRange;
                intervalCopy._ranges[i] = upperRange;
                result.push_back(std::move(intervalCopy));
                result.push_back(std::move(newInterval));

            } else if (((int32_t)_ranges[i]._lower) <= newMinUpper) {
                auto lowerRange = Reachability::range_t(_ranges[i]._lower, newMinUpper);
                newInterval._ranges[i] = lowerRange;
                result.push_back(std::move(newInterval));
            } else if (newMaxLower <= _ranges[i]._upper) {
                auto upperRange = Reachability::range_t(newMaxLower, _ranges[i]._upper);
                newInterval._ranges[i] = upperRange;
                result.push_back(std::move(newInterval));
            }
        }
        return result;
    }

    void print() const {
        for (const auto &range : _ranges) {
            std::cout << " " << range._lower << "-" << range._upper << " ";
        }
    }

    std::string to_string() const {
        std::ostringstream strs;
        for (const auto &range : _ranges) {
            strs << " " << range._lower << "-" << range._upper << " ";
        }
        return strs.str();
    }
};

struct interval_dist_t {
    uint32_t _intervalId1;
    uint32_t _intervalId2;
    uint32_t _distance;
};

class interval_vector_t {
  private:
    std::vector<interval_t> _intervals;

  public:
    ~interval_vector_t() {}

    interval_vector_t() {}

    interval_vector_t(const std::vector<interval_t> &ranges) : _intervals(ranges){};

    std::vector<interval_t>::iterator begin() { return _intervals.begin(); }
    std::vector<interval_t>::iterator end() { return _intervals.end(); }
    std::vector<interval_t>::const_iterator begin() const { return _intervals.begin(); }
    std::vector<interval_t>::const_iterator end() const { return _intervals.end(); }

    bool empty() const { return _intervals.empty(); }

    void clear() { _intervals.clear(); }

    const interval_t &front() const { return _intervals[0]; }

    const interval_t &back() const { return _intervals.back(); }

    size_t size() const { return _intervals.size(); }

    size_t tuple_size() const { return _intervals[0].size(); }

    uint32_t get_contained_colors() const {
        uint32_t colors = 0;
        for (const auto &interval : _intervals) {
            colors += interval.get_contained_colors();
        }
        return colors;
    }

    static std::pair<uint32_t, uint32_t> shift_interval(uint32_t lower, uint32_t upper,
                                                        uint32_t ctSize, int32_t modifier) {
        int32_t lower_val = ctSize + (lower + modifier);
        int32_t upper_val = ctSize + (upper + modifier);
        return std::make_pair(lower_val % ctSize, upper_val % ctSize);
    }

    bool has_valid_intervals() const {
        for (const auto &interval : _intervals) {
            if (interval.is_sound()) {
                return true;
            }
        }
        return false;
    }

    const interval_t &operator[](size_t index) const {
        assert(index < _intervals.size());
        return _intervals[index];
    }

    interval_t &operator[](size_t index) {
        assert(index < _intervals.size());
        return _intervals[index];
    }

    void append(const interval_vector_t &other) {
        _intervals.insert(_intervals.end(), other._intervals.begin(), other._intervals.end());
    }

    interval_t is_range_end(const std::vector<uint32_t> &ids) const {
        for (uint32_t j = 0; j < _intervals.size(); j++) {
            bool rangeEnd = true;
            for (uint32_t i = 0; i < _intervals[j].size(); i++) {
                auto range = _intervals[j][i];
                if (range._upper != ids[i]) {
                    rangeEnd = false;
                    break;
                }
            }
            if (rangeEnd) {
                if (j + 1 != _intervals.size()) {
                    return _intervals[j + 1];
                } else {
                    return front();
                }
            }
        }
        return interval_t();
    }

    std::vector<Colored::interval_t> shrink_intervals(uint32_t newSize) const {
        std::vector<Colored::interval_t> resizedIntervals;
        for (auto &interval : _intervals) {
            Colored::interval_t resizedInterval;
            for (uint32_t i = 0; i < newSize; i++) {
                resizedInterval.add_range(interval[i]);
            }
            resizedIntervals.push_back(resizedInterval);
        }
        return resizedIntervals;
    }

    void add_interval(const interval_t &interval) {
        uint32_t vecIndex = 0;

        if (!_intervals.empty()) {
            assert(_intervals[0].size() == interval.size());
        } else {
            _intervals.push_back(interval);
            return;
        }

        for (auto &localInterval : _intervals) {
            bool extendsInterval = true;
            enum FoundPlace { undecided, greater, lower };
            FoundPlace foundPlace = undecided;

            for (uint32_t k = 0; k < interval.size(); k++) {
                if (interval[k]._lower > localInterval[k]._upper ||
                    localInterval[k]._lower > interval[k]._upper) {
                    extendsInterval = false;
                }
                if (interval[k]._lower < localInterval[k]._lower) {
                    if (foundPlace == undecided) {
                        foundPlace = lower;
                    }
                } else if (interval[k]._lower > localInterval[k]._lower) {
                    if (foundPlace == undecided) {
                        foundPlace = greater;
                    }
                }
                if (!extendsInterval && foundPlace != undecided) {
                    break;
                }
            }

            if (extendsInterval) {
                for (uint32_t k = 0; k < interval.size(); k++) {
                    localInterval[k] |= interval[k];
                }
                return;
            } else if (foundPlace == lower) {
                break;
            }
            vecIndex++;
        }

        _intervals.insert(_intervals.begin() + vecIndex, interval);
    }

    void constrain_lower(const std::vector<uint32_t> &values, bool strict) {
        for (uint32_t i = 0; i < _intervals.size(); ++i) {
            for (uint32_t j = 0; j < values.size(); ++j) {
                if (strict && _intervals[i][j]._lower <= values[j]) {
                    _intervals[i][j]._lower = values[j] + 1;
                } else if (!strict && _intervals[i][j]._lower < values[j]) {
                    _intervals[i][j]._lower = values[j];
                }
            }
        }
        simplify();
    }

    void constrain_upper(const std::vector<uint32_t> &values, bool strict) {
        for (uint32_t i = 0; i < _intervals.size(); ++i) {
            for (uint32_t j = 0; j < values.size(); ++j) {
                if (strict && _intervals[i][j]._upper >= values[j]) {
                    _intervals[i][j]._upper = values[j] - 1;
                } else if (!strict && _intervals[i][j]._upper > values[j]) {
                    _intervals[i][j]._upper = values[j];
                }
            }
        }
        simplify();
    }

    void print() const {
        for (const auto &interval : _intervals) {
            std::cout << "[";
            interval.print();
            std::cout << "]" << std::endl;
        }
    }

    std::string to_string() const {
        std::string out;
        for (const auto &interval : _intervals) {
            out += "[";
            out += interval.to_string();
            out += "]\n";
        }
        return out;
    }

    std::vector<uint32_t> get_lower_ids() const {
        std::vector<uint32_t> ids;
        for (const auto &interval : _intervals) {
            if (ids.empty()) {
                ids = interval.get_lower_ids();
            } else {
                for (uint32_t i = 0; i < ids.size(); i++) {
                    ids[i] = std::min(ids[i], interval[i]._lower);
                }
            }
        }
        return ids;
    }

    std::vector<uint32_t> get_lower_ids(int32_t modifier, const std::vector<size_t> &sizes) const {
        std::vector<uint32_t> ids;
        for (uint32_t j = 0; j < size(); j++) {
            auto &interval = _intervals[j];
            if (ids.empty()) {
                for (uint32_t i = 0; i < ids.size(); i++) {
                    auto shiftedInterval =
                        shift_interval(interval[i]._lower, interval[i]._upper, sizes[i], modifier);
                    if (shiftedInterval.first > shiftedInterval.second) {
                        ids.push_back(0);
                    } else {
                        ids.push_back(shiftedInterval.first);
                    }
                }
            } else {
                for (uint32_t i = 0; i < ids.size(); i++) {
                    if (ids[i] == 0) {
                        continue;
                    }
                    auto shiftedInterval =
                        shift_interval(interval[i]._lower, interval[i]._upper, sizes[i], modifier);
                    if (shiftedInterval.first > shiftedInterval.second) {
                        ids[i] = 0;
                    } else {
                        ids[i] = std::max(ids[i], shiftedInterval.first);
                    }
                }
            }
        }
        return ids;
    }

    std::vector<uint32_t> get_upper_ids(int32_t modifier, const std::vector<size_t> &sizes) const {
        std::vector<uint32_t> ids;
        for (uint32_t j = 0; j < size(); j++) {
            const auto &interval = _intervals[j];
            if (ids.empty()) {
                for (uint32_t i = 0; i < ids.size(); i++) {
                    auto shiftedInterval =
                        shift_interval(interval[i]._lower, interval[i]._upper, sizes[i], modifier);

                    if (shiftedInterval.first > shiftedInterval.second) {
                        ids.push_back(sizes[i] - 1);
                    } else {
                        ids.push_back(shiftedInterval.second);
                    }
                }
            } else {
                for (uint32_t i = 0; i < ids.size(); i++) {
                    if (ids[i] == sizes[i] - 1) {
                        continue;
                    }
                    auto shiftedInterval =
                        shift_interval(interval[i]._lower, interval[i]._upper, sizes[i], modifier);

                    if (shiftedInterval.first > shiftedInterval.second) {
                        ids[i] = sizes[i] - 1;
                    } else {
                        ids[i] = std::max(ids[i], shiftedInterval.second);
                    }
                }
            }
        }
        return ids;
    }

    void apply_modifier(int32_t modifier, const std::vector<size_t> &sizes) {
        std::vector<interval_t> collectedIntervals;
        for (auto &interval : _intervals) {
            std::vector<interval_t> newIntervals;
            newIntervals.push_back(std::move(interval));
            for (uint32_t i = 0; i < interval.size(); i++) {
                std::vector<interval_t> tempIntervals;
                for (auto &interval1 : newIntervals) {
                    auto shiftedInterval = shift_interval(interval1[i]._lower, interval1[i]._upper,
                                                          sizes[i], modifier);

                    if (shiftedInterval.first > shiftedInterval.second) {
                        auto newInterval = interval1;

                        interval1[i]._lower = 0;
                        interval1[i]._upper = shiftedInterval.second;

                        newInterval[i]._lower = shiftedInterval.first;
                        newInterval[i]._upper = sizes[i] - 1;
                        tempIntervals.push_back(std::move(newInterval));
                    } else {
                        interval1[i]._lower = shiftedInterval.first;
                        interval1[i]._upper = shiftedInterval.second;
                    }
                }
                newIntervals.insert(newIntervals.end(), tempIntervals.begin(), tempIntervals.end());
            }
            collectedIntervals.insert(collectedIntervals.end(), newIntervals.begin(),
                                      newIntervals.end());
        }

        _intervals = std::move(collectedIntervals);
    }

    bool contains(const interval_t &interval, const std::vector<bool> &diagonalPositions) const {
        for (const auto &localInterval : _intervals) {
            if (localInterval.contains(interval, diagonalPositions)) {
                return true;
            }
        }
        return false;
    }

    void remove_interval(uint32_t index) { _intervals.erase(_intervals.begin() + index); }

    void restrict(uint32_t k) {
        simplify();
        if (k == 0) {
            return;
        }

        while (size() > k) {
            interval_dist_t closestInterval = get_closest_intervals();
            auto &interval = _intervals[closestInterval._intervalId1];
            const auto &otherInterval = _intervals[closestInterval._intervalId2];

            for (uint32_t l = 0; l < interval.size(); l++) {
                interval[l] |= otherInterval[l];
            }

            _intervals.erase(_intervals.begin() + closestInterval._intervalId2);
        }
        simplify();
    }

    interval_dist_t get_closest_intervals() const {
        interval_dist_t currentBest = {0, 0, std::numeric_limits<uint32_t>::max()};
        for (uint32_t i = 0; i < size() - 1; i++) {
            const auto &interval = _intervals[i];
            for (uint32_t j = i + 1; j < size(); j++) {
                const auto &otherInterval = _intervals[j];
                uint32_t dist = 0;

                for (uint32_t k = 0; k < interval.size(); k++) {
                    int32_t val1 = otherInterval[k]._lower - interval[k]._upper;
                    int32_t val2 = interval[k]._lower - otherInterval[k]._upper;
                    dist += std::max(0, std::max(val1, val2));
                    if (dist >= currentBest._distance) {
                        break;
                    }
                }

                if (dist < currentBest._distance) {
                    currentBest._distance = dist;
                    currentBest._intervalId1 = i;
                    currentBest._intervalId2 = j;

                    // if the distance is 1 we cannot find any intervals that are closer so we stop
                    // searching
                    if (currentBest._distance == 1) {
                        return currentBest;
                    }
                }
            }
        }
        return currentBest;
    }

    void simplify() {
        while (!_intervals.empty() && !_intervals[0].is_sound()) {
            _intervals.erase(_intervals.begin());
        }
        for (size_t i = 0; i < _intervals.size(); ++i) {
            for (size_t j = _intervals.size() - 1; j > i; --j) {
                const auto &otherInterval = _intervals[j];
                auto &interval = _intervals[i];
                if (!otherInterval.is_sound()) {
                    _intervals.erase(_intervals.begin() + j);
                } else if (interval.intersects(otherInterval)) {
                    interval |= otherInterval;
                    _intervals.erase(_intervals.begin() + j);
                }
            }
        }
    }

    void combine_neighbours() {
        std::set<uint32_t> rangesToRemove;
        if (_intervals.empty()) {
            return;
        }

        for (uint32_t i = 0; i < _intervals.size(); i++) {
            auto &interval = _intervals[i];
            if (!interval.is_sound()) {
                rangesToRemove.insert(i);
                continue;
            }
            for (uint32_t j = i + 1; j < _intervals.size(); j++) {
                const auto &otherInterval = _intervals[j];

                if (!otherInterval.is_sound()) {
                    continue;
                }
                bool overlap = true;

                uint32_t dist = 1;

                if (overlap) {
                    for (uint32_t k = 0; k < interval.size(); k++) {
                        if (interval[k]._lower > otherInterval[k]._upper ||
                            otherInterval[k]._lower > interval[k]._upper) {
                            if (interval[k]._lower > otherInterval[k]._upper + dist ||
                                otherInterval[k]._lower > interval[k]._upper + dist) {
                                overlap = false;
                                break;
                            } else {
                                dist = 0;
                            }
                        }
                    }
                }

                if (overlap) {
                    for (uint32_t l = 0; l < interval.size(); l++) {
                        interval[l] |= otherInterval[l];
                    }
                    rangesToRemove.insert(j);
                }
            }
        }
        for (auto i = rangesToRemove.rbegin(); i != rangesToRemove.rend(); ++i) {
            _intervals.erase(_intervals.begin() + *i);
        }
    }
};
} // namespace Colored
} // namespace PetriEngine

#endif /* INTERVALS_H */