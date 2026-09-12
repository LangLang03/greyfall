#include "mkt/VolModel.h"

#include <algorithm>

namespace gf {

Fixed volVariance(const VolState& v) {
    return v.lastSigma * v.lastSigma;
}

Fixed volSigma(const VolState& v) {
    Fixed var = volVariance(v) + v.jumpBias;
    if (var.rawValue() < 1) var = Fixed::raw(1);
    return fxSqrt(var);
}

void volUpdate(VolState& v, Fixed ret, Fixed dt) {
    Fixed var = volVariance(v);
    Fixed eps2 = ret * ret;
    Fixed newVar = v.omega + v.alpha * eps2 + v.beta * var;
    // 下限：不能让波动率塌缩到 0（真实市场永远有噪声底）
    Fixed floor = Fixed::bp(1) * dt;
    if (newVar.rawValue() < floor.rawValue()) newVar = floor;
    // 上限：防止数值爆炸
    Fixed cap = Fixed(1);
    if (newVar.rawValue() > cap.rawValue()) newVar = cap;
    v.lastSigma = fxSqrt(newVar);
    v.lastReturn = ret;
    // 跳跃偏差自然衰减（半衰期约 2 tick）
    v.jumpBias = v.jumpBias / Fixed(2);
    if (v.jumpBias.rawValue() < 1) v.jumpBias = Fixed(0);
}

void volInjectJump(VolState& v, Fixed magnitude) {
    Fixed m = fxAbs(magnitude);
    v.jumpBias += m * m;
}

void volApplyToBook(VolState& v, Book& b) {
    Fixed ret = Fixed(0);
    if (b.open.rawValue() > 0) ret = Fixed::raw(mulDivSat(b.last.rawValue() - b.open.rawValue(), FIX, b.open.rawValue()));
    volUpdate(v, ret);
    b.sigma = volSigma(v);
    // V20：成交量的 EWMA（真实市场的"近期活跃度"）
    b.var20 = fxMax((b.var20 * Fixed(19) + Fixed(b.volume)) / Fixed(20), Fixed(1));
}

}  // namespace gf
