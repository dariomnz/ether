#pragma once

#include <algorithm>
#include <vector>

template <typename Container, typename Compare, typename Function>
void for_each_sorted(Container& c, Compare comp, Function f) {
    using ElementPtr = decltype(c.begin());
    std::vector<ElementPtr> ptrs;
    ptrs.reserve(std::size(c));

    for (auto it = c.begin(); it != c.end(); ++it) {
        ptrs.push_back(it);
    }

    std::sort(ptrs.begin(), ptrs.end(), [&comp](ElementPtr a, ElementPtr b) { return comp(*a, *b); });

    for (auto ptr : ptrs) {
        f(*ptr);
    }
}