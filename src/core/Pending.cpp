#include "core/Pending.h"

namespace gf {

bool PendingQueue::removeAt(std::size_t idx) {
    if (idx >= items.size()) return false;
    items.erase(items.begin() + static_cast<std::ptrdiff_t>(idx));
    return true;
}

PendingChoice* PendingQueue::find(u32 id) {
    for (auto& p : items)
        if (p.id == id) return &p;
    return nullptr;
}

}  // namespace gf
