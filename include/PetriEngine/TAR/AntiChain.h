// -*- mode: C++; c-file-style: "stroustrup"; c-basic-offset: 4; indent-tabs-mode: nil; -*-
///////////////////////////////////////////////////////////////////////////////
//
// This file is a part of UPPAAL.
// Copyright (c) 1995 - 2006, Uppsala University and Aalborg University.
// All right reserved.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef ANTICHAIN_H
#define ANTICHAIN_H

#include <algorithm>
#include <map>
#include <set>
#include <stack>
#include <unordered_map>
#include <vector>

template <typename T, typename U> class AntiChain {
    using set_t = std::set<U>;
    using sset_t = std::vector<U>;
    using smap_t = std::vector<std::vector<sset_t>>;

    smap_t _map;

    struct node_t {
        U _key;
        std::vector<node_t *> _children;
    };

  public:
    AntiChain() = default;
    ;

    void clear() { _map.clear(); }

    template <typename S> auto subsumed(T &el, const S &set) -> bool {
        bool exists = false;
        if (_map.size() > (size_t)el) {
            for (auto &s : _map[el]) {
                if (std::includes(set.begin(), set.end(), s.begin(), s.end())) {
                    /*std::cout << "SUBSUMBED BY ";
                    for(auto& e : s) std::cout << e << ",";
                    std::cout << std::endl;*/
                    exists = true;
                    break;
                }
            }
        }
        return exists;
    }

    template <typename S> auto insert(T &el, const S &set) -> bool {
        bool inserted = false;
        if (_map.size() <= (size_t)el)
            _map.resize(el + 1);
        /*            std::cout << "ANTI (" << (size_t)el << ") -> ";
                    for(auto& e : set) std::cout << e << ",";
                    std::cout << std::endl;*/
        if (!subsumed(el, set)) {
            auto &chains = _map[el];
            for (int i = chains.size() - 1; i >= 0; --i) {
                if (std::includes(chains[i].begin(), chains[i].end(), set.begin(), set.end())) {
                    chains.erase(chains.begin() + i);
                }
            }
            chains.emplace_back(sset_t{set.begin(), set.end()});
            inserted = true;
        } else {
            inserted = false;
        }

        return inserted;
    }
};

#endif /* ANTICHAIN_H */
